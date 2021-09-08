local tap = require('tap')
local jutil = require('jit.util')
local bit = require('bit')
local vmdef = require('jit.vmdef')

local shr = bit.rshift
local band = bit.band
local sub = string.sub
local traceinfo = jutil.traceinfo
local traceir = jutil.traceir

local function check_ir(tr, funcnm)
  local info = traceinfo(tr)
  assert(info ~= nil)

  local nins = info.nins
  local irnames = vmdef.irnames

  for ins = 1, nins do
    local m, ot, _, op2, _ = traceir(tr, ins)
    local oidx = 6 * shr(ot, 8)
    local op = sub(irnames, oidx + 1, oidx + 6)
    if sub(op, 1, 5) == "CALLN" and
      band(m, 3 * 4) == 4 and
      vmdef.ircall[op2] == funcnm then return true end
  end
  return false
end

jit.off()
jit.flush()

local test = tap.test("gh-6163-jit-min-max")
test:plan(18)

-- `math.min`/`math.max` success with no args.
local pcall = pcall

jit.opt.start(0, 'hotloop=1')
jit.on()

local r, _ = pcall(function() math.min() end)
test:ok(false == r)
r, _ = pcall(function() math.min() end)
test:ok(false == r)

jit.off()
jit.flush()
jit.on()

-- `math.modf` shouldn't be compiled.
for _ = 1, 3 do math.modf(5.32) end

local tr1_info = jutil.traceinfo(1)
local tr2_info = jutil.traceinfo(2)

test:ok(tr1_info ~= nil)
test:ok(tr2_info ~= nil)
test:ok(tr1_info.link == 2)
test:ok(tr1_info.linktype == "stitch")

jit.off()
jit.flush()
jit.on()

-- All math functions should appear as `CALLN` in IRs.
for _=1,3 do
  math.sin(1)
  math.cos(1)
  math.tan(1)
  math.exp(1)
  math.log10(1)
  math.asin(0)
  math.acos(0)
  math.atan(1)
  math.atan2(2, 1)
  math.sinh(1)
  math.cosh(1)
  math.tanh(1)
end
jit.off()

test:ok(check_ir(1, "sin") == true)
test:ok(check_ir(1, "cos") == true)
test:ok(check_ir(1, "tan") == true)
test:ok(check_ir(1, "exp") == true)
test:ok(check_ir(1, "log10") == true)
test:ok(check_ir(1, "asin") == true)
test:ok(check_ir(1, "acos") == true)
test:ok(check_ir(1, "atan") == true)
test:ok(check_ir(1, "atan2") == true)
test:ok(check_ir(1, "sinh") == true)
test:ok(check_ir(1, "cosh") == true)
test:ok(check_ir(1, "tanh") == true)

os.exit(test:check() and 0 or 1)
