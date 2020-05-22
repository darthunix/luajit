local tap = require('tap')
jit.off()
jit.flush()

local test = tap.test("gh-6163-jit-min-max")
test:plan(2)
--
-- gh-6163: math.min/math.max success with no args
--
local pcall = pcall

jit.opt.start(0, 'hotloop=1')
jit.on()

local r, _ = pcall(function() math.min() end)
test:ok(false == r)
r, _ = pcall(function() math.min() end)
test:ok(false == r)

os.exit(test:check() and 0 or 1)
