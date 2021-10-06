#define lj_sysprof_c
#define LUA_CORE

#include "lj_arch.h"
#include "lj_sysprof.h"

#if LJ_HASSYSPROF

#include "lj_obj.h"
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

#define SYSPROF_HANDLER_STACK_DEPTH 4
#define SYSPROF_BACKTRACE_BUF_SIZE 4096

enum sysprof_state {
  /* Profiler needs to be configured. */
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
  struct luam_sysprof_counters counters; /* Profiling counters. */
  struct luam_sysprof_options opt; /* Profiling options. */
  struct luam_sysprof_config config; /* Profiler configurations. */
  lj_profile_timer timer; /* Profiling timer. */
  int saved_errno; /* Saved errno when profiler failed. */
};

static struct sysprof sysprof = {0};

/* --- Stream ------------------------------------------------------------- */

static const uint8_t ljp_header[] = {'l', 'j', 'p', LJP_FORMAT_VERSION,
                                     0x0, 0x0, 0x0};

static int stream_is_needed(struct sysprof *sp)
{
  return LUAM_SYSPROF_DEFAULT != sp->opt.mode;
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

static void stream_lfunc(struct lj_wbuf *buf, GCfunc *func)
{
  GCproto *pt = funcproto(func);
  lua_assert(pt != NULL);
  lj_wbuf_addbyte(buf, LJP_FRAME_LFUNC);
  lj_wbuf_addu64(buf, (uintptr_t)pt);
  lj_wbuf_addu64(buf, (uint64_t)pt->firstline);
}

static void stream_cfunc(struct lj_wbuf *buf, GCfunc *func)
{
  lj_wbuf_addbyte(buf, LJP_FRAME_CFUNC);
  lj_wbuf_addu64(buf, (uintptr_t)func->c.f);
}

static void stream_ffunc(struct lj_wbuf *buf, GCfunc *func)
{
  lj_wbuf_addbyte(buf, LJP_FRAME_FFUNC);
  lj_wbuf_addu64(buf, func->c.ffid);
}

static void stream_frame_lua(struct lj_wbuf *buf, cTValue *frame)
{
  GCfunc *func = frame_func(frame);
  lua_assert(NULL != func);
  if (isluafunc(func)) {
    stream_lfunc(buf, func);
  } else if (isffunc(func)) {
    stream_ffunc(buf, func);
  } else {
    stream_cfunc(buf, func);
  }
}

static void stream_backtrace_lua(struct sysprof *sp)
{
  global_State *g = sp->g;
  struct lj_wbuf *buf = &sp->out;
  cTValue *top_frame = NULL, *frame = NULL, *bot = NULL;
  lua_State *L = NULL;

  lua_assert(g != NULL);
  L = gco2th(gcref(g->cur_L));
  lua_assert(L != NULL);

  top_frame = g->top_frame.guesttop.interp_base - (1 + LJ_FR2);
  bot = tvref(L->stack);
  /* Traverse frames backwards */
  for (frame = top_frame; frame > bot; frame = frame_prev(frame)) {
    if (frame_gc(frame) == obj2gco(L)) {
      continue;  /* Skip dummy frames. See lj_err_optype_call(). */
    }
    stream_frame_lua(buf, frame);
  }

  lj_wbuf_addbyte(buf, LJP_FRAME_LUA_LAST);
}

static int default_backtracer(void *backtrace_buf[], size_t max_depth)
{
  return backtrace(backtrace_buf, max_depth);
}

static void stream_backtrace_host(struct sysprof *sp)
{
  static void* backtrace_buf[SYSPROF_BACKTRACE_BUF_SIZE] = {};
  const int max_depth = sp->opt.mode == LUAM_SYSPROF_LEAF
                        ? SYSPROF_HANDLER_STACK_DEPTH + 1
                        : SYSPROF_BACKTRACE_BUF_SIZE;
  int depth = 0;

  lua_assert(sp->config.backtracer != NULL);
  depth = sp->config.backtracer(backtrace_buf, max_depth);

  for (int i = SYSPROF_HANDLER_STACK_DEPTH; i < depth; ++i) {
    lj_wbuf_addu64(&sp->out, (uintptr_t)backtrace_buf[i]);
    if (LJ_UNLIKELY(sp->opt.mode == LUAM_SYSPROF_LEAF)) {
      break;
    }
  }

  lj_wbuf_addu64(&sp->out, (uintptr_t)LJP_FRAME_HOST_LAST);
}

static void stream_trace(struct sysprof *sp)
{
  struct lj_wbuf *out = &sp->out;

  uint32_t traceno = sp->g->vmstate;
  jit_State *J = G2J(sp->g);

  lj_wbuf_addu64(out, traceno);
  lj_wbuf_addu64(out, (uintptr_t)J->prev_pt);
  lj_wbuf_addu64(out, J->prev_line);
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
  /* XXX: order is important */
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
  lua_assert((vmstate & ~(uint32_t)((1 << 4) - 1)) == 0);
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

  lua_assert(pthread_self() == sp->thread);

  /* Caveat: order of counters must match vmstate order in <lj_obj.h>. */
  ((uint64_t *)&sp->counters)[vmstate]++;

  sp->counters.samples++;
  sp->counters.overruns += info->si_overrun;

  if (stream_is_needed(sp)) {
    stream_event(sp, vmstate);
    if (LJ_UNLIKELY(lj_wbuf_test_flag(&sp->out, STREAM_ERRIO|STREAM_STOP))) {
      sp->saved_errno = lj_wbuf_errno(&sp->out);
      lj_wbuf_terminate(&sp->out);
      sp->state = SPS_HALT;
    }
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
      /* noop */
      break;

    default:
      lua_assert(0);
      break;
  }
}

/* -- Internal ------------------------------------------------------------ */

static int sysprof_validate(struct sysprof *sp,
                            const struct luam_sysprof_options *opt)
{
  switch (sp->state) {
    case SPS_UNCONFIGURED:
      return SYSPROF_ERRUSE;

    case SPS_IDLE: {
      if (opt->mode > LUAM_SYSPROF_CALLGRAPH) {
        return SYSPROF_ERRUSE;
      } else if (opt->mode != LUAM_SYSPROF_DEFAULT &&
                 (opt->buf == NULL || opt->len == 0 ||
                  sp->config.writer == NULL || sp->config.on_stop == NULL)) {
        return SYSPROF_ERRUSE;
      } else if (opt->interval == 0) {
        return SYSPROF_ERRUSE;
      }
      break;
    }

    case SPS_PROFILE:
    case SPS_HALT:
      return SYSPROF_ERRRUN;

    default:
      lua_assert(0);
  }

  return SYSPROF_SUCCESS;
}

static int sysprof_init(struct sysprof *sp, lua_State *L,
                        const struct luam_sysprof_options *opt)
{
  int status = sysprof_validate(sp, opt);
  if (SYSPROF_SUCCESS != status) {
    return status;
  }

  /* Copy validated options to sysprof state. */
  memcpy(&sp->opt, opt, sizeof(sp->opt));

  /* Init general fields. */
  sp->g = G(L);
  sp->thread = pthread_self();

  /* Reset counters. */
  memset(&sp->counters, 0, sizeof(sp->counters));

  /* Reset saved errno. */
  sp->saved_errno = 0;

  if (stream_is_needed(sp)) {
    lj_wbuf_init(&sp->out, sp->config.writer, opt->ctx, opt->buf, opt->len);
  }

  return SYSPROF_SUCCESS;
}

/* -- Public profiling API ------------------------------------------------ */

int lj_sysprof_configure(const struct luam_sysprof_config *config)
{
  struct sysprof *sp = &sysprof;
  lua_assert(config != NULL);
  if (sp->state != SPS_UNCONFIGURED && sp->state != SPS_IDLE) {
    return SYSPROF_ERRUSE;
  }

  memcpy(&sp->config, config, sizeof(*config));

  if (sp->config.backtracer == NULL) {
    sp->config.backtracer = default_backtracer;
  }

  sp->state = SPS_IDLE;

  return SYSPROF_SUCCESS;
}

int lj_sysprof_start(lua_State *L, const struct luam_sysprof_options *opt)
{
  struct sysprof *sp = &sysprof;

  int status = sysprof_init(sp, L, opt);
  if (SYSPROF_SUCCESS != status) {
    if (NULL != sp->config.on_stop) {
      /*
      ** Initialization may fail in case of unconfigured sysprof,
      ** so we cannot guarantee cleaning up resources in this case.
      */
      sp->config.on_stop(opt->ctx, opt->buf);
    }
    return status;
  }

  sp->state = SPS_PROFILE;

  if (stream_is_needed(sp)) {
    stream_prologue(sp);
    if (LJ_UNLIKELY(lj_wbuf_test_flag(&sp->out, STREAM_ERRIO|STREAM_STOP))) {
      /* on_stop call may change errno value. */
      int saved_errno = lj_wbuf_errno(&sp->out);
      /* Ignore possible errors. mp->out.buf may be NULL here. */
      sp->config.on_stop(opt->ctx, sp->out.buf);
      lj_wbuf_terminate(&sp->out);
      sp->state = SPS_IDLE;
      errno = saved_errno;
      return SYSPROF_ERRIO;
    }
  }

  sp->timer.opt.interval_msec = opt->interval;
  sp->timer.opt.handler = sysprof_signal_handler;
  lj_profile_timer_start(&sp->timer);

  return SYSPROF_SUCCESS;
}

int lj_sysprof_stop(lua_State *L)
{
  struct sysprof *sp = &sysprof;
  global_State *g = sp->g;
  struct lj_wbuf *out = &sp->out;

  if (SPS_IDLE == sp->state) {
    return SYSPROF_ERRSTOP;
  } else if (G(L) != g) {
    return SYSPROF_ERRUSE;
  }

  lj_profile_timer_stop(&sp->timer);

  if (SPS_HALT == sp->state) {
    errno = sp->saved_errno;
    sp->state = SPS_IDLE;
    /* wbuf was terminated when error occured. */
    return SYSPROF_ERRIO;
  }

  sp->state = SPS_IDLE;

  if (stream_is_needed(sp)) {
    int cb_status = 0;

    stream_epilogue(sp);
    lj_wbuf_flush(out);

    cb_status = sp->config.on_stop(sp->opt.ctx, out->buf);
    if (LJ_UNLIKELY(lj_wbuf_test_flag(out, STREAM_ERRIO|STREAM_STOP)) ||
        0 != cb_status) {
      errno = lj_wbuf_errno(out);
      lj_wbuf_terminate(out);
      return SYSPROF_ERRIO;
    }

    lj_wbuf_terminate(out);
  }

  return SYSPROF_SUCCESS;
}

int lj_sysprof_report(struct luam_sysprof_counters *counters)
{
  const struct sysprof *sp = &sysprof;
  if (sp->state != SPS_IDLE) {
    return SYSPROF_ERRUSE;
  }
  memcpy(counters, &sp->counters, sizeof(sp->counters));
  return SYSPROF_SUCCESS;
}

#else /* LJ_HASSYSPROF */

int lj_sysprof_configure(const struct luam_sysprof_config *config)
{
  UNUSED(config);
  return SYSPROF_ERRUSE;
}

int lj_sysprof_start(lua_State *L, const struct luam_sysprof_options *opt)
{
  UNUSED(L);
  opt->on_stop(opt->ctx, opt->buf);
  return SYSPROF_ERRUSE;
}

int lj_sysprof_stop(lua_State *L)
{
  UNUSED(L);
  return SYSPROF_ERRUSE;
}

int lj_sysprof_report(struct luam_sysprof_counters *counters)
{
  UNUSED(counters);
  return SYSPROF_ERRUSE;
}

#endif /* LJ_HASSYSPROF */

