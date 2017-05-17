local tap = require('tap')

local test = tap.test('fix-slot-check-for-mm-record')
test:plan(2)

-- Before the patch, JIT compiler doesn't check slots overflow
-- for recording of metamethods call. So the assertion checking
-- that we don't overflow the slots limit (the limit
-- `LJ_MAX_JSLOTS` is 250) is failing, when we record metamethod
-- call (`J->baseslot` diff + `J->maxslot` ~ 5-8 stack slots),
-- while almost all slots of JIT engine are occupied.

-- Table with the simplest metamethod to call.
local a0 = setmetatable({}, {
  __add = function(t, arg1)
    t[arg1] = arg1
  end
})
_G.a0 = a0

-- Fixarg function with call to metamethod.
local function a1()
  -- This constant is not set as an upvalue to simplify stack
  -- slots counting. Just remember that it is 42.
  return a0 + 42
end
_G.a1 = a1

-- Generate bunch of functions to call them recursively.
-- Each function is a vararg function bumps slots on
-- 2 (4) = 1 (2) * 2 for usual Lua frame and vararg frame
-- recording for GC32 (GC64).
for i = 2, 121 do
  local f, err = load(('local r = a%d() return r'):format(i - 1))
  assert(f, err)
  _G['a'..i] = f
end

-- Trace is long enough, so we need to increase maxrecord.
jit.opt.start('hotloop=1', 'maxrecord=2048')

local function test_gc32()
  -- 1 - Base slot.
  -- 3 slots for cycle start, stop, step.
  for _ = 1, 4 do
    -- Occupy 1 slot for the function itself + 2 next slots will
    -- occupied for a call to the vararg function.
    -- Need 121 calls: 7 (baseslot after `a121()` is recorded)
    -- + 119 * 2 + 1 (`a1` -- is not vararg function) = 246 slots.
    -- The next call of metamethod in `a0` to record have 2 args
    -- + 2 slots for metamethod function + 1 slot for frame.
    -- luacheck: no global
    a121()
    assert(a0[42] == 42)
    a0[42] = nil
  end
  return true
end

local function test_gc64()
  -- 2 - Base slot.
  -- 3 slots for cycle start, stop, step.
  for _ = 1, 4 do
    -- Occupy 1 slot for the function itself + 4 next slots will
    -- occupied for a call to the vararg function.
    -- Need 60 calls: 10 (baseslot after `a60()` is recorded)
    -- + 58 * 4 + 2 (`a1` -- is not vararg function) = 244 slots.
    -- The next call of metamethod in `a0` to record have 2 args
    -- + 3 slots for metamethod function + 2 slots for frame.
    -- luacheck: no global
    a60()
    assert(a0[42] == 42)
    a0[42] = nil
  end
  return true
end

test:ok(test_gc32())
test:ok(test_gc64())

os.exit(test:check() and 0 or 1)
