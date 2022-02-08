-- Parser of LuaJIT's sysprof binary stream.
-- The format spec can be found in <src/lj_sysprof.h>.

local string_format = string.format

local LJP_MAGIC = "ljp"
local LJP_CURRENT_VERSION = 1

local M = {}

M.VMST = {
  INTERP = 0,
  LFUNC  = 1,
  FFUNC  = 2,
  CFUNC  = 3,
  GC     = 4,
  EXIT   = 5,
  RECORD = 6,
  OPT    = 7,
  ASM    = 8,
  TRACE  = 9,
  SYMTAB = 10,
}


M.FRAME = {
  LFUNC  = 1,
  CFUNC  = 2,
  FFUNC  = 3,
  BOTTOM = 0x80
}

local STREAM_END = 0x80

local function new_event()
  return {
    lua = {
      vmstate = 0,
      callchain = {},
      trace = {
        id = nil,
        addr = 0,
        line = 0
      }
    },
    host = {
      callchain = {}
    },
    symtab = nil
  }
end

local function parse_lfunc(reader, event)
  local addr = reader:read_uleb128()
  local line = reader:read_uleb128()
  table.insert(event.lua.callchain, {
    type = M.FRAME.LFUNC,
    addr = addr,
    line = line
  })
end

local function parse_ffunc(reader, event)
  local ffid = reader:read_uleb128()
  table.insert(event.lua.callchain, {
    type = M.FRAME.FFUNC,
    ffid = ffid,
  })
end

local function parse_cfunc(reader, event)
  local addr = reader:read_uleb128()
  table.insert(event.lua.callchain, {
    type = M.FRAME.CFUNC,
    addr = addr
  })
end

local frame_parsers = {
  [M.FRAME.LFUNC] = parse_lfunc,
  [M.FRAME.FFUNC] = parse_ffunc,
  [M.FRAME.CFUNC] = parse_cfunc
}

local function parse_lua_callchain(reader, event)
  while true do
    local frame_header = reader:read_octet()
    if frame_header == M.FRAME.BOTTOM then
      break
    end
    frame_parsers[frame_header](reader, event)
  end
end

--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~--

local function parse_host_callchain(reader, event)
  local addr = reader:read_uleb128()

  while addr ~= 0 do
    table.insert(event.host.callchain, {
      addr = addr
    })
    addr = reader:read_uleb128()
  end
end

--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~--

local function parse_trace_callchain(reader, event)
  event.lua.trace.id   = reader:read_uleb128()
  event.lua.trace.addr = reader:read_uleb128()
  event.lua.trace.line = reader:read_uleb128()
end

--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~--

local function parse_host_only(reader, event)
  parse_host_callchain(reader, event)
end

local function parse_lua_host(reader, event)
  parse_lua_callchain(reader, event)
  parse_host_callchain(reader, event)
end

local function parse_trace(reader, event)
  parse_trace_callchain(reader, event)
  -- parse_lua_callchain(reader, event)
end

local function parse_symtab(reader, event)
  local addr = reader:read_uleb128()
  local name = reader:read_string()

  event.symtab = {
    name = name,
    addr = addr,
  }
end

local event_parsers = {
  [M.VMST.INTERP] = parse_host_only,
  [M.VMST.LFUNC]  = parse_lua_host,
  [M.VMST.FFUNC]  = parse_lua_host,
  [M.VMST.CFUNC]  = parse_lua_host,
  [M.VMST.GC]     = parse_host_only,
  [M.VMST.EXIT]   = parse_host_only,
  [M.VMST.RECORD] = parse_host_only,
  [M.VMST.OPT]    = parse_host_only,
  [M.VMST.ASM]    = parse_host_only,
  [M.VMST.TRACE]  = parse_trace,
  [M.VMST.SYMTAB] = parse_symtab,
}

local function parse_event(reader, events)
  local event = new_event()

  local vmstate = reader:read_octet()
  if vmstate == STREAM_END then
    -- TODO: samples & overruns
    return false
  end

  assert(0 <= vmstate and vmstate <= 10, "Vmstate "..vmstate.." is not valid")
  event.lua.vmstate = vmstate

  event_parsers[vmstate](reader, event)

  table.insert(events, event)
  return true
end

function M.parse(reader)
  local events = {}

  local magic = reader:read_octets(3)
  local version = reader:read_octets(1)
  -- Dummy-consume reserved bytes.
  local _ = reader:read_octets(3)

  if magic ~= LJP_MAGIC then
    error("Bad LJP format prologue: "..magic)
  end

  if string.byte(version) ~= LJP_CURRENT_VERSION then
    error(string_format(
      "LJP format version mismatch: the tool expects %d, but your data is %d",
      LJP_CURRENT_VERSION,
      string.byte(version)
    ))
  end

  while parse_event(reader, events) do
    -- Empty body.
  end

  return events
end

return M
