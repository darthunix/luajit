local tap = require("tap")

local test = tap.test("misc-sysprof-lapi")
test:plan(14)

jit.off()
jit.flush()

local bufread = require "utils.bufread"
local symtab = require "utils.symtab"
local sysprof = require "sysprof.parse"

local TMP_BINFILE = arg[0]:gsub(".+/([^/]+)%.test%.lua$", "%.%1.sysprofdata.tmp.bin")
local BAD_PATH = arg[0]:gsub(".+/([^/]+)%.test%.lua$", "%1/sysprofdata.tmp.bin")

local function payload()
  local function fib(n)
    if n <= 1 then
      return n
    end
    return fib(n - 1) + fib(n - 2)
  end
  return fib(32)
end

local function generate_output(file, opts)
  file = file or "sysprof.bin"
  local res, err = misc.sysprof.start(file, opts)
  assert(res, err)

  payload()

  res,err = misc.sysprof.stop()
  assert(res, err)
end
--## GENERAL ###############################################################--

--wrong profiling mode
local res, err, errno = misc.sysprof.start{ mode = "A" }
test:ok(res == nil and err:match("profiler misuse"))
test:ok(type(errno) == "number")

--already running
res, err = misc.sysprof.start{ mode = "D" }
assert(res, err)

res, err, errno = misc.sysprof.start{ mode = "D" }
test:ok(res == nil and err:match("profiler is running already"))
test:ok(type(errno) == "number")

res, err = misc.sysprof.stop()
assert(res, err)

--not running
res, err, errno = misc.sysprof.stop()
test:ok(res == nil and err:match("profiler is not running"))
test:ok(type(errno) == "number")

--bad path
res, err, errno = misc.sysprof.start(BAD_PATH, { mode = "C" })
test:ok(res == nil and err:match("No such file or directory"))
test:ok(type(errno) == "number")

--bad interval
res, err, errno = misc.sysprof.start{ mode = "C", interval = -1 }
test:ok(res == nil and err:match("profiler misuse"))
test:ok(type(errno) == "number")
--## DEFAULT MODE ##########################################################--

res, err = pcall(generate_output, nil, { mode = "D", interval = 11 })
if res == nil then
  error(err)
end

local report = misc.sysprof.report()

test:ok(report.samples > 0)  -- TODO: more accurate test?
test:ok(report.vmstate.LFUNC > 0)
test:ok(report.vmstate.TRACE == 0)

-- with very big interval
res, err = pcall(generate_output, nil, { mode = "D", interval = 1000 })
if res == nil then
  error(err)
end

report = misc.sysprof.report()
test:ok(report.samples == 0)

--## CALL MODE #############################################################--

res, err = pcall(
  generate_output, TMP_BINFILE, { mode = "C", interval = 11 }
)
if res == nil then
  error(err)
end

local reader = bufread.new(TMP_BINFILE)
symtab.parse(reader)
sysprof.parse(reader)

os.remove(TMP_BINFILE)

jit.on()
os.exit(test:check() and 0 or 1)
