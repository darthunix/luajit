local libcur_L = require('libcur_L')
local tap = require('tap')

local test = tap.test('gh-6189-cur_L')
test:plan(1)

local function cbool(cond)
  if cond then
    return 1
  else
    return 0
  end
end

-- Compile function to trace with snapshot.
jit.opt.start('hotloop=1')
cbool(true)
cbool(true)

pcall(libcur_L.error_from_other_thread)
-- Call with restoration from a snapshot with wrong cur_L.
cbool(false)

test:ok(true)
os.exit(test:check() and 0 or 1)
