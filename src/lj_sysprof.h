/*
** Sysprof - platform and Lua profiler.
*/

/*
** XXX: Platform profiler is not thread safe. Please, don't try to
** use it inside several VM, you can profile only one at a time.
*/

/*
** XXX: Platform profiler uses the same signal backend as lj_profile. Please,
** don't use both at the same time.
*/

#ifndef _LJ_SYSPROF_H
#define _LJ_SYSPROF_H

#include "lj_def.h"
#include "lj_obj.h"
#include "lmisclib.h"

#define LJP_FORMAT_VERSION 0x1

/*
** Event stream format:
**
** stream          := symtab sysprof
** symtab          := see symtab description
** sysprof         := prologue sample* epilogue
** prologue        := 'l' 'j' 'p' version reserved
** version         := <BYTE>
** reserved        := <BYTE> <BYTE> <BYTE>
** sample          := sample-guest | sample-host | sample-trace
** sample-guest    := sample-header stack-lua stack-host
** sample-host     := sample-header stack-host
** sample-trace    := sample-header traceno sym-addr line-no
** sample-header   := <BYTE>
** stack-lua       := frame-lua* frame-lua-last
** stack-host      := frame-host* frame-host-last
** frame-lua       := frame-lfunc | frame-cfunc | frame-ffunc
** frame-lfunc     := frame-header sym-addr line-no
** frame-cfunc     := frame-header exec-addr
** frame-ffunc     := frame-header ffid
** frame-lua-last  := frame-header
** frame-header    := <BYTE>
** frame-host      := exec-addr
** frame-host-last := <ULEB128>
** line-no         := <ULEB128>
** traceno         := <ULEB128>
** ffid            := <ULEB128>
** sym-addr        := <ULEB128>
** exec-addr       := <ULEB128>
** epilogue        := sample-header
**
** <BYTE>   :  A single byte (no surprises here)
** <ULEB128>:  Unsigned integer represented in ULEB128 encoding
**
** (Order of bits below is hi -> lo)
**
** version: [VVVVVVVV]
**  * VVVVVVVV: Byte interpreted as a plain integer version number
**
** sample-header: [FUUUUEEE]
**  * EEEE : 4 bits for representing vmstate (LJ_VMST_*)
**  * UUU  : 3 unused bits
**  * F    : 0 for regular samples, 1 for epilogue's Final header
**           (if F is set to 1, all other bits are currently ignored)
**
** frame-header: [FUUUUUEE]
**  * EE    : 2 bits for representing frame type (FRAME_*)
**  * UUUUU : 5 unused bits
**  * F     : 0 for regular frames, 1 for final frame
**            (if F is set to 1, all other bits are currently ignored)
**
** frame-host-last = NULL
*/

#define LJP_VMSTATE_MASK ((1 << 4) - 1)

#define LJP_FRAME_LFUNC ((uint8_t)1)
#define LJP_FRAME_CFUNC ((uint8_t)2)
#define LJP_FRAME_FFUNC ((uint8_t)3)
#define LJP_FRAME_LUA_LAST  0x80
#define LJP_FRAME_HOST_LAST NULL

#define LJP_EPILOGUE_BYTE 0x80


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

// TODO
LJ_STATIC_ASSERT(offsetof(struct luam_Sysprof_Counters, vmst_interp) == 0);

#define LUAM_SYSPROF_INTERVAL_DEFAULT 11


int lj_sysprof_init(const struct luam_Sysprof_Config *config);

int lj_sysprof_start(lua_State *L, void *ctx);

int lj_sysprof_stop(lua_State *L);

int lj_sysprof_report(struct luam_Sysprof_Counters *counters);

#endif

