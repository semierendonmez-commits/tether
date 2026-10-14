-- A stand-in norns for the plug-in's end-to-end test: the real norns/tether/lib/mod.lua on top of
-- stubs, driven line by line over stdin / stdout by tests/EngineTests.cpp, which carries the
-- lines to and from real OSC over UDP.
--   in:  IN <port> <path> <arg>...      an OSC message from the plug-in (args typed: i:3 f:0.5 s:text)
--        TICK [n]                       run the mod's poll n times
--        HW key|enc <n> <v>             the hardware moved
--        SET <id> <raw>                 something on norns moved a parameter
--        PAINT <band> <level>           fill one screen band
--        PIXELS <hex x 8192>            the whole screen, one hex digit a pixel
--        SCRIPT <name>                  load another script
--        GET <id>                       print the parameter's raw value
--   out: OUT <port> <path> <arg>...     an OSC message to send; END after each input line
io.stdout:setvbuf("no")

local function emit(...)
  io.write(table.concat({ ... }, "\t"), "\n")
end

local function fmt(v)
  if math.type(v) == "integer" then return "i:" .. v end
  if type(v) == "number" then return "f:" .. string.format("%.9g", v) end
  return "s:" .. tostring(v):gsub("[\t\n]", " ")
end

util = {
  time = function() return os.clock() * 0 + (util._t or 0) end,
  clamp = function(x, lo, hi) return math.max(lo, math.min(hi, x)) end,
  round = function(x) return math.floor(x + 0.5) end,
}
osc = { send = function(to, path, args)
  local parts = { "OUT", tostring(to[2]), path }
  for _, a in ipairs(args) do parts[#parts + 1] = fmt(a) end
  emit(table.unpack(parts))
end }

local screen_buf = string.rep("\0", 128 * 64)
screen = {
  peek = function(x, y, w, h) return screen_buf:sub(y * 128 + 1, (y + h) * 128) end,
  clear = function() end, level = function() end, move = function() end, text = function() end, update = function() end,
}
local tick_fn
metro = { init = function(fn) tick_fn = fn; return { id = 1, start = function() end } end, free = function() end }
clock = { internal = { set_tempo = function(b) clock.tempo = b end, start = function() clock.running = true end, stop = function() clock.running = false end } }
_norns = {
  osc = { event = function() end },
  key = function(n, z) emit("LOG", "key", n, z) end,
  enc = function(n, d) emit("LOG", "enc", n, d) end,
}
norns = { state = { name = "awake" } }
_path = { code = "/home/we/dust/code/" }
os.execute = function() emit("LOG", "audio_start"); return true end

local function make_params()
  local function P(t, f) f.t = t; return f end
  local list = {
    P(3, { id = "cutoff", name = "cutoff", raw = 0.25, get_raw = function(s) return s.raw end, set_raw = function(s, r) s.raw = r end,
      string = function(s) return string.format("%.0f hz", 100 + s.raw * 1000) end }),
    P(1, { id = "steps", name = "steps", min = 1, max = 16, value = 8, string = function(s) return tostring(s.value) end }),
    P(2, { id = "scale", name = "scale", options = { "major", "minor", "dorian", "lydian" }, count = 4, selected = 1, string = function(s) return s.options[s.selected] end }),
    P(9, { id = "hold", name = "hold", value = 0, string = function(s) return s.value == 1 and "on" or "off" end }),
    P(6, { id = "reseed", name = "reseed", fired = 0, string = function() return "" end }),
    P(1, { id = "clock_tempo", name = "tempo", min = 1, max = 300, value = 90, string = function(s) return tostring(s.value) end }),
    P(2, { id = "clock_source", name = "source", options = { "internal", "midi", "link", "crow" }, count = 4, selected = 1, string = function(s) return s.options[s.selected] end }),
  }
  for i = 1, 40 do                                     -- enough rows to need more than one tick
    list[#list + 1] = P(3, { id = "lvl" .. i, name = "level " .. i, raw = i / 40,
      get_raw = function(s) return s.raw end, set_raw = function(s, r) s.raw = r end, string = function(s) return string.format("%.2f", s.raw) end })
  end
  local lookup = {}
  for _, p in ipairs(list) do lookup[p.id] = p end
  return list, lookup
end

local list, lookup = make_params()
params = {
  lookup_param = function(self, id) return lookup[id] end,
  visible = function() return true end,
  get = function(self, id) local p = lookup[id]; if p.t == 2 then return p.selected end; return p.value end,
  set = function(self, id, v)
    local p = lookup[id]
    if p.t == 2 then p.selected = v elseif p.t == 6 then p.fired = p.fired + 1 else p.value = v end
  end,
  read = function(self, n) emit("LOG", "pset_read", n) end,
  write = function(self, n) emit("LOG", "pset_write", n) end,
}
params.params = list

local hooks = {}
package.loaded['core/mods'] = {
  this_name = "tether",
  hook = { register = function(which, name, fn) hooks[which] = fn end },
  menu = { register = function() end, exit = function() end, redraw = function() end },
}

local here = arg and arg[0] and arg[0]:match("(.*/)") or "./"
local T = dofile(here .. "../norns/tether/lib/mod.lua")
hooks.system_post_startup()
hooks.script_post_init()
util._t = 0

local function parse(tok)
  local k, v = tok:match("^(%a):(.*)$")
  if k == "i" then return math.tointeger(tonumber(v)) end
  if k == "f" then return tonumber(v) + 0.0 end
  return v or tok
end

emit("READY")
for line in io.lines() do
  local f = {}
  for tok in line:gmatch("[^\t]+") do f[#f + 1] = tok end
  local cmd = f[1]
  if cmd == "IN" then
    local args = {}
    for i = 4, #f do args[#args + 1] = parse(f[i]) end
    _norns.osc.event(f[3], args, { "127.0.0.1", f[2] })
  elseif cmd == "TICK" then
    for _ = 1, tonumber(f[2] or "1") do util._t = util._t + 1 / 30; tick_fn() end
  elseif cmd == "HW" then
    if f[2] == "key" then _norns.key(tonumber(f[3]), tonumber(f[4])) else _norns.enc(tonumber(f[3]), tonumber(f[4])) end
  elseif cmd == "SET" then
    local p = lookup[f[2]]; if p then p.raw = tonumber(f[3]) end
  elseif cmd == "PAINT" then
    local b, lvl = tonumber(f[2]), tonumber(f[3])
    screen_buf = screen_buf:sub(1, b * 1024) .. string.rep(string.char(lvl), 1024) .. screen_buf:sub((b + 1) * 1024 + 1)
  elseif cmd == "PIXELS" then
    screen_buf = (f[2] or ""):gsub(".", function(c) return string.char(tonumber(c, 16) or 0) end)
  elseif cmd == "SCRIPT" then
    norns.state.name = f[2]
    hooks.script_post_cleanup()
    list, lookup = make_params()
    table.remove(list, 1)                              -- the new script has one fewer parameter
    params.params = list
    hooks.script_pre_init()
    hooks.script_post_init()
  elseif cmd == "GET" then
    local p = lookup[f[2]]
    emit("VAL", tostring(T.get_raw(p)))
  elseif cmd == "TEMPO" then
    emit("VAL", tostring(clock.tempo), tostring(clock.running))
  end
  emit("END")
end
