-- The test is GC64 only.
local ffi = require('ffi')
require('utils').skipcond(not ffi.abi('gc64'), 'test is GC64 only')

local tap = require('tap')
local test = tap.test('gh-4199-gc64-fuse')
test:plan(1)

collectgarbage()
-- Chomp memory in currently allocated GC space.
collectgarbage('stop')

for _ = 1, 8 do
  ffi.new('char[?]', 256 * 1024 * 1024)
end

jit.opt.start('hotloop=1')

-- Generate a bunch of traces to trigger constant placement at the
-- end of the trace. Since global describing the mcode area in the
-- jit structure is not updated, the next trace generated will
-- invalidate the constant of the previous trace. Then
-- execution of the _previous_ trace will use wrong value.

-- Keep last two functions generated to compare results.
local s = {}
local test_res = true

-- XXX: The number of iteration is fragile, depending on the trace
-- length and the amount of currently pre-allocated mcode area.
-- Usually works under 100, which doesn't take too long in case of
-- success, so I gave up to locate a better way to chomp the mcode
-- area.

for n = 1, 100 do
  local src = string.format([[
    function f%d(x, y)
      local a = {}
      for i = 1, 5 do
        a[i] = 0
        -- This summ is not necessarry but decreases the amount of
        -- iterations.
        a[i] = a[i] + x + y
        -- Use all FPR registers and one value from the memory
        -- (xmm0 is for result, xmm15 states for work with table).
        a[i] = a[i] + 1.1
        a[i] = a[i] + 2.2
        a[i] = a[i] + 3.3
        a[i] = a[i] + 4.4
        a[i] = a[i] + 5.5
        a[i] = a[i] + 6.6
        a[i] = a[i] + 7.7
        a[i] = a[i] + 8.8
        a[i] = a[i] + 9.9
        a[i] = a[i] + 10.10
        a[i] = a[i] + 11.11
        a[i] = a[i] + 12.12
        a[i] = a[i] + 13.13
        a[i] = a[i] + 14.14
        a[i] = a[i] + 15.15
      end
      return a[1]
    end
    return f%d(...)
  ]], n, n)
  s[2] = load(src)
  if s[1] ~= nil then
    local res1 = s[1](1, 2)
    local res2 = s[2](1, 2)
    if res1 ~= res2 then
      test_res = false
      break
    end
  end
  s[1] = s[2]
end

test:ok(test_res, 'IR constant fusion')
os.exit(test:check() and 0 or 1)
