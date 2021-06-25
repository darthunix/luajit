local tap = require('tap')

-- Test file to demonstrate next FF incorrect behaviour on LJ_64.
-- See also, https://github.com/LuaJIT/LuaJIT/issues/727.

local test = tap.test('lj-727-lightuserdata-itern')
test:plan(1)

local ud = require('lightuserdata').craft_ptr_wp()

-- We now have the tagged lightuuserdata pointer
-- 0xFFFE7FFF00000002 in the up before this patch (after the patch
-- the maximum available lightuserdata segment is 0xffe).

-- These pointers are special in for loop keys as they are used in
-- the INTERN control variable to denote the current index in the
-- array.
-- If the ITERN is then patched to ITERC because of
-- despecialization via the ISNEXT bytecode, the control variable
-- is considered as the existing key in the table and some
-- elements are skipped during iteration.

local real_next = next
local next = next

local function itern_test(t, f)
  for k, v in next, t do
    f(k, v)
  end
end

local visited = {}
local t = {1, [ud] = 2345}
itern_test(t, function(k, v)
  visited[k] = v
  if next == real_next then
    next = function(tab, key)
      return real_next(tab, key)
    end
    -- Despecialize bytecode.
    itern_test({}, function() end)
  end
end)

test.strict = true
test:is_deeply(visited, t, 'userdata node is visited')

os.exit(test:check() and 0 or 1)
