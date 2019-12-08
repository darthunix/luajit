local tap = require('tap')

local test = tap.test('print-tostring-number')

-- First field is type, second -- content of cmd.
local test_data = {
  {'nil', 'nil'},
  {'boolean', 'true'},
  {'userdata', 'newproxy()'},
  {'number', '42'},
  -- FIXME: This test case is disabled, because __tostring
  -- metamethod isn't checked for string base metatable.
  -- See also https://github.com/tarantool/tarantool/issues/6746.
  -- {'string', '"teststr"'},
  {'table', '{}'},
  {'function', 'function() end'},
  {'thread', 'coroutine.create(function() end)'},
}

local NTEST = #test_data
test:plan(NTEST)

local i = 0
while arg[i] do i = i - 1 end
local luabin = arg[i + 1]

for j = 1, NTEST do
  local prefix = '__tostring reloaded for '
  local datatype = test_data[j][1]
  local expected = prefix .. datatype

  local cmd = luabin .. ([[ -e '
    local testvar = %s
    debug.setmetatable(testvar, {__tostring = function(a)
      return "%s" .. type(a)
    end})
    print(testvar)
  ']]):format(test_data[j][2], prefix)

  local proc = io.popen(cmd)
  local res = proc:read('*all'):gsub('%s+$', '')
  test:ok(res == expected, expected)
end

os.exit(test:check() and 0 or 1)
