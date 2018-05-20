local ffi = require('ffi')
local tap = require('tap')

local test = tap.test('lj-416-xor-before-jcc')
test:plan(1)

-- To reproduce this issue, we need:
-- 1) a register which contains the constant zero value
-- 2) a floating point comparison operation
-- 3) the comparison operation to perform a fused load, which in
--    turn needs to allocate a register, and for there to be no
--    free registers at that moment, and for the register chosen
--    for sacrifice to be holding the constant zero.
--
-- This leads to assembly code like the following:
--   ucomisd xmm7, [r14+0x18]
--   xor r14d, r14d
--   jnb 0x12a0e001c ->3
--
-- That xor is a big problem, as it modifies flags between the
-- ucomisd and the jnb, thereby causing the jnb to do the wrong
-- thing.

ffi.cdef[[
  int test_xor_func(int a, int b, int c, int d, int e, int f, void * g, int h);
]]
local testxor = ffi.load('libtestxor.so')

local handler = setmetatable({}, {
  __newindex = function ()
    -- 0 and nil are suggested as differnt constant-zero values
    -- for the call and occupied different registers.
    testxor.test_xor_func(0, 0, 0, 0, 0, 0, nil, 0)
  end
})

local mconf = {
  { use = false, value = 100 },
  { use = true,  value = 100 },
}

local function testf()
  -- Generate register pressure.
  local value = 50
  for _, rule in ipairs(mconf) do
    if rule.use then
      value = rule.value
      break
    end
  end

  -- This branch shouldn't be taken.
  if value <= 42 then
    return true
  end

  -- Nothing to do, just call testxor with many arguments.
  handler[4] = 4
end

jit.opt.start('hotloop=1')
-- There are several traces to compile, and error 'inner loop in
-- the root trace' is raised several times, so we need the bigger
-- (than standard 4) value for iterations.
for _ = 1, 12 do
  -- Don't use any `test` functions here to freeze the trace.
  assert (not testf())
end
test:ok(true, 'imposible branch is not taken')

os.exit(test:check() and 0 or 1)
