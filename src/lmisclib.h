/*
** Miscellaneous public C API extensions.
**
** Major portions taken verbatim or adapted from the LuaVela.
** Copyright (C) 2015-2019 IPONWEB Ltd.
*/

#ifndef _LMISCLIB_H
#define _LMISCLIB_H

#include "lua.h"

/* API for obtaining various platform metrics. */

struct luam_Metrics {
  /*
  ** Number of strings being interned (i.e. the string with the
  ** same payload is found, so a new one is not created/allocated).
  */
  size_t strhash_hit;
  /* Total number of strings allocations during the platform lifetime. */
  size_t strhash_miss;

  /* Amount of allocated string objects. */
  size_t gc_strnum;
  /* Amount of allocated table objects. */
  size_t gc_tabnum;
  /* Amount of allocated udata objects. */
  size_t gc_udatanum;
  /* Amount of allocated cdata objects. */
  size_t gc_cdatanum;

  /* Memory currently allocated. */
  size_t gc_total;
  /* Total amount of freed memory. */
  size_t gc_freed;
  /* Total amount of allocated memory. */
  size_t gc_allocated;

  /* Count of incremental GC steps per state. */
  size_t gc_steps_pause;
  size_t gc_steps_propagate;
  size_t gc_steps_atomic;
  size_t gc_steps_sweepstring;
  size_t gc_steps_sweep;
  size_t gc_steps_finalize;

  /*
  ** Overall number of snap restores (amount of guard assertions
  ** leading to stopping trace executions).
  */
  size_t jit_snap_restore;
  /* Overall number of abort traces. */
  size_t jit_trace_abort;
  /* Total size of all allocated machine code areas. */
  size_t jit_mcode_size;
  /* Amount of JIT traces. */
  unsigned int jit_trace_num;
};

LUAMISC_API void luaM_metrics(lua_State *L, struct luam_Metrics *metrics);

/* API for controlling profilers. */

#define PROFILE_SUCCESS 0
#define PROFILE_ERRUSE  1
#define PROFILE_ERRRUN  2
#define PROFILE_ERRMEM  3
#define PROFILE_ERRIO   4


enum luam_Sysprof_Mode {
  /*
  ** DEFAULT mode collects only data for luam_sysprof_counters, which is stored
  ** in memory and can be collected with luaM_sysprof_report after profiler
  ** stops.
  */
  LUAM_SYSPROF_DEFAULT,
  /*
  ** LEAF mode = DEFAULT + streams samples with only top frames of host and
  ** guests stacks in format described in <lj_sysprof.h>
  */
  LUAM_SYSPROF_LEAF,
  /*
  ** CALLGRAPH mode = DEFAULT + streams samples with full callchains of host
  ** and guest stacks in format described in <lj_sysprof.h>
  */
  LUAM_SYSPROF_CALLGRAPH
};

struct luam_Sysprof_Config {
  /* Profiling mode. */
  enum luam_Sysprof_Mode mode;
  /*
  ** Sampling interval in msec. If equals to 0, will be force set
  ** to the SYSPROF_DEFAULT_INTERVAL.
  */
  uint64_t interval;
  /*
  ** Writer function for profile events. Must be POSIX AS-safe.
  ** Should return amount of written bytes on success or zero in case of error.
  ** Setting *data to NULL means end of profiling.
  ** For details see <lj_wbuf.h>.
  */
  size_t (*writer)(const void **data, size_t len, void *ctx);
  /* Custom buffer to write data. */
  uint8_t *buf;
  /* The buffer's size. */
  size_t buf_len;
  /*
  ** Callback on profiler stopping. Required for correctly cleaning
  ** at VM finalization when profiler is still running.
  ** Returns zero on success.
  */
  int (*on_stop)(void *ctx, uint8_t *buf);
  /*
  ** Backtracing function for the host stack. Must be POSIX AS-safe.
  ** Should fill the address buffer with frame pointers starting from the top.
  ** `max_depth` shows the maximum number of frames which should be dumped.
  ** Returns the number of written frames.
  **
  ** If NULL, the default GNU <backtrace> will be used as a backtracer.
  ** Caveat: the default GNU <backtrace> is AS-safe only in case if the GCC
  ** library is preloaded.
  */
  int (*backtracer)(void *addr_buf[], size_t max_depth);
};

struct luam_Sysprof_Counters {
  uint64_t vmst_interp;
  uint64_t vmst_lfunc;
  uint64_t vmst_ffunc;
  uint64_t vmst_cfunc;
  uint64_t vmst_gc;
  uint64_t vmst_exit;
  uint64_t vmst_record;
  uint64_t vmst_opt;
  uint64_t vmst_asm;
  uint64_t vmst_trace;
  /*
  ** XXX: order of vmst counters must match the order
  ** of vmstates in <lj_obj.h>.
  */
  uint64_t samples;
  uint64_t overruns;
};

#define LUAM_SYSPROF_INTERVAL_DEFAULT 11


LUAMISC_API int luaM_sysprof_init(const struct luam_Sysprof_Config *config);

LUAMISC_API int luaM_sysprof_start(lua_State *L, void *ctx);

LUAMISC_API int luaM_sysprof_stop(lua_State *L);

LUAMISC_API int luaM_sysprof_report(struct luam_Sysprof_Counters *counters);


#define LUAM_MISCLIBNAME "misc"
LUALIB_API int luaopen_misc(lua_State *L);

#endif /* _LMISCLIB_H */
