#include <lua.h>
#include <luajit.h>
#include <lauxlib.h>

#include <lmisclib.h>

#include <stdlib.h>
#include <errno.h>

#undef NDEBUG
#include <assert.h>

/* --- utils -------------------------------------------------------------- */

#define SYSPROF_INTERVAL_DEFAULT 11

/*
** Yep, 8Mb. Tuned in order not to bother the platform with too often flushes.
*/
#define STREAM_BUFFER_SIZE (8 * 1024 * 1024)

/* Structure given as ctx to memprof writer and on_stop callback. */
struct sysprof_ctx {
  /* Output file stream for data. */
  FILE *stream;
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
  struct sysprof_ctx *ctx = opt;
  FILE *stream = ctx->stream;
  const void * const buf_start = *buf_addr;
  const void *data = *buf_addr;
  size_t write_total = 0;

  assert(len <= STREAM_BUFFER_SIZE);

  for (;;) {
    const size_t written = fwrite(data, 1, len - write_total, stream);

    if (written == 0) {
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
    assert(write_total <= len);

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
  struct sysprof_ctx *ctx = opt;
  FILE *stream = ctx->stream;
  free(ctx);
  return fclose(stream);
}

static int stream_init(struct luam_sysprof_options *opt)
{
  struct sysprof_ctx *ctx = calloc(1, sizeof(struct sysprof_ctx));
  if (NULL == ctx) {
    return SYSPROF_ERRIO;
  }

  ctx->stream = fopen("/dev/null", "wb");
  if (NULL == ctx->stream) {
    free(ctx);
    return SYSPROF_ERRIO;
  }

  opt->ctx = ctx;
  opt->buf = ctx->buf;
  opt->len = STREAM_BUFFER_SIZE;

  return SYSPROF_SUCCESS;
}

/* --- Payload ------------------------------------------------------------ */

static double fib(double n)
{
  if (n <= 1) {
    return n;
  }
  return fib(n - 1) + fib(n - 2);
}

static int c_payload(lua_State *L)
{
  fib(luaL_checknumber(L, 1));
  return 0;
}

/* --- sysprof C API tests ------------------------------------------------ */

static int base(lua_State *L)
{
  struct luam_sysprof_config config = {};
  (void)config.writer;
  (void)config.on_stop;
  (void)config.backtracer;

  struct luam_sysprof_options opt = {};
  (void)opt.interval;
  (void)opt.mode;
  (void)opt.ctx;
  (void)opt.buf;
  (void)opt.len;

  struct luam_sysprof_counters cnt = {};
  luaM_sysprof_report(&cnt);

  (void)cnt.samples;
  (void)cnt.overruns;
  (void)cnt.vmst_interp;
  (void)cnt.vmst_lfunc;
  (void)cnt.vmst_ffunc;
  (void)cnt.vmst_cfunc;
  (void)cnt.vmst_gc;
  (void)cnt.vmst_exit;
  (void)cnt.vmst_record;
  (void)cnt.vmst_opt;
  (void)cnt.vmst_asm;
  (void)cnt.vmst_trace;

  lua_pushboolean(L, 1);
  return 1;
}

static int validation(lua_State *L)
{
  struct luam_sysprof_config config = {};
  struct luam_sysprof_options opt = {};
  int status = SYSPROF_SUCCESS;

  status = luaM_sysprof_configure(&config);
  assert(SYSPROF_SUCCESS == status);

  /* Unknown mode */
  opt.mode = 0x40;
  status = luaM_sysprof_start(L, &opt);
  assert(SYSPROF_ERRUSE == status);

  /* Buffer not configured */
  opt.mode = LUAM_SYSPROF_CALLGRAPH;
  opt.buf = NULL;
  status = luaM_sysprof_start(L, &opt);
  assert(SYSPROF_ERRUSE == status);

  /* Bad interval */
  opt.mode = LUAM_SYSPROF_DEFAULT;
  opt.interval = 0;
  status = luaM_sysprof_start(L, &opt);
  assert(SYSPROF_ERRUSE == status);

  /* Check if profiling started */
  opt.mode = LUAM_SYSPROF_DEFAULT;
  opt.interval = SYSPROF_INTERVAL_DEFAULT;
  status = luaM_sysprof_start(L, &opt);
  assert(SYSPROF_SUCCESS == status);

  /* Already running */
  status = luaM_sysprof_start(L, &opt);
  assert(SYSPROF_ERRRUN == status);

  /* Profiler stopping */
  status = luaM_sysprof_stop(L);
  assert(SYSPROF_SUCCESS == status);

  /* Stopping profiler which is not running */
  status = luaM_sysprof_stop(L);
  assert(SYSPROF_ERRSTOP == status);

  lua_pushboolean(L, 1);
  return 1;
}

static int profile_func(lua_State *L)
{
  struct luam_sysprof_config config = {};
  struct luam_sysprof_options opt = {};
  struct luam_sysprof_counters cnt = {};
  int status = SYSPROF_ERRUSE;

  int n = lua_gettop(L);
  if (n != 1 || !lua_isfunction(L, 1)) {
    luaL_error(L, "incorrect argument: 1 function is required");
  }

  /*
  ** Since all the other modes functionality is the
  ** subset of CALLGRAPH mode, run this mode to test
  ** the profiler's behavior.
  */
  opt.mode = LUAM_SYSPROF_CALLGRAPH;
  opt.interval = SYSPROF_INTERVAL_DEFAULT;
  stream_init(&opt);

  config.on_stop = on_stop_cb_default;
  config.writer = buffer_writer_default;
  status = luaM_sysprof_configure(&config);
  assert(SYSPROF_SUCCESS == status);

  status = luaM_sysprof_start(L, &opt);
  assert(SYSPROF_SUCCESS == status);

  /* Run payload. */
  if (lua_pcall(L, 0, 0, 0) != 0) {
    luaL_error(L, "error running payload: %s", lua_tostring(L, -1));
  }

  status = luaM_sysprof_stop(L);
  assert(SYSPROF_SUCCESS == status);

  status = luaM_sysprof_report(&cnt);
  assert(SYSPROF_SUCCESS == status);

  assert(cnt.samples > 1);
  assert(cnt.samples == cnt.vmst_asm +
                        cnt.vmst_cfunc +
                        cnt.vmst_exit +
                        cnt.vmst_ffunc +
                        cnt.vmst_gc +
                        cnt.vmst_interp +
                        cnt.vmst_lfunc +
                        cnt.vmst_opt +
                        cnt.vmst_record +
                        cnt.vmst_trace);

  lua_pushboolean(L, 1);
  return 1;
}

static const struct luaL_Reg testsysprof[] = {
  {"c_payload", c_payload},
  {"base", base},
  {"validation", validation},
  {"profile_func", profile_func},
  {NULL, NULL}
};

LUA_API int luaopen_testsysprof(lua_State *L)
{
  luaL_register(L, "testsysprof", testsysprof);
  return 1;
}
