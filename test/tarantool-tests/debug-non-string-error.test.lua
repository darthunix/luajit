local tap = require('tap')

local test = tap.test('debug-non-string-error')
test:plan(1)

local i = 0
while arg[i] do i = i - 1 end
local luabin = arg[i + 1]

local magic = 42
-- XXX: Need \n before print to be interpreted as independend
-- command.
local cmd = ([[
  echo 'error({});
  print(%d)' | %s -e 'debug.debug()' 2>&1
]]):format(magic, luabin)

local proc = io.popen(cmd)
local res = proc:read('*all'):gsub('%s+$', '')
local ldb = 'lua_debug> '
local errmsg = '(error object is not a string)'
-- XXX: lines aren't broken by '\n', so need 2 `ldb`.
local expected = ldb .. errmsg .. '\n' .. ldb .. ldb .. magic
test:ok(res == expected, 'handle non-string error in debug.debug()')

os.exit(test:check() and 0 or 1)
