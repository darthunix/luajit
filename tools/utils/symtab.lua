-- Parser of LuaJIT's symtab binary stream.
-- The format spec can be found in <src/lj_memprof.h>.
--
-- Major portions taken verbatim or adapted from the LuaVela.
-- Copyright (C) 2015-2019 IPONWEB Ltd.

local bit = require "bit"

local avl = require "utils.avl"

local band = bit.band
local string_format = string.format

local LJS_MAGIC = "ljs"
local LJS_CURRENT_VERSION = 2
local LJS_EPILOGUE_HEADER = 0x80
local LJS_SYMTYPE_MASK = 0x03

local SYMTAB_LFUNC = 0
local SYMTAB_CFUNC = 1

local M = {}

-- Parse a single entry in a symtab: lfunc symbol.
local function parse_sym_lfunc(reader, symtab)
  local sym_addr = reader:read_uleb128()
  local sym_chunk = reader:read_string()
  local sym_line = reader:read_uleb128()

  symtab.SYMTAB_LFUNC[sym_addr] = {
    source = sym_chunk,
    linedefined = sym_line,
  }
end

-- Parse a single entry in a symtab: .so library
local function parse_sym_cfunc(reader, symtab)
  local addr = reader:read_uleb128()
  local name = reader:read_string()

  symtab.SYMTAB_CFUNC = avl.insert(symtab.SYMTAB_CFUNC, addr, {
    name = name
  })
end

local parsers = {
  [SYMTAB_LFUNC] = parse_sym_lfunc,
  [SYMTAB_CFUNC] = parse_sym_cfunc
}

function M.parse(reader)
  local symtab = {
    SYMTAB_LFUNC = {},
    SYMTAB_CFUNC = nil
  }

  local magic = reader:read_octets(3)
  local version = reader:read_octets(1)

  -- Dummy-consume reserved bytes.
  local _ = reader:read_octets(3)

  if magic ~= LJS_MAGIC then
    error("Bad LJS format prologue: "..magic)
  end

  if string.byte(version) ~= LJS_CURRENT_VERSION then
    error(string_format(
         "LJS format version mismatch:"..
         "the tool expects %d, but your data is %d",
         LJS_CURRENT_VERSION,
         string.byte(version)
    ))

  end

  while not reader:eof() do
    local header = reader:read_octet()
    local is_final = band(header, LJS_EPILOGUE_HEADER) ~= 0

    if is_final then
      break
    end

    local sym_type = band(header, LJS_SYMTYPE_MASK)
    if parsers[sym_type] then
      parsers[sym_type](reader, symtab)
    end
  end
  return symtab
end

function M.demangle(symtab, loc)
  local addr = loc.addr

  if addr == 0 then
    return "INTERNAL"
  end

  if symtab.SYMTAB_LFUNC[addr] then
    return string_format("%s:%d", symtab.SYMTAB_LFUNC[addr].source, loc.line)
  end

  local key, value = avl.upper_bound(symtab.SYMTAB_CFUNC, addr)

  if key then
    return string_format("%s:%#x", value.name, key)
  end

  return string_format("CFUNC %#x", addr)
end

function M.add_cfunc(symtab, addr, name)
  symtab.SYMTAB_CFUNC = avl.insert(symtab.SYMTAB_CFUNC, addr, {name = name})
end

return M
