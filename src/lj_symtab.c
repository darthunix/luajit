/*
** Implementation of symbol table for profilers.
**
** Major portions taken verbatim or adapted from the LuaVela.
** Copyright (C) 2015-2019 IPONWEB Ltd.
*/

#define lj_symtab_c
#define LUA_CORE

#include "lj_symtab.h"

static const unsigned char ljs_header[] = {'l', 'j', 's', LJS_CURRENT_VERSION,
                                          0x0, 0x0, 0x0};

#if LJ_HASJIT

static void dump_symtab_trace(struct lj_wbuf *out, const GCtrace *trace)
{
  GCproto *pt = &gcref(trace->startpt)->pt;
  BCLine lineno = 0;

  const BCIns *startpc = mref(trace->startpc, const BCIns);
  lua_assert(startpc >= proto_bc(pt) &&
             startpc < proto_bc(pt) + pt->sizebc);

  lineno = lj_debug_line(pt, proto_bcpos(pt, startpc));

  lj_wbuf_addbyte(out, SYMTAB_TRACE);
  lj_wbuf_addu64(out, (uint64_t)trace->traceno);
  lj_wbuf_addu64(out, (uint64_t)trace->mcode);
  /*
  ** The information about the prototype, associated with the
  ** trace's start has already been dumped, as it is anchored
  ** via the trace and is not collected while the trace is alive.
  ** For this reason, we do not need to repeat dumping the chunk
  ** name for the prototype.
  */
  lj_wbuf_addu64(out, (uintptr_t)pt);
  lj_wbuf_addu64(out, (uint64_t)lineno);
}

#else

static void dump_symtab_trace(struct lj_wbuf *out, const GCtrace *trace)
{
  UNUSED(out);
  UNUSED(trace);
  lua_assert(0);
}

#endif

void lj_symtab_dump(struct lj_wbuf *out, const struct global_State *g)
{
  const GCRef *iter = &g->gc.root;
  const GCobj *o;
  const size_t ljs_header_len = sizeof(ljs_header) / sizeof(ljs_header[0]);

  /* Write prologue. */
  lj_wbuf_addn(out, ljs_header, ljs_header_len);

  while ((o = gcref(*iter)) != NULL) {
    switch (o->gch.gct) {
    case (~LJ_TPROTO): {
      const GCproto *pt = gco2pt(o);
      lj_wbuf_addbyte(out, SYMTAB_LFUNC);
      lj_wbuf_addu64(out, (uintptr_t)pt);
      lj_wbuf_addstring(out, proto_chunknamestr(pt));
      lj_wbuf_addu64(out, (uint64_t)pt->firstline);
      break;
    }
    case (~LJ_TTRACE): {
      dump_symtab_trace(out, gco2trace(o));
      break;
    }
    default:
      break;
    }
    iter = &o->gch.nextgc;
  }

  lj_wbuf_addbyte(out, SYMTAB_FINAL);
}
