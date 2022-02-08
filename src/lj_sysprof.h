/*
** Sysprof - platform and Lua profiler
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
**  * EEE  : 3 bits for representing vmstate (LJ_VMST_*)
**  * UUUU : 4 unused bits
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

#define LJP_FRAME_SYMTAB ((uint8_t)0)
#define LJP_FRAME_LFUNC ((uint8_t)1)
#define LJP_FRAME_CFUNC ((uint8_t)2)
#define LJP_FRAME_FFUNC ((uint8_t)3)
#define LJP_FRAME_LUA_LAST  0x80
#define LJP_FRAME_HOST_LAST NULL

#define LJP_EPILOGUE_BYTE 0x80

#define LJP_STATE_SYMTAB ((uint8_t)10)

int lj_sysprof_configure(const struct luam_sysprof_config *config);

int lj_sysprof_start(lua_State *L, const struct luam_sysprof_options *opt);

int lj_sysprof_stop(lua_State *L);

int lj_sysprof_report(struct luam_sysprof_counters *counters);

#endif

