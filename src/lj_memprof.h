/*
** Memory profiler.
**
** Major portions taken verbatim or adapted from the LuaVela.
** Copyright (C) 2015-2019 IPONWEB Ltd.
*/

/*
** XXX: Memory profiler is not thread safe. Please, don't try to
** use it inside several VM, you can profile only one at a time.
*/

#ifndef _LJ_MEMPROF_H
#define _LJ_MEMPROF_H

#include "lj_def.h"
#include "lj_wbuf.h"


#define LJM_CURRENT_FORMAT_VERSION 0x01

/*
** Event stream format:
**
** stream         := symtab memprof
** symtab         := see symtab description
** memprof        := prologue event* epilogue
** prologue       := 'l' 'j' 'm' version reserved
** version        := <BYTE>
** reserved       := <BYTE> <BYTE> <BYTE>
** event          := event-alloc | event-realloc | event-free
** event-alloc    := event-header loc? naddr nsize
** event-realloc  := event-header loc? oaddr osize naddr nsize
** event-free     := event-header loc? oaddr osize
** event-header   := <BYTE>
** loc            := loc-lua | loc-c
** loc-lua        := sym-addr line-no
** loc-c          := sym-addr
** sym-addr       := <ULEB128>
** line-no        := <ULEB128>
** oaddr          := <ULEB128>
** naddr          := <ULEB128>
** osize          := <ULEB128>
** nsize          := <ULEB128>
** epilogue       := event-header
**
** <BYTE>   :  A single byte (no surprises here)
** <ULEB128>:  Unsigned integer represented in ULEB128 encoding
**
** (Order of bits below is hi -> lo)
**
** version: [VVVVVVVV]
**  * VVVVVVVV: Byte interpreted as a plain integer version number
**
** event-header: [FUUUSSEE]
**  * EE   : 2 bits for representing allocation event type (AEVENT_*)
**  * SS   : 2 bits for representing allocation source type (ASOURCE_*)
**  * UUU  : 3 unused bits
**  * F    : 0 for regular events, 1 for epilogue's *F*inal header
**           (if F is set to 1, all other bits are currently ignored)
*/

/* Allocation events. */
#define AEVENT_ALLOC   ((uint8_t)1)
#define AEVENT_FREE    ((uint8_t)2)
#define AEVENT_REALLOC ((uint8_t)(AEVENT_ALLOC | AEVENT_FREE))

/* Allocation sources. */
#define ASOURCE_INT   ((uint8_t)(1 << 2))
#define ASOURCE_LFUNC ((uint8_t)(2 << 2))
#define ASOURCE_CFUNC ((uint8_t)(3 << 2))

#define LJM_EPILOGUE_HEADER 0x80

/* Profiler options. */
struct lj_memprof_options {
  /* Context for the profile writer and final callback. */
  void *ctx;
  /* Custom buffer to write data. */
  uint8_t *buf;
  /* The buffer's size. */
  size_t len;
  /*
  ** Writer function for profile events.
  ** Should return amount of written bytes on success or zero in case of error.
  ** Setting *data to NULL means end of profiling.
  ** For details see <lj_wbuf.h>.
  */
  lj_wbuf_writer writer;
  /*
  ** Callback on profiler stopping. Required for correctly cleaning
  ** at VM finalization when profiler is still running.
  ** Returns zero on success.
  */
  int (*on_stop)(void *ctx, uint8_t *buf);
};

/* Avoid to provide additional interfaces described in other headers. */
struct lua_State;

/*
** Starts profiling. Returns PROFILE_SUCCESS on success and one of
** PROFILE_ERR* codes otherwise. Destructor is called in case of
** PROFILE_ERRIO.
*/
int lj_memprof_start(struct lua_State *L, const struct lj_memprof_options *opt);

/*
** Stops profiling. Returns PROFILE_SUCCESS on success and one of
** PROFILE_ERR* codes otherwise. If writer() function returns zero
** on call at buffer flush, profiled stream stops, or on_stop() callback
** returns non-zero value, returns PROFILE_ERRIO.
*/
int lj_memprof_stop(struct lua_State *L);

#endif
