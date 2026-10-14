-- tether: the norns side of TETHER (the AU / VST3 that drives norns from a DAW).
--
-- A mod, so it runs under every script. It answers the plug-in over OSC on port 10111:
--   the running script's parameter list and every change to it,
--   the screen (128 x 64, 16 levels) for the plug-in's mirror,
--   the hardware keys and encoders (so the family bus can use norns as a controller),
--   parameter writes, PSET load / save, tempo and transport from the host.
-- Protocol: docs/PROTOCOL.md in the TETHER repo.
--
-- install: ~/dust/code/tether/lib/mod.lua, then SYSTEM > MODS > tether (+), restart.

local mod = require 'core/mods'

local VERSION = 1
local PORT_TIMEOUT = 6.0          -- seconds without a ping before a client is dropped
local TICK = 1 / 30               -- the poll metro
local DUMP_PER_TICK = 24          -- parameter list rows sent per tick
local CHANGES_PER_TICK = 48
local BANDS, BAND_ROWS = 8, 8     -- screen bands of 128 x 8 pixels

local T = {
  NUMBER = 1, OPTION = 2, CONTROL = 3, FILE = 4, TAPER = 5,
  TRIGGER = 6, GROUP = 7, TEXT = 8, BINARY = 9, SEPARATOR = 0,
}

local state = {
  clients = {},                   -- "host:port" -> client
  metro = nil,
  frame = 0,
  script = "none",
  rows = {},                      -- index -> { id, raw, text } of the params the plug-in can see
  epoch = 0,                      -- bumps when the parameter list changes
  scr_last = {},                  -- band -> last band string sent
  scr_frame = 0,
  injecting = false,              -- keys / encoders coming from the plug-in are not echoed back
  sync = { bpm = nil, playing = nil },
  stats = { rx = 0, tx = 0 },
  orig = {},
}

-- ---------------------------------------------------------------------------------------------
-- helpers

local function now() return util.time() end

local function send(c, path, args)
  state.stats.tx = state.stats.tx + 1
  osc.send({ c.host, c.port }, path, args)
end

local function broadcast(path, args)
  for _, c in pairs(state.clients) do send(c, path, args) end
end

local function count_clients()
  local n = 0
  for _ in pairs(state.clients) do n = n + 1 end
  return n
end

local function script_name()
  local n = norns.state and norns.state.name
  if n == nil or n == "" then return "none" end
  return n
end

local function ptype(p)
  return p and p.t or -1
end

-- 0..1 for the plug-in, nil for parameters it cannot drive
local function get_raw(p)
  local t = ptype(p)
  if t == T.CONTROL or t == T.TAPER then
    local ok, v = pcall(p.get_raw, p)
    return ok and v or 0
  elseif t == T.NUMBER then
    local lo, hi = p.min or 0, p.max or 1
    if hi == lo then return 0 end
    return util.clamp(((p.value or lo) - lo) / (hi - lo), 0, 1)
  elseif t == T.OPTION then
    local n = p.count or (p.options and #p.options) or 1
    if n <= 1 then return 0 end
    return util.clamp(((p.selected or 1) - 1) / (n - 1), 0, 1)
  elseif t == T.BINARY then
    return (p.value or 0) > 0 and 1 or 0
  elseif t == T.TRIGGER then
    return 0
  end
  return nil
end

-- the number of positions a stepped parameter has (0: continuous)
local function steps(p)
  local t = ptype(p)
  if t == T.NUMBER then return math.floor((p.max or 0) - (p.min or 0)) + 1
  elseif t == T.OPTION then return p.count or (p.options and #p.options) or 1
  elseif t == T.BINARY or t == T.TRIGGER then return 2 end
  return 0
end

local function text_of(p)
  local ok, s = pcall(p.string, p)
  if ok and s ~= nil then return tostring(s) end
  return ""
end

local function set_raw(id, r)
  local p = params:lookup_param(id)
  if p == nil then return end
  r = util.clamp(r or 0, 0, 1)
  local t = ptype(p)
  if t == T.CONTROL or t == T.TAPER then
    pcall(p.set_raw, p, r)
  elseif t == T.NUMBER then
    local lo, hi = p.min or 0, p.max or 0
    pcall(params.set, params, id, util.round(lo + r * (hi - lo)))
  elseif t == T.OPTION then
    local n = p.count or (p.options and #p.options) or 1
    pcall(params.set, params, id, util.clamp(math.floor(r * (n - 1) + 0.5) + 1, 1, n))
  elseif t == T.BINARY then
    pcall(params.set, params, id, r >= 0.5 and 1 or 0)
  elseif t == T.TRIGGER then
    if r >= 0.5 then pcall(params.set, params, id, 1) end
  end
end

-- ---------------------------------------------------------------------------------------------
-- the parameter table

local function rebuild_rows()
  state.rows = {}
  state.script = script_name()
  local list = params and params.params or {}
  for i = 1, #list do
    local p = list[i]
    local r = get_raw(p)
    if r ~= nil and p.id ~= nil then
      local hidden = false
      local ok, vis = pcall(params.visible, params, i)
      if ok and vis == false then hidden = true end
      state.rows[#state.rows + 1] = { p = p, id = p.id, raw = r, text = text_of(p), hidden = hidden }
    end
  end
  state.epoch = state.epoch + 1
end

local function send_row(c, i)
  local row = state.rows[i]
  if row == nil then return end
  local p = row.p
  send(c, "/tether/p", { i, row.id, p.name or row.id, ptype(p), row.raw, row.text, steps(p), row.hidden and 1 or 0 })
end

local function start_dump(c)
  c.dump = 1
  c.dump_epoch = state.epoch
  send(c, "/tether/params/begin", { #state.rows, state.script, state.epoch })
end

local function pump_dump(c)
  if c.dump == nil then return end
  if c.dump_epoch ~= state.epoch then start_dump(c) end
  local last = math.min(#state.rows, c.dump + DUMP_PER_TICK - 1)
  for i = c.dump, last do send_row(c, i) end
  if last >= #state.rows then
    c.dump = nil
    send(c, "/tether/params/end", { #state.rows, state.epoch })
  else
    c.dump = last + 1
  end
end

-- the values that moved since the last tick (whoever moved them: the script, the menu, the plug-in)
local function scan_changes()
  local sent = 0
  for i = 1, #state.rows do
    local row = state.rows[i]
    local r = get_raw(row.p)
    if r ~= nil and math.abs(r - row.raw) > 1e-6 then
      row.raw = r
      row.text = text_of(row.p)
      broadcast("/tether/v", { i, r, row.text })
      sent = sent + 1
      if sent >= CHANGES_PER_TICK then return end
    end
  end
end

-- ---------------------------------------------------------------------------------------------
-- the screen

local HEX = {}
for v = 0, 255 do HEX[string.char(v)] = string.format("%x", math.min(v, 15)) end

local function screen_fps()
  local f = 0
  for _, c in pairs(state.clients) do f = math.max(f, c.fps or 0) end
  return f
end

local function pump_screen()
  local fps = screen_fps()
  if fps <= 0 or screen.peek == nil then return end
  state.scr_frame = state.scr_frame + 1
  local every = math.max(1, math.floor(30 / fps + 0.5))
  if state.scr_frame % every ~= 0 then return end
  -- one band is always resent, in turn: a lost packet heals within BANDS frames
  local heal = (state.scr_frame // every) % BANDS
  for b = 0, BANDS - 1 do
    local ok, buf = pcall(screen.peek, 0, b * BAND_ROWS, 128, BAND_ROWS)
    if ok and buf ~= nil and #buf == 128 * BAND_ROWS then
      if buf ~= state.scr_last[b] or b == heal then
        state.scr_last[b] = buf
        broadcast("/tether/scr", { b, (buf:gsub(".", HEX)) })
      end
    end
  end
end

-- ---------------------------------------------------------------------------------------------
-- host tempo and transport

local function apply_clock(bpm, playing, mode)
  -- mode 0 off, 1 tempo, 2 tempo + transport; only when norns runs its own clock
  if mode == nil or mode <= 0 then return end
  local ok, src = pcall(params.get, params, "clock_source")
  if not ok or src ~= 1 then return end
  if bpm ~= nil and bpm > 0 and (state.sync.bpm == nil or math.abs(bpm - state.sync.bpm) > 0.005) then
    state.sync.bpm = bpm
    pcall(params.set, params, "clock_tempo", util.round(bpm), true)
    pcall(clock.internal.set_tempo, bpm)
  end
  if mode >= 2 and playing ~= state.sync.playing then
    state.sync.playing = playing
    if playing then pcall(clock.internal.start) else pcall(clock.internal.stop) end
  end
end

-- ---------------------------------------------------------------------------------------------
-- OSC in

local function client_of(from, port)
  local host = from and from[1]
  if host == nil then return nil end
  port = tonumber(port) or tonumber(from[2])
  local key = host .. ":" .. tostring(port)
  local c = state.clients[key]
  if c == nil then
    c = { host = host, port = port, key = key, fps = 0, name = "", seen = now() }
    state.clients[key] = c
  end
  c.seen = now()
  return c
end

local function inject(fn, a, b)
  state.injecting = true
  pcall(fn, a, b)
  state.injecting = false
end

local handlers = {}

handlers["/tether/hello"] = function(args, from)
  local c = client_of(from, args[1])
  if c == nil then return end
  c.name = tostring(args[2] or "")
  send(c, "/tether/welcome", { VERSION, state.script })
  start_dump(c)
end

handlers["/tether/ping"] = function(args, from)
  local c = client_of(from, args[2])
  if c then send(c, "/tether/pong", { args[1] or 0, state.epoch }) end
end

handlers["/tether/bye"] = function(args, from)
  local c = client_of(from, args[1])
  if c then state.clients[c.key] = nil end
end

handlers["/tether/dump"] = function(args, from)
  local c = client_of(from, args[1])
  if c then start_dump(c) end
end

handlers["/tether/range"] = function(args, from)
  local c = client_of(from, args[3])
  if c == nil then return end
  local a, b = tonumber(args[1]) or 1, tonumber(args[2]) or 0
  for i = math.max(1, a), math.min(#state.rows, b) do send_row(c, i) end
end

handlers["/tether/set"] = function(args)
  if type(args[1]) == "string" then set_raw(args[1], tonumber(args[2])) end
end

handlers["/tether/key"] = function(args)
  inject(_norns.key, math.floor(args[1] or 0), math.floor(args[2] or 0))
end

handlers["/tether/enc"] = function(args)
  inject(_norns.enc, math.floor(args[1] or 0), math.floor(args[2] or 0))
end

handlers["/tether/screen"] = function(args, from)
  local c = client_of(from, args[2])
  if c == nil then return end
  c.fps = util.clamp(math.floor(args[1] or 0), 0, 30)
  state.scr_last = {}                                 -- the next frame goes out whole
end

handlers["/tether/clock"] = function(args)
  apply_clock(tonumber(args[1]), (tonumber(args[2]) or 0) > 0, tonumber(args[3]) or 0)
end

-- tether-audio (the audio stream, docs/AUDIO.md): a plug-in asks, the mod starts it once. The
-- daemon takes UDP 10112; a second copy finds it taken and exits, so asking twice is harmless.
local AUDIO_BIN = { aarch64 = "tether-audio-arm64", armv7l = "tether-audio-armhf", armv6l = "tether-audio-armhf", armhf = "tether-audio-armhf" }

local function start_audio()
  local t = now()
  if state.audio_started ~= nil and t - state.audio_started < 3 then return end
  state.audio_started = t
  local dir = (_path and _path.code or "/home/we/dust/code/") .. "tether/bin/"
  local cmd = "cd '" .. dir .. "' && a=$(uname -m) && case $a in "
  for arch, bin in pairs(AUDIO_BIN) do cmd = cmd .. arch .. ") b=" .. bin .. ";; " end
  cmd = cmd .. "*) b=tether-audio-$a;; esac && chmod +x \"$b\" 2>/dev/null; "
            .. "(nohup ./\"$b\" > /tmp/tether-audio.log 2>&1 &)"
  os.execute(cmd)
  print("tether: starting tether-audio")
end
state.start_audio = start_audio

handlers["/tether/audio/start"] = function()
  start_audio()
end

handlers["/tether/pset/load"] = function(args)
  local n = math.floor(args[1] or 0)
  if n > 0 then pcall(params.read, params, n) end
end

handlers["/tether/pset/save"] = function(args)
  local n = math.floor(args[1] or 0)
  if n > 0 then pcall(params.write, params, n, args[2] and tostring(args[2]) or nil) end
end

local function on_osc(path, args, from)
  local h = handlers[path]
  if h == nil then return false end
  state.stats.rx = state.stats.rx + 1
  local ok, err = pcall(h, args or {}, from)
  if not ok then print("tether: " .. path .. ": " .. tostring(err)) end
  return true
end

-- ---------------------------------------------------------------------------------------------
-- the hardware, reported to the plug-in

local function report(kind, n, v)
  if state.injecting or next(state.clients) == nil then return end
  broadcast("/tether/hw", { kind, n, v })
end

local function install_taps()
  if state.orig.osc ~= nil then return end
  state.orig.osc = _norns.osc.event
  _norns.osc.event = function(path, args, from)
    if type(path) == "string" and path:sub(1, 8) == "/tether/" and on_osc(path, args, from) then return end
    return state.orig.osc(path, args, from)
  end
  state.orig.key = _norns.key
  _norns.key = function(n, z)
    report("key", n, z)
    return state.orig.key(n, z)
  end
  state.orig.enc = _norns.enc
  _norns.enc = function(n, d)
    report("enc", n, d)
    return state.orig.enc(n, d)
  end
end

-- ---------------------------------------------------------------------------------------------
-- the poll

local function tick()
  state.frame = state.frame + 1
  if next(state.clients) == nil then return end
  local t = now()
  for key, c in pairs(state.clients) do
    if t - c.seen > PORT_TIMEOUT then state.clients[key] = nil end
  end
  for _, c in pairs(state.clients) do pump_dump(c) end
  if state.frame % 2 == 0 then scan_changes() end
  pump_screen()
end

-- Script.clear frees every metro after script_post_cleanup: the poll is made again on each load
local function start_metro()
  if state.metro ~= nil then pcall(metro.free, state.metro.id) end
  state.metro = metro.init(tick, TICK, -1)
  if state.metro then state.metro:start() end
end

local function list_changed()
  rebuild_rows()
  broadcast("/tether/script", { state.script, state.epoch })
  for _, c in pairs(state.clients) do start_dump(c) end
end

mod.hook.register("system_post_startup", "tether", function()
  install_taps()
  rebuild_rows()
  start_metro()
end)

mod.hook.register("script_pre_init", "tether", function()
  start_metro()
end)

mod.hook.register("script_post_init", "tether", function()
  start_metro()
  list_changed()
end)

mod.hook.register("script_post_cleanup", "tether", function()
  state.rows = {}
  state.script = "none"
  state.epoch = state.epoch + 1
  state.sync = { bpm = nil, playing = nil }
end)

-- ---------------------------------------------------------------------------------------------
-- SYSTEM > MODS > TETHER: who is connected

local m = {}

m.key = function(n, z)
  if n == 2 and z == 1 then mod.menu.exit() end
  if n == 3 and z == 1 then
    state.clients = {}            -- drop everyone: plug-ins say hello again within a second
    mod.menu.redraw()
  end
end

m.enc = function() end

m.redraw = function()
  screen.clear()
  screen.level(15)
  screen.move(0, 10)
  screen.text("TETHER  v" .. VERSION)
  screen.level(6)
  screen.move(0, 20)
  screen.text("script: " .. state.script .. "  (" .. #state.rows .. ")")
  local y = 31
  if count_clients() == 0 then
    screen.move(0, y)
    screen.text("no plug-in connected")
  end
  for _, c in pairs(state.clients) do
    if y > 52 then break end
    screen.level(15)
    screen.move(0, y)
    local label = c.name ~= "" and c.name or "TETHER"
    screen.text(label .. "  " .. c.host .. (c.fps > 0 and "  scr" or ""))
    y = y + 10
  end
  screen.level(3)
  screen.move(0, 62)
  screen.text("K2 back   K3 drop all")
  screen.update()
end

m.init = function() end
m.deinit = function() end

mod.menu.register(mod.this_name, m)

-- the plug-in side's tests load this file with stubs and drive it
return { state = state, handlers = handlers, tick = tick, on_osc = on_osc, get_raw = get_raw, set_raw = set_raw,
         rebuild_rows = rebuild_rows, apply_clock = apply_clock }
