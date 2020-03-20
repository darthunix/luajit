local tap = require('tap')
local utils = require('utils')

local test = tap.test('fix-gc-setupvalue')
test:plan(1)

-- Test file to demonstrate LuaJIT GC invariant violation
-- for inherited upvalues.

-- The bug is about the situation, when black upvalue refers to
-- a white object. This happens due to parent function is marked
-- first (all closed upvalues and function are colored to black),
-- and then `debug.setupvalue()` is called for a child function
-- with inherited upvalues. The barrier is move forward for a
-- non-marked function (instead upvalue) and invariant is
-- violated.

-- Create to functions with closed upvalue.
do
  local uv = 1
  local function f_parent()
    local function f()
      return uv + 1
    end
    _G.f = f
    return uv + 1
  end
  -- Set up `f()`.
  f_parent()
  _G.f_parent = f_parent
end

-- Set GC on start.
collectgarbage()
-- Set minimally possible stepmul.
-- 1024/10 * stepmul == 10 < sizeof(GCfuncL), so it guarantees,
-- that 2 functions will be marked in different time.
local oldstepmul = collectgarbage('setstepmul', 1)

-- `f_parent()` function is marked before `f()`, so wait until
-- it becomes black and proceed with the test.
while not utils.gc_isblack(_G.f_parent) do
  collectgarbage('step')
end

-- Set created string (white) for the upvalue.
debug.setupvalue(_G.f, 1, '4'..'1')
_G.f = nil

-- Lets finish it faster.
collectgarbage('setstepmul', oldstepmul)
-- Finish GC cycle to be sure that the object is collected.
while not collectgarbage('step') do end

-- Generate some garbage to reuse freed memory.
for i = 1, 1e2 do local _ = {string.rep('0', i)} end

test:ok(_G.f_parent() == 42, 'correct set up of upvalue')

os.exit(test:check() and 0 or 1)
