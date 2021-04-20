local utils = require('utils')

-- Disabled on *BSD due to #4819.
utils.skipcond(jit.os == 'BSD', 'Disabled due to #4819')

-- XXX: Tweak the process environment to get around SIP.
-- See the comment in suite CMakeLists.txt for more info.
utils.tweakenv(jit.os == 'OSX', 'DYLD_LIBRARY_PATH')

utils.selfrun(arg, {
  {
    arg = {
      1, -- hotloop (arg[1])
      1, -- trigger (arg[2])
    },
    message = 'Trace is aborted',
    assertions = {
      like = {
        utils.jit.assert({
          traceno = 2,
          result = 'abort',
          reason = 'error thrown or hook called during recording',
          ir = { 'CALLXS' },
        }),
        '3', -- hotloop + trigger + 1
      },
    },
  },
  {
    arg = {
      1, -- hotloop (arg[1])
      2, -- trigger (arg[2])
    },
    message = 'Trace is recorded',
    assertions = {
      like = {
        utils.jit.assert({
          traceno = 2,
          result = 'stop',
          link = 'loop',
          ir = {
            'CALLXS (%b[])',
            'LOOP',
            'CALLXS %1',
          },
        }),
        'Lua VM re%-entrancy is detected while executing the trace',
      },
    },
  },
})

----- Test payload. ----------------------------------------------

local cfg = {
  hotloop = arg[1] or 1,
  trigger = arg[2] or 1,
}

local ffi = require('ffi')
local ffisandwich = ffi.load('libsandwich')
ffi.cdef('int increment(struct sandwich *state, int i)')

-- Save the current coroutine and set the value to trigger
-- <increment> call the Lua routine instead of C implementation.
local sandwich = require('libsandwich')(cfg.trigger)

-- Flush all collected traces to not break trace assertions.
jit.flush()
-- Depending on trigger and hotloop values the following contexts
-- are possible:
-- * if trigger <= hotloop -> trace recording is aborted
-- * if trigger >  hotloop -> trace is recorded but execution
--   leads to panic
jit.opt.start("3", string.format("hotloop=%d", cfg.hotloop))
-- Dump compiler progress to stdout that is required for trace
-- assertions above.
require('jit.dump').start('+tbisrmXaT')

local res
for i = 0, cfg.trigger + cfg.hotloop do
  res = ffisandwich.increment(sandwich, i)
end
-- Check the resulting value if panic didn't occur earlier.
print(res)
