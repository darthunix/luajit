-- the test is GC64 only
local ffi = require('ffi')
require('utils').skipcond(not ffi.abi('gc64'), 'test is GC64 only')

local tap = require("tap")
local test = tap.test("gh-4199-gc64-flaky")
test:plan(1)

-- Chomp memory in currently allocated gc space
collectgarbage('stop')
jit.opt.start('hotloop=1')

ffi.new('int[?]', 300 * 1024 * 1024)
ffi.new('int[?]', 300 * 1024 * 1024)

-- Generate a bunch of traces to trigger constant placement at the
-- end of the trace. Since global describing the mcode area in the
-- jit structure is not updated, the next trace generated will
-- invalidate the constant of the previous trace. Then
-- execution of the _previous_ trace will use wrong value.

-- Keep last two functions generated to compare results
local s = {}
local test_res = true

-- The number of iteration is a guess, depending on the situation
-- in GC and the amount of currently pre-allocated mcode area. Usually
-- works under 1000, wich doesnt take too long in case of success, so
-- I gave up to locate a better way to chomp the mcode area.

for n = 1, 1000 do
    local src=string.format([[
        function f%d(x, y, z, f, g, h, j, k, r, c, d)
            local a = {}
            for i = 1, 5 do
                a[i] = x + y + z + f + g + h + j + k + r + c + d
                if (x > 0) then
		    a[i] = a[i] + 1.1
		end
                if (c > 0) then
                    a[i] = a[i] + 2.2
                end
                if (z > 0) then
                    a[i] = a[i] + 3.3
                end
                if (f > 0) then
                    a[i] = a[i] + 4.4
                end
                x = x + r
                y = y - c
                z = z + d
            end
            return a[1]
        end
        return f%d(...)
        ]], n, n)
    s[2] = load(src)
    if s[1] ~= nil then
	local res1 = s[1](1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11)
	local res2 = s[2](1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11)
	if res1 ~= res2 then
            test_res = false
            break
        end
    end
    s[1] = s[2]
end

test:ok(test_res, 'IR constant fusion')

