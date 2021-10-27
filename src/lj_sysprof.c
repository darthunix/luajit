/*
** Implementation of the platform and Lua profiler.
*/

#define lj_sysprof_c
#define LUA_CORE

#include "lj_arch.h"
#include "lj_sysprof.h"
#include "lmisclib.h"

#if LJ_HASSYSPROF

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_debug.h"
#include "lj_dispatch.h"
#include "lj_frame.h"

#if LJ_HASJIT
#include "lj_jit.h"
#include "lj_trace.h"
#endif

#include "lj_wbuf.h"
#include "lj_profile_timer.h"
#include "lj_symtab.h"

#include <pthread.h>
#include <errno.h>
#include <execinfo.h>

/*
** When calling backtrace, we need to strip the calling graph head
** containing frames of the system profiler in order not to burden
** the host stack and not to dump excess frames to the file.
**
** The sysprof's stack is:
** -> TODO
*/
#define SYSPROF_HANDLER_STACK_DEPTH 6

#define SYSPROF_BACKTRACE_BUF_SIZE 512

enum sysprof_state {
  /* Profiler is not initialized. */
  SPS_UNCONFIGURED,
  /* Profiler is not running. */
  SPS_IDLE,
  /* Profiler is running. */
  SPS_PROFILE,
  /*
  ** Stopped in case of stopped or failed stream.
  ** Saved errno is set at luaM_sysprof_stop.
  */
  SPS_HALT
};

struct sysprof {
  global_State *g; /* Profiled VM. */
  pthread_t thread; /* Profiled thread. */
  enum sysprof_state state; /* Internal state. */
  struct lj_wbuf out; /* Output accumulator. */
  struct luam_Sysprof_Counters counters; /* Profiling counters. */
  struct luam_Sysprof_Config cfg; /* Profiler configurations. */
  void *ctx; /* Context for the writer. */
  lj_profile_timer timer; /* Profiling timer. */
  int saved_errno; /* Saved errno when profiler failed. */
};

/*
** We have to use a static profiler state. You can still use
** multiple VMs in multiple threads, but only profile one at a time.
*/
static struct sysprof sysprof = {0};

/* -- Default configuration ----------------------------------------------- */

/*
** Default backtracer function. Just call the GCC `backtrace`.
**
** XXX: note, that the GCC dynamic library must be preloaded. Otherwise,
** `backtrace` call will violate POSIX AS-safety trying to load the GCC
** library dynamically.
*/
static int default_backtrace_host(void *addr_buf[], size_t max_depth)
{
  return backtrace(addr_buf, max_depth);
}

/* -- Stream -------------------------------------------------------------- */

static const uint8_t ljp_header[] = {'l', 'j', 'p', LJP_FORMAT_VERSION,
				     0x0, 0x0, 0x0};

static int stream_is_needed(struct sysprof *sp)
{
  return sp->cfg.mode != LUAM_SYSPROF_DEFAULT;
}

static void stream_prologue(struct sysprof *sp)
{
  lj_symtab_dump(&sp->out, sp->g);
  lj_wbuf_addn(&sp->out, ljp_header, sizeof(ljp_header));
}

static void stream_epilogue(struct sysprof *sp)
{
  lj_wbuf_addbyte(&sp->out, LJP_EPILOGUE_BYTE);
}

static void stream_lfunc(struct lj_wbuf *buf, const GCfunc *func)
{
  GCproto *pt = NULL;
  lua_assert(isluafunc(func));

  pt = funcproto(func);
  lua_assert(pt != NULL);

  lj_wbuf_addbyte(buf, LJP_FRAME_LFUNC);
  lj_wbuf_addu64(buf, (uintptr_t)pt);
  lj_wbuf_addu64(buf, (uint64_t)pt->firstline);
}

static void stream_cfunc(struct lj_wbuf *buf, const GCfunc *func)
{
  lua_assert(iscfunc(func));
  lj_wbuf_addbyte(buf, LJP_FRAME_CFUNC);
  lj_wbuf_addu64(buf, (uintptr_t)func->c.f);
}

static void stream_ffunc(struct lj_wbuf *buf, const GCfunc *func)
{
  lua_assert(isffunc(func));
  lj_wbuf_addbyte(buf, LJP_FRAME_FFUNC);
  lj_wbuf_addu64(buf, func->c.ffid);
}

static void stream_frame_lua(struct lj_wbuf *buf, cTValue *frame)
{
  const GCfunc *func = frame_func(frame);
  lua_assert(func != NULL);
  if (isluafunc(func)) {
    stream_lfunc(buf, func);
  } else if (isffunc(func)) {
    stream_ffunc(buf, func);
  } else if (iscfunc(func)) {
    stream_cfunc(buf, func);
  } else {
    lua_assert(0);
  }
}

static void stream_backtrace_lua(struct sysprof *sp)
{
  global_State *g = sp->g;
  struct lj_wbuf *buf = &sp->out;
  cTValue *top_frame, *frame, *bot;
  lua_State *L;

  lua_assert(g != NULL);
  L = gco2th(gcref(g->cur_L));
  lua_assert(L != NULL);

  top_frame = g->top_frame - 1;
  bot = tvref(L->stack) + LJ_FR2;
  /* Traverse frames backwards */
  for (frame = top_frame; frame > bot; frame = frame_prev(frame)) {
    if (frame_gc(frame) == obj2gco(L)) {
      continue;  /* Skip dummy frames. See lj_err_optype_call(). */
    }
    stream_frame_lua(buf, frame);
  }

  lj_wbuf_addbyte(buf, LJP_FRAME_LUA_LAST);
}

static void stream_backtrace_host(struct sysprof *sp)
{
  static void *backtrace_buf[SYSPROF_BACKTRACE_BUF_SIZE];

  size_t backtrace_max_depth =
    sp->cfg.mode == LUAM_SYSPROF_LEAF ? SYSPROF_HANDLER_STACK_DEPTH + 1
                                      : SYSPROF_BACKTRACE_BUF_SIZE;
  size_t depth;

  lua_assert(sp->cfg.backtracer != NULL);
  depth = sp->cfg.backtracer(backtrace_buf, backtrace_max_depth);
  lua_assert(depth <= backtrace_max_depth);

  for (size_t i = SYSPROF_HANDLER_STACK_DEPTH; i < depth; ++i) {
    lj_wbuf_addu64(&sp->out, (uintptr_t)backtrace_buf[i]);
  }

  lj_wbuf_addu64(&sp->out, (uintptr_t)LJP_FRAME_HOST_LAST);
}

static void stream_trace(struct sysprof *sp)
{
#if LJ_HASJIT
  struct lj_wbuf *out = &sp->out;
  uint32_t traceno = sp->g->vmstate;
  lj_wbuf_addu64(out, traceno);
#else
  UNUSED(sp);
#endif /* LJ_HASJIT */
}

static void stream_guest(struct sysprof *sp)
{
  stream_backtrace_lua(sp);
  stream_backtrace_host(sp);
}

static void stream_host(struct sysprof *sp)
{
  stream_backtrace_host(sp);
}

typedef void (*event_streamer)(struct sysprof *sp);

static event_streamer event_streamers[] = {
  /* XXX: order is important. */
  stream_host,  /* LJ_VMST_INTERP */
  stream_guest, /* LJ_VMST_LFUNC */
  stream_guest, /* LJ_VMST_FFUNC */
  stream_guest, /* LJ_VMST_CFUNC */
  stream_host,  /* LJ_VMST_GC */
  stream_host,  /* LJ_VMST_EXIT */
  stream_host,  /* LJ_VMST_RECORD */
  stream_host,  /* LJ_VMST_OPT */
  stream_host,  /* LJ_VMST_ASM */
  stream_trace  /* LJ_VMST_TRACE */
};

static void stream_event(struct sysprof *sp, uint32_t vmstate)
{
  event_streamer stream = NULL;

  /* Check that vmstate fits in 4 bits (see streaming format) */
  lua_assert((vmstate & LJP_VMSTATE_MASK) == vmstate);
  lj_wbuf_addbyte(&sp->out, (uint8_t)vmstate);

  stream = event_streamers[vmstate];
  lua_assert(NULL != stream);
  stream(sp);
}

/* -- Signal handler ------------------------------------------------------ */

static void sysprof_record_sample(struct sysprof *sp, siginfo_t *info)
{
  global_State *g = sp->g;
  uint32_t _vmstate = ~(uint32_t)(g->vmstate);
  uint32_t vmstate = _vmstate < LJ_VMST_TRACE ? _vmstate : LJ_VMST_TRACE;

  /*
  ** Check if handler is called in the same thread, where the VM
  ** executes, as we want to record the host stack under the handler.
  */
  lua_assert(pthread_self() == sp->thread);

  /* Caveat: order of counters must match vmstate order in <lj_obj.h>. */
  ((uint64_t *)&sp->counters)[vmstate]++;

  sp->counters.samples++;
  sp->counters.overruns += info->si_overrun;

  if (!stream_is_needed(sp)) return;

  stream_event(sp, vmstate);
  if (LJ_UNLIKELY(lj_wbuf_test_flag(&sp->out, STREAM_ERRIO | STREAM_STOP))) {
    sp->saved_errno = lj_wbuf_errno(&sp->out);
    lj_wbuf_terminate(&sp->out);
    sp->state = SPS_HALT;
  }
}

static void sysprof_signal_handler(int sig, siginfo_t *info, void *ctx)
{
  struct sysprof *sp = &sysprof;
  UNUSED(sig);
  UNUSED(ctx);

  switch (sp->state) {
    case SPS_PROFILE:
      sysprof_record_sample(sp, info);
      break;

    case SPS_IDLE:
    case SPS_HALT:
      /* Noop. */
      break;

    default:
      lua_assert(0);
      break;
  }
}

/* -- Internal ------------------------------------------------------------ */

static int stream_is_valid(const struct luam_Sysprof_Config *cfg)
{
  return cfg->buf != NULL && cfg->buf_len != 0 && cfg->writer != NULL &&
	 cfg->on_stop != NULL;
}

static int sysprof_validate_config(const struct luam_Sysprof_Config *cfg)
{
  int config_is_valid =
    cfg->mode >= LUAM_SYSPROF_DEFAULT &&
    cfg->mode <= LUAM_SYSPROF_CALLGRAPH &&
    (cfg->mode == LUAM_SYSPROF_DEFAULT || stream_is_valid(cfg));

  return config_is_valid ? PROFILE_SUCCESS : PROFILE_ERRUSE;
}

static int sysprof_prepare(struct sysprof *sp, lua_State *L, void *ctx)
{
  /* Init general fields. */
  sp->g = G(L);
  sp->thread = pthread_self();
  sp->ctx = ctx;
  /* Reset counters. */
  memset(&sp->counters, 0, sizeof(sp->counters));
  /* Reset saved errno. */
  sp->saved_errno = 0;

  if (!stream_is_needed(sp))
    return PROFILE_SUCCESS;

  lj_wbuf_init(&sp->out, sp->cfg.writer, sp->ctx,
               sp->cfg.buf, sp->cfg.buf_len);

  return PROFILE_SUCCESS;
}

/* -- Public profiling API ------------------------------------------------ */

int lj_sysprof_init(const struct luam_Sysprof_Config *config)
{
  struct sysprof *sp = &sysprof;
  lua_assert(config != NULL);

  if (sp->state != SPS_UNCONFIGURED && sp->state != SPS_IDLE)
    return PROFILE_ERRRUN;

  int status = sysprof_validate_config(config);
  if (status != PROFILE_SUCCESS)
    return status;

  memcpy(&sp->cfg, config, sizeof(sp->cfg));

  if (sp->cfg.interval == 0)
    sp->cfg.interval = LUAM_SYSPROF_INTERVAL_DEFAULT;

  if (!sp->cfg.backtracer)
    sp->cfg.backtracer = default_backtrace_host;

  sp->state = SPS_IDLE;

  return PROFILE_SUCCESS;
}

int lj_sysprof_start(lua_State *L, void *ctx)
{
  struct sysprof *sp = &sysprof;
  if (sp->state == SPS_UNCONFIGURED)
    return PROFILE_ERRUSE;
  else if (sp->state != SPS_IDLE)
    return PROFILE_ERRRUN;

  int status = sysprof_prepare(sp, L, ctx);
  if (status != PROFILE_SUCCESS) {
    lua_assert(sp->cfg.on_stop != NULL);
    sp->cfg.on_stop(ctx, sp->cfg.buf);
    return status;
  }

  sp->state = SPS_PROFILE;

  if (stream_is_needed(sp)) {
    stream_prologue(sp);
    if (LJ_UNLIKELY(lj_wbuf_test_flag(&sp->out, STREAM_ERRIO | STREAM_STOP))) {
      /* on_stop call may change errno value. */
      const int saved_errno = lj_wbuf_errno(&sp->out);
      /* Ignore possible errors. mp->out.buf may be NULL here. */
      sp->cfg.on_stop(ctx, sp->out.buf);
      lj_wbuf_terminate(&sp->out);
      sp->state = SPS_IDLE;
      errno = saved_errno;
      return PROFILE_ERRIO;
    }
  }

  sp->timer.opt.interval_msec = sp->cfg.interval;
  sp->timer.opt.handler = sysprof_signal_handler;
  lj_profile_timer_start(&sp->timer);

  return PROFILE_SUCCESS;
}

int lj_sysprof_stop(lua_State *L)
{
  struct sysprof *sp = &sysprof;
  global_State *g = sp->g;
  struct lj_wbuf *out = &sp->out;

  if (sp->state == SPS_IDLE)
    return PROFILE_ERRRUN;
  else if (G(L) != g)
    return PROFILE_ERRUSE;

  lj_profile_timer_stop(&sp->timer);

  if (sp->state == SPS_HALT) {
    errno = sp->saved_errno;
    sp->state = SPS_IDLE;
    /* wbuf was terminated when error occured. */
    return PROFILE_ERRIO;
  }

  sp->state = SPS_IDLE;

  if (stream_is_needed(sp)) {
    int cb_status;

    stream_epilogue(sp);
    lj_wbuf_flush(out);

    cb_status = sp->cfg.on_stop(sp->ctx, out->buf);
    if (LJ_UNLIKELY(lj_wbuf_test_flag(out, STREAM_ERRIO | STREAM_STOP)) ||
	cb_status != 0) {
      errno = lj_wbuf_errno(out);
      lj_wbuf_terminate(out);
      return PROFILE_ERRIO;
    }

    lj_wbuf_terminate(out);
  }

  return PROFILE_SUCCESS;
}

int lj_sysprof_report(struct luam_Sysprof_Counters *counters)
{
  const struct sysprof *sp = &sysprof;
  if (sp->state != SPS_IDLE)
    return PROFILE_ERRUSE;

  memcpy(counters, &sp->counters, sizeof(sp->counters));
  return PROFILE_SUCCESS;
}

#else /* LJ_HASSYSPROF */

int lj_sysprof_init(const struct luam_Sysprof_Config *config)
{
  UNUSED(config);
  return PROFILE_ERRUSE;
}

int lj_sysprof_start(lua_State *L, void *ctx)
{
  UNUSED(L);
  UNUSED(ctx);
  return PROFILE_ERRUSE;
}

int lj_sysprof_stop(lua_State *L)
{
  UNUSED(L);
  return PROFILE_ERRUSE;
}

int lj_sysprof_report(struct luam_Sysprof_Counters *counters)
{
  UNUSED(counters);
  return PROFILE_ERRUSE;
}

#endif /* LJ_HASSYSPROF */
