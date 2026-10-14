-- Drives norns/tether/lib/mod.lua with a stub norns (no device needed):
--   lua5.3 tests/mod_test.lua
-- Checks the protocol the plug-in relies on: hello -> welcome + paced dump, value changes, set_raw
-- for each parameter type, key / enc injection without echo, hardware reports, screen bands,
-- clock sync, client time-out, script reload.

local sent = {}
local fails, checks = 0, 0
local function check(cond, what)
  checks = checks + 1
  if not cond then fails = fails + 1; print("FAIL: " .. what) end
end

local t_now = 100.0

-- ---- stubs
util = {
  time = function() return t_now end,
  clamp = function(x, lo, hi) return math.max(lo, math.min(hi, x)) end,
  round = function(x) return math.floor(x + 0.5) end,
}
osc = { send = function(to, path, args) sent[#sent + 1] = { to = to, path = path, args = args } end }

local screen_buf = string.rep("\0", 128 * 64)
screen = {
  peek = function(x, y, w, h)
    return screen_buf:sub(y * 128 + 1, (y + h) * 128)
  end,
  clear = function() end, level = function() end, move = function() end, text = function() end, update = function() end,
}

local metros = {}
metro = {
  init = function(fn, t, n) local m = { id = #metros + 1, fn = fn, start = function() end }; metros[#metros + 1] = m; return m end,
  free = function() end,
}

local tempo_set, started, stopped = nil, 0, 0
clock = { internal = { set_tempo = function(b) tempo_set = b end, start = function() started = started + 1 end, stop = function() stopped = stopped + 1 end } }

local keys, encs = {}, {}
_norns = {
  osc = { event = function(path) _G.passed = path end },
  key = function(n, z) keys[#keys + 1] = { n, z } end,
  enc = function(n, d) encs[#encs + 1] = { n, d } end,
}
norns = { state = { name = "awake" } }

local function P(t, fields) fields.t = t; return fields end
local ctl = P(3, { id = "cutoff", name = "cutoff", raw = 0.25,
  get_raw = function(self) return self.raw end, set_raw = function(self, r) self.raw = r end,
  string = function(self) return string.format("%.0f hz", 100 + self.raw * 1000) end })
local num = P(1, { id = "steps", name = "steps", min = 1, max = 16, value = 4, string = function(self) return tostring(self.value) end })
local opt = P(2, { id = "mode", name = "mode", options = { "a", "b", "c" }, count = 3, selected = 1, string = function(self) return self.options[self.selected] end })
local bin = P(9, { id = "mute", name = "mute", value = 0, string = function(self) return self.value == 1 and "on" or "off" end })
local grp = P(7, { id = "grp", name = "GROUP" })
local trg = P(6, { id = "reseed", name = "reseed", fired = 0, string = function() return "" end })
local tempo = P(1, { id = "clock_tempo", name = "tempo", min = 1, max = 300, value = 90, string = function(self) return tostring(self.value) end })
local src = P(2, { id = "clock_source", name = "source", options = { "internal", "midi", "link", "crow" }, count = 4, selected = 1, string = function() return "internal" end })

local list = { ctl, grp, num, opt, bin, trg, tempo, src }
local lookup = {}
for _, p in ipairs(list) do lookup[p.id] = p end
local pset_read, pset_write = nil, nil
params = {
  params = list,
  lookup_param = function(self, id) return lookup[id] end,
  visible = function(self, i) return true end,
  get = function(self, id) local p = lookup[id]; if p.t == 2 then return p.selected end; return p.value end,
  set = function(self, id, v, silent)
    local p = lookup[id]
    if p.t == 2 then p.selected = v elseif p.t == 6 then p.fired = p.fired + 1 else p.value = v end
  end,
  read = function(self, n) pset_read = n end,
  write = function(self, n, name) pset_write = n end,
}

local hooks, menu = {}, nil
package.loaded['core/mods'] = {
  this_name = "tether",
  hook = { register = function(which, name, fn) hooks[which] = fn end },
  menu = { register = function(name, m) menu = m end, exit = function() end, redraw = function() end },
}

-- ---- load the mod (the repo's tests run from the repo root)
local here = arg and arg[0] and arg[0]:match("(.*/)") or "./"
local T = dofile(here .. "../norns/tether/lib/mod.lua")
check(hooks.system_post_startup ~= nil and hooks.script_post_init ~= nil and menu ~= nil, "hooks and menu registered")
hooks.system_post_startup()
hooks.script_post_init()

local from = { "10.0.0.5", "54321" }
local function rx(path, args) _norns.osc.event(path, args, from) end
local function take(path)
  local out = {}
  for _, m in ipairs(sent) do if m.path == path then out[#out + 1] = m end end
  return out
end
local function clear() sent = {} end
local function ticks(n) for _ = 1, n do T.tick() end end

-- ---- hello: welcome, then the list, paced
rx("/tether/hello", { 54321, "Track 3" })
check(#take("/tether/welcome") == 1, "welcome")
check(take("/tether/welcome")[1].args[2] == "awake", "welcome names the script")
local begin = take("/tether/params/begin")
check(#begin == 1 and begin[1].args[1] == 7, "begin: 7 drivable params (the group is left out), got " .. tostring(begin[1] and begin[1].args[1]))
check(sent[1].to[1] == "10.0.0.5" and sent[1].to[2] == 54321, "replies go to the hello's host and port")
ticks(1)
local rows = take("/tether/p")
check(#rows == 7, "rows sent: " .. #rows)
check(#take("/tether/params/end") == 1, "end")
check(rows[1].args[2] == "cutoff" and math.abs(rows[1].args[5] - 0.25) < 1e-6 and rows[1].args[6] == "350 hz", "control row")
check(rows[2].args[2] == "steps" and math.abs(rows[2].args[5] - 3 / 15) < 1e-6 and rows[2].args[7] == 16, "number row")
check(rows[3].args[7] == 3 and rows[3].args[5] == 0, "option row")
check(_G.passed == nil, "tether paths stop at the mod")

-- ---- the plug-in writes
clear()
rx("/tether/set", { "cutoff", 0.5 })
check(math.abs(ctl.raw - 0.5) < 1e-6, "set control raw")
rx("/tether/set", { "steps", 1.0 })
check(num.value == 16, "set number top")
rx("/tether/set", { "mode", 0.5 })
check(opt.selected == 2, "set option middle")
rx("/tether/set", { "mute", 0.7 })
check(bin.value == 1, "set binary")
rx("/tether/set", { "reseed", 1.0 })
check(trg.fired == 1, "trigger fires")
rx("/tether/set", { "nope", 1.0 })
ticks(2)
local v = take("/tether/v")
check(#v == 4, "changes reported: " .. #v)
check(v[1].args[1] == 1 and v[1].args[3] == "600 hz", "change carries index and text")

-- a change made on norns itself comes back too
clear()
ctl.raw = 0.9
ticks(2)
check(#take("/tether/v") == 1, "a norns-side change is reported")

-- ---- keys and encoders
clear()
rx("/tether/key", { 2, 1 })
rx("/tether/enc", { 3, -2 })
check(#keys == 1 and keys[1][1] == 2 and keys[1][2] == 1, "key injected")
check(#encs == 1 and encs[1][2] == -2, "enc injected")
check(#take("/tether/hw") == 0, "injected input is not echoed")
_norns.key(3, 1)
_norns.enc(1, 5)
local hw = take("/tether/hw")
check(#hw == 2 and hw[1].args[1] == "key" and hw[2].args[1] == "enc" and hw[2].args[3] == 5, "hardware reported")

-- ---- other osc still reaches the script / core
rx("/param/cutoff", { 1 })
check(_G.passed == "/param/cutoff", "other paths pass through")

-- ---- screen
clear()
ticks(1)
check(#take("/tether/scr") == 0, "no screen until asked")
rx("/tether/screen", { 30, 54321 })
screen_buf = string.rep("\15", 128) .. string.rep("\0", 128 * 63)
ticks(1)
local scr = take("/tether/scr")
check(#scr == 8, "first frame whole: " .. #scr)
check(#scr[1].args[2] == 1024 and scr[1].args[2]:sub(1, 3) == "fff" and scr[1].args[2]:sub(129, 131) == "000", "band hex")
clear()
ticks(1)
check(#take("/tether/scr") == 1, "unchanged frame: one healing band")
rx("/tether/screen", { 0, 54321 })
clear()
ticks(3)
check(#take("/tether/scr") == 0, "screen off")

-- ---- clock
rx("/tether/clock", { 128.5, 1, 2 })
check(tempo_set == 128.5 and tempo.value == 129 and started == 1, "tempo + start")
rx("/tether/clock", { 128.5, 0, 2 })
check(stopped == 1, "stop")
src.selected = 2
rx("/tether/clock", { 140, 1, 2 })
check(tempo_set == 128.5, "external clock source: left alone")
src.selected = 1
rx("/tether/clock", { 140, 1, 1 })
check(tempo_set == 140 and started == 1, "tempo only mode does not start")

-- ---- audio: the daemon is started once, for this machine's architecture
local executed = {}
os.execute = function(c) executed[#executed + 1] = c; return true end
_path = { code = "/home/we/dust/code/" }
rx("/tether/audio/start", {})
rx("/tether/audio/start", {})
check(#executed == 1, "audio daemon started once: " .. #executed)
check(executed[1] and executed[1]:find("/home/we/dust/code/tether/bin/", 1, true) ~= nil, "from the mod's bin folder")
check(executed[1] and executed[1]:find("armv7l) b=tether-audio-armhf;;", 1, true) ~= nil and executed[1]:find("aarch64) b=tether-audio-arm64;;", 1, true) ~= nil, "picks the binary by uname -m")
check(executed[1] and executed[1]:find("nohup", 1, true) ~= nil and executed[1]:find("&)", 1, true) ~= nil, "in the background")
t_now = t_now + 5
rx("/tether/audio/start", {})
check(#executed == 2, "asked again later: started again (a second copy exits by itself)")

-- ---- pset
rx("/tether/pset/load", { 3 })
rx("/tether/pset/save", { 4, "x" })
check(pset_read == 3 and pset_write == 4, "pset")

-- ---- ping, range, script reload, time-out
clear()
rx("/tether/ping", { 7, 54321 })
check(#take("/tether/pong") == 1 and take("/tether/pong")[1].args[1] == 7, "pong")
rx("/tether/range", { 2, 3, 54321 })
check(#take("/tether/p") == 2, "range resend")
clear()
norns.state.name = "mlr"
hooks.script_post_cleanup()
hooks.script_pre_init()
hooks.script_post_init()
check(#take("/tether/script") == 1 and take("/tether/script")[1].args[1] == "mlr", "script change announced")
check(#take("/tether/params/begin") == 1, "new dump")
t_now = t_now + 10
clear()
ticks(1)
check(next(T.state.clients) == nil, "silent client dropped")
check(#take("/tether/p") == 0, "nothing sent to dropped client")

print(string.format("mod: %d checks, %d failed", checks, fails))
os.exit(fails == 0 and 0 or 1)
