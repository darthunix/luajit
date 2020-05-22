local tap = require('tap')
local jutil = require('jit.util')
jit.off()
jit.flush()

local test = tap.test("gh-6163-jit-min-max")
test:plan(6)

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

os.exit(test:check() and 0 or 1)
