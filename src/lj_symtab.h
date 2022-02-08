/*
** Symbol table for profilers.
**
** Major portions taken verbatim or adapted from the LuaVela.
** Copyright (C) 2015-2019 IPONWEB Ltd.
*/
#define _GNU_SOURCE
#include <link.h>

#ifndef LJ_SYMTAB_H
#define LJ_SYMTAB_H

#include "lj_wbuf.h"
#include "lj_obj.h"

#define LJS_CURRENT_VERSION 0x2

/*
** symtab format:
**
** symtab         := prologue sym*
** prologue       := 'l' 'j' 's' version reserved
** version        := <BYTE>
** reserved       := <BYTE> <BYTE> <BYTE>
** sym            := sym-lua | sym-cfunc | sym-final
** sym-lua        := sym-header sym-addr sym-chunk sym-line
** sym-header     := <BYTE>
** sym-addr       := <ULEB128>
** sym-chunk      := string
** sym-line       := <ULEB128>
** sym-cfunc      := sym-header sym-addr sym-name
** sym-name       := string
** sym-final      := sym-header
** string         := string-len string-payload
** string-len     := <ULEB128>
** string-payload := <BYTE> {string-len}
**
** <BYTE>   :  A single byte (no surprises here)
** <ULEB128>:  Unsigned integer represented in ULEB128 encoding
**
** (Order of bits below is hi -> lo)
**
** version: [VVVVVVVV]
**  * VVVVVVVV: Byte interpreted as a plain numeric version number
**
** sym-header: [FUUUUUTT]
**  * TT    : 2 bits for representing symbol type
**  * UUUUU : 5 unused bits
**  * F     : 1 bit marking the end of the symtab (final symbol)
*/

#define SYMTAB_LFUNC ((uint8_t)0)
#define SYMTAB_CFUNC ((uint8_t)1)
#define SYMTAB_FINAL ((uint8_t)0x80)

struct symbol_resolver_conf {
  struct lj_wbuf *buf;
  const uint8_t header;

  uint32_t cur_lib;
  uint32_t lib_cnt_prev;
  uint32_t to_dump_cnt;
  uint32_t *lib_cnt;
};

int resolve_symbolnames(struct dl_phdr_info *info, size_t info_size, void *data);

/*
** Dumps symbol table for Lua functions into a buffer
*/
void lj_symtab_dump(struct lj_wbuf *out, const struct global_State *g, uint32_t *lib_cnt);

#endif
