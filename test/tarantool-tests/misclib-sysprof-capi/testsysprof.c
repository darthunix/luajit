#include <lua.h>
#include <luajit.h>
#include <lauxlib.h>

#include <lmisclib.h>

#include <stdlib.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>

#undef NDEBUG
#include <assert.h>


/* --- Utils -------------------------------------------------------------- */

#define STREAM_BUFFER_SIZE (512)

/* Structure given as ctx to memprof writer and on_stop callback. */
struct profile_ctx {
  /* Output file descriptor for data. */
  int fd;
  /* Buffer for data. */
  uint8_t buf[STREAM_BUFFER_SIZE];
};

/*
** Default buffer writer function.
** Just call write to the corresponding descriptor.
*/
static size_t writer(const void **buf_addr, size_t len, void *opt)
{
  struct profile_ctx *ctx = opt;
  const int fd = ctx->fd;
  const void * const buf_start = *buf_addr;
  const void *data = *buf_addr;
  size_t write_total = 0;

  assert(len <= STREAM_BUFFER_SIZE);

  for (;;) {
    const size_t written = write(fd, data, len - write_total);

    if (written == -1) {
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

/* Default on stop callback. Just close the corresponding descriptor. */
static int on_stop(void *opt, uint8_t *buf)
{
	struct profile_ctx *ctx = opt;
	const int fd = ctx->fd;
	free(ctx);
	return close(fd);
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
	struct luam_Sysprof_Config config = {};
	(void)config.mode;
	(void)config.interval;
	(void)config.writer;
	(void)config.buf;
	(void)config.buf_len;
	(void)config.on_stop;
	(void)config.backtracer;

	struct luam_Sysprof_Counters cnt = {};
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
	struct luam_Sysprof_Config config = {};
	int status = PROFILE_SUCCESS;

	config.interval = 11;

	/* Unconfigured. */
	status = luaM_sysprof_start(L, NULL);
	assert(PROFILE_ERRUSE == status);

	/* Unknown mode. */
	config.mode = 0x42;
	status = luaM_sysprof_init(&config);
	assert(PROFILE_ERRUSE == status);

	/* Buffer not configured. */
	config.mode = LUAM_SYSPROF_CALLGRAPH;
	config.buf = NULL;
	status = luaM_sysprof_init(&config);
	assert(PROFILE_ERRUSE == status);

	/* Check if profiling started. */
	config.mode = LUAM_SYSPROF_DEFAULT;
	status = luaM_sysprof_init(&config);
	assert(PROFILE_SUCCESS == status);
	status = luaM_sysprof_start(L, NULL);
	assert(PROFILE_SUCCESS == status);

	/* Already running. */
	status = luaM_sysprof_start(L, NULL);
	assert(PROFILE_ERRRUN == status);

	/* Profiler stopping. */
	status = luaM_sysprof_stop(L);
	assert(PROFILE_SUCCESS == status);

	/* Stopping profiler which is not running. */
	status = luaM_sysprof_stop(L);
	assert(PROFILE_ERRRUN == status);

	lua_pushboolean(L, 1);
	return 1;
}

static int profile_func(lua_State *L)
{
	struct luam_Sysprof_Config config = {};
	struct luam_Sysprof_Counters cnt = {};
	int status = PROFILE_SUCCESS;

	int n = lua_gettop(L);
	if (n != 1 || !lua_isfunction(L, 1)) {
		luaL_error(L, "incorrect argument: 1 function is required");
	}

	/*
	** Since all the other modes functionality is the
	** subset of CALLGRAPH mode, run this mode to test
	** the profiler's behavior.
	*/
	config.mode = LUAM_SYSPROF_CALLGRAPH;
	config.interval = 11;

	struct profile_ctx *ctx = calloc(1, sizeof(*ctx));
	assert(ctx != NULL);
	ctx->fd = open("/dev/null", O_WRONLY);
	assert(ctx->fd != -1);

	config.writer = writer;
	config.on_stop = on_stop;
	config.buf = ctx->buf;
	config.buf_len = STREAM_BUFFER_SIZE;

	status = luaM_sysprof_init(&config);
	assert(PROFILE_SUCCESS == status);

	status = luaM_sysprof_start(L, ctx);
	assert(PROFILE_SUCCESS == status);

	/* Run payload. */
	if (lua_pcall(L, 0, 0, 0) != 0)
		luaL_error(L, "error running payload: %s", lua_tostring(L, -1));

	status = luaM_sysprof_stop(L);
	assert(PROFILE_SUCCESS == status);

	status = luaM_sysprof_report(&cnt);
	assert(PROFILE_SUCCESS == status);

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
	{"base", base},
	{"c_payload", c_payload},
	{"profile_func", profile_func},
	{"validation", validation},
	{NULL, NULL}
};

LUA_API int luaopen_testsysprof(lua_State *L)
{
	luaL_register(L, "testsysprof", testsysprof);
	return 1;
}
