/*
** Implementation of symbol table for profilers.
**
** Major portions taken verbatim or adapted from the LuaVela.
** Copyright (C) 2015-2019 IPONWEB Ltd.
*/

#define _GNU_SOURCE
#include <link.h>

#define lj_symtab_c
#define LUA_CORE

#include "lj_symtab.h"

#include <stdio.h>
#include <stdlib.h>
#include <link.h>


static const unsigned char ljs_header[] = {'l', 'j', 's', LJS_CURRENT_VERSION,
                                          0x0, 0x0, 0x0};
struct ghashtab_header {
    uint32_t nbuckets;
    uint32_t symoffset;
    uint32_t bloom_size;
    uint32_t bloom_shift;
};

uint32_t ghashtab_size(ElfW(Addr) ghashtab)
{
    /*
     * There is no easy way to get count of symbols in GNU hashtable, so the
     * only way to do this is to iterate over it.
     */
    uint32_t last_entry = 0;
    uint32_t *cur_bucket = NULL;
    uint32_t *entry = NULL;

    const void *chain_address = NULL;
    struct ghashtab_header *header = (struct ghashtab_header*)ghashtab;
    const void *buckets = (void*)ghashtab + sizeof(struct ghashtab_header) +
                          sizeof(uint64_t) * header->bloom_size;

    cur_bucket = (uint32_t*)buckets;
    for (uint32_t i = 0; i < header->nbuckets; ++i) {
        if (last_entry < *cur_bucket)
            last_entry = *cur_bucket;
        cur_bucket++;
    }

    if (last_entry < header->symoffset)
        return header->symoffset;

    chain_address = buckets + sizeof(uint32_t) * header->nbuckets;
    do {
        entry = (uint32_t*)(chain_address + (last_entry - header->symoffset) *
                sizeof(uint32_t));
        last_entry++;
    } while (!(*entry & 1));

    return last_entry;
}


int resolve_symbolnames(struct dl_phdr_info *info, size_t info_size, void *data)
{
  if (strcmp(info->dlpi_name, "linux-vdso.so.1") == 0) {
    return 0;
  }

  ElfW(Dyn*) dyn = NULL;
  ElfW(Sym*) sym = NULL;
  ElfW(Word*) hashtab = NULL;
  ElfW(Word) sym_cnt = 0;

  char *strtab = 0;
  char *sym_name = 0;

  struct symbol_resolver_conf *conf = data;
  const uint8_t header = conf->header;
  struct lj_wbuf *buf = conf->buf;

  conf->lib_cnt_prev = *conf->lib_cnt;
  uint32_t lib_cnt_prev = conf->lib_cnt_prev;

  if ((conf->to_dump_cnt = info->dlpi_adds - lib_cnt_prev) == 0)
    /* No new libraries, stop resolver. */
    return 1;

  uint32_t lib_cnt = info->dlpi_adds - info->dlpi_subs;
  if (conf->cur_lib < lib_cnt - conf->to_dump_cnt) {
    /* That lib is already dumped, skip it. */
    ++conf->cur_lib;
    return 0;
  }

  for (size_t header_index = 0; header_index < info->dlpi_phnum; ++header_index) {
    if (info->dlpi_phdr[header_index].p_type == PT_DYNAMIC) {
      dyn = (ElfW(Dyn)*)(info->dlpi_addr +  info->dlpi_phdr[header_index].p_vaddr);

      while(dyn->d_tag != DT_NULL) {
        if (dyn->d_tag == DT_HASH) {
          hashtab = (ElfW(Word*))dyn->d_un.d_ptr;
          sym_cnt = hashtab[1];
        }
        else if (dyn->d_tag == DT_GNU_HASH && sym_cnt == 0)
          sym_cnt = ghashtab_size(dyn->d_un.d_ptr);
        else if (dyn->d_tag == DT_STRTAB)
          strtab = (char*)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_SYMTAB) {
          sym = (ElfW(Sym*))dyn->d_un.d_ptr;

          for (ElfW(Word) sym_index = 0; sym_index < sym_cnt; sym_index++) {
              sym_name = &strtab[sym[sym_index].st_name];
              lj_wbuf_addbyte(buf, header);
              lj_wbuf_addu64(buf, sym[sym_index].st_value + info->dlpi_addr);
              lj_wbuf_addstring(buf, sym_name);
          }
        }
        dyn++;
      }
    }
  }

  ++conf->cur_lib;
  return 0;
}

void lj_symtab_dump(struct lj_wbuf *out, const struct global_State *g, uint32_t *lib_cnt)
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
    default:
      break;
    }
    iter = &o->gch.nextgc;
  }
  /* Write symbols. */
  struct symbol_resolver_conf conf = {
    out,
    SYMTAB_CFUNC,
    0,
    *lib_cnt,
    0,
    lib_cnt
  };

  dl_iterate_phdr(resolve_symbolnames, &conf);

  lj_wbuf_addbyte(out, SYMTAB_FINAL);
}
