-- Sysprof is implemented for x86 and x64 architectures only.
require("utils").skipcond(
  jit.arch ~= "x86" and jit.arch ~= "x64",
  jit.arch.." architecture is NIY for memprof"
)

local testsysprof = require("testsysprof")

local tap = require("tap")

local test = tap.test("clib-misc-sysprof")
test:plan(4)

test:ok(testsysprof.base())
test:ok(testsysprof.validation())

local function lua_payload(n)
  if n <= 1 then
    return n
  end
  return lua_payload(n - 1) + lua_payload(n - 2)
end

local function payload()
  local n_iterations = 5000000

  local co = coroutine.create(function ()
    for i = 1, n_iterations do
      if i % 2 == 0 then
        testsysprof.c_payload(10)
      else
        lua_payload(10)
      end
      coroutine.yield()
    end
  end)

  for _=1,n_iterations do
    coroutine.resume(co)
  end
end

local jit = require('jit')

jit.off()

test:ok(testsysprof.profile_func(payload))

jit.on()
jit.flush()

test:ok(testsysprof.profile_func(payload))

