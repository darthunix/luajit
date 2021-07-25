/*
** Miscellaneous Lua extensions library.
**
** Major portions taken verbatim or adapted from the LuaVela interpreter.
** Copyright (C) 2015-2019 IPONWEB Ltd.
*/

#define lib_misc_c
#define LUA_LIB

#include <stdio.h>
#include <errno.h>

#include "lua.h"
#include "lmisclib.h"
#include "lauxlib.h"

#include "lj_obj.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_lib.h"
#include "lj_gc.h"
#include "lj_err.h"

#include "lj_memprof.h"

/* ------------------------------------------------------------------------ */

static LJ_AINLINE void setnumfield(struct lua_State *L, GCtab *t,
				   const char *name, int64_t val)
{
  setnumV(lj_tab_setstr(L, t, lj_str_newz(L, name)), (double)val);
}

#define LJLIB_MODULE_misc

LJLIB_CF(misc_getmetrics)
{
  struct luam_Metrics metrics;
  GCtab *m;

  lua_createtable(L, 0, 19);
  m = tabV(L->top - 1);

  luaM_metrics(L, &metrics);

  setnumfield(L, m, "strhash_hit", metrics.strhash_hit);
  setnumfield(L, m, "strhash_miss", metrics.strhash_miss);

  setnumfield(L, m, "gc_strnum", metrics.gc_strnum);
  setnumfield(L, m, "gc_tabnum", metrics.gc_tabnum);
  setnumfield(L, m, "gc_udatanum", metrics.gc_udatanum);
  setnumfield(L, m, "gc_cdatanum", metrics.gc_cdatanum);

  setnumfield(L, m, "gc_total", metrics.gc_total);
  setnumfield(L, m, "gc_freed", metrics.gc_freed);
  setnumfield(L, m, "gc_allocated", metrics.gc_allocated);

  setnumfield(L, m, "gc_steps_pause", metrics.gc_steps_pause);
  setnumfield(L, m, "gc_steps_propagate", metrics.gc_steps_propagate);
  setnumfield(L, m, "gc_steps_atomic", metrics.gc_steps_atomic);
  setnumfield(L, m, "gc_steps_sweepstring", metrics.gc_steps_sweepstring);
  setnumfield(L, m, "gc_steps_sweep", metrics.gc_steps_sweep);
  setnumfield(L, m, "gc_steps_finalize", metrics.gc_steps_finalize);

  setnumfield(L, m, "jit_snap_restore", metrics.jit_snap_restore);
  setnumfield(L, m, "jit_trace_abort", metrics.jit_trace_abort);
  setnumfield(L, m, "jit_mcode_size", metrics.jit_mcode_size);
  setnumfield(L, m, "jit_trace_num", metrics.jit_trace_num);

  return 1;
}

/* ------------------------------------------------------------------------ */

#include "lj_libdef.h"

/* --------- profile common  ---------------------------------------------- */

/*
** Yep, 8Mb. Tuned in order not to bother the platform with too often flushes.
*/
#define STREAM_BUFFER_SIZE (8 * 1024 * 1024)

/* Structure given as ctx to memprof writer and on_stop callback. */
struct profile_ctx {
  /* Output file stream for data. */
  FILE *stream;
  /* Profiled global_State for lj_mem_free at on_stop callback. */
  global_State *g;
  /* Buffer for data. */
  uint8_t buf[STREAM_BUFFER_SIZE];
};

/*
** Default buffer writer function.
** Just call fwrite to the corresponding FILE.
*/
static size_t buffer_writer_default(const void **buf_addr, size_t len,
				    void *opt)
{
  struct profile_ctx *ctx = opt;
  FILE *stream = ctx->stream;
  const void * const buf_start = *buf_addr;
  const void *data = *buf_addr;
  size_t write_total = 0;

  lua_assert(len <= STREAM_BUFFER_SIZE);

  for (;;) {
    const size_t written = fwrite(data, 1, len - write_total, stream);

    if (LJ_UNLIKELY(written == 0)) {
      /* Re-tries write in case of EINTR. */
      if (errno != EINTR) {
	/* Will be freed as whole chunk later. */
	*buf_addr = NULL;
	return write_total;
      }

      errno = 0;
      continue;
    }

    write_total += written;
    lua_assert(write_total <= len);

    if (write_total == len)
      break;

    data = (uint8_t *)data + (ptrdiff_t)written;
  }

  *buf_addr = buf_start;
  return write_total;
}

/* Default on stop callback. Just close the corresponding stream. */
static int on_stop_cb_default(void *opt, uint8_t *buf)
{
  struct profile_ctx *ctx = opt;
  FILE *stream = ctx->stream;
  UNUSED(buf);
  lj_mem_free(ctx->g, ctx, sizeof(*ctx));
  return fclose(stream);
}

/* ----- misc.sysprof module ---------------------------------------------- */

/* Not loaded by default, use: local profile = require("misc.sysprof") */
#define LJLIB_MODULE_misc_sysprof

/* The default profiling interval equals to 11 ms. */
#define SYSPROF_DEFAULT_INTERVAL (11)
#define SYSPROF_DEFAULT_OUTPUT "sysprof.bin"

int parse_sysprof_opts(lua_State *L, struct luam_sysprof_options *opt, int idx) {
  GCtab *options = lj_lib_checktab(L, idx);

  /* get profiling mode */
  {
    cTValue *mode_opt = lj_tab_getstr(options, lj_str_newlit(L, "mode"));
    char mode = 0;
    if (mode_opt) {
      mode = *strVdata(mode_opt);
    }

    switch (mode) {
      case 'D':
        opt->mode = LUAM_SYSPROF_DEFAULT;
        break;
      case 'L':
        opt->mode = LUAM_SYSPROF_LEAF;
        break;
      case 'C':
        opt->mode = LUAM_SYSPROF_CALLGRAPH;
        break;
      default:
        return SYSPROF_ERRUSE;
    }
  }

  /* Get profiling interval. */
  {
    cTValue *interval = lj_tab_getstr(options, lj_str_newlit(L, "interval"));
    opt->interval = SYSPROF_DEFAULT_INTERVAL;
    if (interval) {
      int32_t signed_interval = numberVint(interval);
      if (signed_interval < 1) {
        return SYSPROF_ERRUSE;
      }
      opt->interval = signed_interval;
    }
  }

  return SYSPROF_SUCCESS;
}

int set_output_path(const char* path, struct luam_sysprof_options *opt) {
  lua_assert(path != NULL);
  struct profile_ctx *ctx = opt->ctx;
  FILE *stream = fopen(path, "wb");
  if(!stream) {
    return SYSPROF_ERRIO;
  }
  ctx->stream = stream;
  return SYSPROF_SUCCESS;
}

const char* parse_output_path(lua_State *L, struct luam_sysprof_options *opt, int idx) {
  /* By default, sysprof writes events to a `sysprof.bin` file. */
  const char *path = luaL_checkstring(L, idx);
  return path ? path : SYSPROF_DEFAULT_OUTPUT;
}

int parse_options(lua_State *L, struct luam_sysprof_options *opt)
{
  int status = SYSPROF_SUCCESS;

  switch(lua_gettop(L)) {
    case 2:
      if(!(lua_isstring(L, 1) && lua_istable(L, 2))) {
        status = SYSPROF_ERRUSE;
        break;
      }
      const char* path = parse_output_path(L, opt, 1); 
      status = set_output_path(path, opt);
      if(status != SYSPROF_SUCCESS) 
        break;

      status = parse_sysprof_opts(L, opt, 2);
      break;
    case 1:
      if(!lua_istable(L, 1)) {
        status = SYSPROF_ERRUSE;
        break;
      }
      status = parse_sysprof_opts(L, opt, 1);
      if(status != SYSPROF_SUCCESS)
        break;
      status = set_output_path(SYSPROF_DEFAULT_OUTPUT, opt);
      break;
    default:
      status = SYSPROF_ERRUSE;
      break;
  } 
  return status;
}

int sysprof_error(lua_State *L, int status)
{
  switch (status) {
    case SYSPROF_ERRUSE:
      lua_pushnil(L);
      lua_pushstring(L, err2msg(LJ_ERR_PROF_MISUSE));
      lua_pushinteger(L, EINVAL);
      return 3;
    case SYSPROF_ERRRUN:
      lua_pushnil(L);
      lua_pushstring(L, err2msg(LJ_ERR_PROF_ISRUNNING));
      lua_pushinteger(L, EINVAL);
      return 3;
    case SYSPROF_ERRSTOP:
      lua_pushnil(L);
      lua_pushstring(L, err2msg(LJ_ERR_PROF_NOTRUNNING));
      lua_pushinteger(L, EINVAL);
      return 3;
    case SYSPROF_ERRIO:
      return luaL_fileresult(L, 0, NULL);
    default:
      lua_assert(0);
      return 0;
  }
}

/* local res, err, errno = sysprof.start(options) */
LJLIB_CF(misc_sysprof_start)
{
  int status = SYSPROF_SUCCESS;

  struct luam_sysprof_options opt = {};
  struct profile_ctx *ctx = NULL;

  ctx = lj_mem_new(L, sizeof(*ctx));
  ctx->g = G(L);
  opt.ctx = ctx;
  opt.buf = ctx->buf;
  opt.len = STREAM_BUFFER_SIZE;

  status = parse_options(L, &opt);
  if (LJ_UNLIKELY(status != PROFILE_SUCCESS)) {
    lj_mem_free(ctx->g, ctx, sizeof(*ctx));
    return sysprof_error(L, status);
  }

  const struct luam_sysprof_config conf = {buffer_writer_default, on_stop_cb_default, NULL};
  status = luaM_sysprof_configure(&conf);
  if (LJ_UNLIKELY(status != PROFILE_SUCCESS)) {
    return sysprof_error(L, status);
  }

  status = luaM_sysprof_start(L, &opt);
  if (LJ_UNLIKELY(status != PROFILE_SUCCESS)) {
    /* Allocated memory will be freed in on_stop callback. */
    return sysprof_error(L, status);
  }

  lua_pushboolean(L, 1);
  return 1;
}

/* local res, err, errno = profile.sysprof_stop() */
LJLIB_CF(misc_sysprof_stop)
{
  int status = luaM_sysprof_stop(L);
  if (LJ_UNLIKELY(status != PROFILE_SUCCESS)) {
    return sysprof_error(L, status);
  }
  lua_pushboolean(L, 1);
  return 1;
}

/* local counters, err, errno = sysprof.report() */
LJLIB_CF(misc_sysprof_report)
{
  struct luam_sysprof_counters counters = {};
  GCtab *data_tab = NULL;
  GCtab *count_tab = NULL;

  int status = luaM_sysprof_report(&counters);
  if (status != SYSPROF_SUCCESS) {
    return sysprof_error(L, status);
  }

  lua_createtable(L, 0, 3);
  data_tab = tabV(L->top - 1);

  setnumfield(L, data_tab, "samples", counters.samples);
  setnumfield(L, data_tab, "overruns", counters.overruns);

  lua_createtable(L, 0, LJ_VMST__MAX + 1);
  count_tab = tabV(L->top - 1);

  setnumfield(L, count_tab, "INTERP", counters.vmst_interp);
  setnumfield(L, count_tab, "LFUNC",  counters.vmst_lfunc);
  setnumfield(L, count_tab, "FFUNC",  counters.vmst_ffunc);
  setnumfield(L, count_tab, "CFUNC",  counters.vmst_cfunc);
  setnumfield(L, count_tab, "GC",     counters.vmst_gc);
  setnumfield(L, count_tab, "EXIT",   counters.vmst_exit);
  setnumfield(L, count_tab, "RECORD", counters.vmst_record);
  setnumfield(L, count_tab, "OPT",    counters.vmst_opt);
  setnumfield(L, count_tab, "ASM",    counters.vmst_asm);
  setnumfield(L, count_tab, "TRACE",  counters.vmst_trace);

  lua_setfield(L, -2, "vmstate");

  return 1;
}

/* ----- misc.memprof module ---------------------------------------------- */

#define LJLIB_MODULE_misc_memprof
/* local started, err, errno = misc.memprof.start(fname) */
LJLIB_CF(misc_memprof_start)
{
  struct lj_memprof_options opt = {0};
  const char *fname = strdata(lj_lib_checkstr(L, 1));
  struct profile_ctx *ctx;
  int memprof_status;

  /*
  ** FIXME: more elegant solution with ctx.
  ** Throws in case of OOM.
  */
  ctx = lj_mem_new(L, sizeof(*ctx));
  opt.ctx = ctx;
  opt.buf = ctx->buf;
  opt.writer = buffer_writer_default;
  opt.on_stop = on_stop_cb_default;
  opt.len = STREAM_BUFFER_SIZE;

  ctx->g = G(L);
  ctx->stream = fopen(fname, "wb");

  if (ctx->stream == NULL) {
    lj_mem_free(ctx->g, ctx, sizeof(*ctx));
    return luaL_fileresult(L, 0, fname);
  }

  memprof_status = lj_memprof_start(L, &opt);

  if (LJ_UNLIKELY(memprof_status != PROFILE_SUCCESS)) {
    switch (memprof_status) {
    case PROFILE_ERRUSE:
      lua_pushnil(L);
      lua_pushstring(L, err2msg(LJ_ERR_PROF_MISUSE));
      lua_pushinteger(L, EINVAL);
      return 3;
#if LJ_HASMEMPROF
    case PROFILE_ERRRUN:
      lua_pushnil(L);
      lua_pushstring(L, err2msg(LJ_ERR_PROF_ISRUNNING));
      lua_pushinteger(L, EINVAL);
      return 3;
    case PROFILE_ERRIO:
      return luaL_fileresult(L, 0, fname);
#endif
    default:
      lua_assert(0);
      return 0;
    }
  }
  lua_pushboolean(L, 1);
  return 1;
}

/* local stopped, err, errno = misc.memprof.stop() */
LJLIB_CF(misc_memprof_stop)
{
  int status = lj_memprof_stop(L);
  if (status != PROFILE_SUCCESS) {
    switch (status) {
    case PROFILE_ERRUSE:
      lua_pushnil(L);
      lua_pushstring(L, err2msg(LJ_ERR_PROF_MISUSE));
      lua_pushinteger(L, EINVAL);
      return 3;
#if LJ_HASMEMPROF
    case PROFILE_ERRRUN:
      lua_pushnil(L);
      lua_pushstring(L, err2msg(LJ_ERR_PROF_NOTRUNNING));
      lua_pushinteger(L, EINVAL);
      return 3;
    case PROFILE_ERRIO:
      return luaL_fileresult(L, 0, NULL);
#endif
    default:
      lua_assert(0);
      return 0;
    }
  }
  lua_pushboolean(L, 1);
  return 1;
}

#include "lj_libdef.h"

/* ------------------------------------------------------------------------ */

LUALIB_API int luaopen_misc(struct lua_State *L)
{
  int status = SYSPROF_SUCCESS;
  struct luam_sysprof_config config = {};

  config.writer = buffer_writer_default;
  config.on_stop = on_stop_cb_default;
  status = luaM_sysprof_configure(&config);
  if (LJ_UNLIKELY(status != PROFILE_SUCCESS)) {
    return sysprof_error(L, status);
  }

  LJ_LIB_REG(L, LUAM_MISCLIBNAME, misc);
  LJ_LIB_REG(L, LUAM_MISCLIBNAME ".memprof", misc_memprof);
  LJ_LIB_REG(L, LUAM_MISCLIBNAME ".sysprof", misc_sysprof);

  return 1;
}
