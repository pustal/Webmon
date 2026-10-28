-- Webmon bridge (embedded in the webmon executable, run before LÖVE boots).
--
-- 1. File picker. gen1recomp asks platforms without a desktop file dialog for
--    a "native picker": love.system.pickFile(kind) opens it, then the game
--    polls love.system.getPickedFile() for the chosen path and
--    love.system.getPickError() for a failure. Webmon's picker is drawn over
--    the game and lists the files Webmon knows of that kind: TV storage
--    (uploaded through the upload page), the USB Webmon folder, and for ROMs
--    and saves RetroArch-style roms/ and saves/ folders on USB drives.
-- 2. Menu. Remote key 1 (caught by the launcher's SDL filter, read through
--    webmon.takePanelToggles, so the game never sees it) or a controller's
--    Home/guide button (unused by the game) opens Webmon's menu: the upload
--    page address and PIN, the game's keyboard-only functions (quick save and
--    load, tilt, zoom, colours, game speed, a voxel mod's view and camera
--    distance, the mod manager) so a controller or remote can reach them, and
--    quitting. A small panel with the address shows for a few seconds at start.
--
-- 3. Memory. LÖVE objects (meshes, image data, canvases) hold large native
--    buffers that Lua's garbage collector does not count, so a mod building
--    lots of them (a voxel world cache) can run the TV out of RAM long before
--    Lua thinks it is time to collect, and webOS then restarts the app. Once a
--    second the bridge checks the process size and runs a full collection
--    when it has grown a lot, or when the TV is running low. If the TV stays
--    critically low even after collecting, the whole TV would soon freeze, so
--    a countdown offers a fresh start: the game quits cleanly and Webmon
--    starts again as a new process, handing all its memory back (a voxel
--    cache build then resumes where it stopped).
--
-- The picker needs love.system, so the global require is wrapped and the
-- picker installed just before "main" is required. Drawing hooks
-- love.graphics.present, which every frame calls whatever the game does with
-- love.draw; the input callbacks are wrapped after main.lua loads and
-- re-wrapped whenever the game replaces them.

local webmon = webmon
local originalRequire = require

-- Kinds the picker serves (gen1recomp's names) and their titles.
local KINDS = {
  rom = "Choose a ROM",
  sav = "Choose a save file",
  mod = "Choose a mod (.zip)",
  cart = "Choose a cart (.g1rcart)",
  required_import = "Choose the file the mod needs (N64 ROM)",
}
local KIND_FOLDERS = {
  rom = "Webmon/roms (or, for Pokemon titles, a RetroArch roms folder)",
  sav = "Webmon/saves (or, for Pokemon titles, a RetroArch saves folder)",
  mod = "Webmon/mod-zips",
  cart = "Webmon/carts",
  required_import = "Webmon/n64 (or, for Pokemon titles, a RetroArch roms folder)",
}
-- The game's kind -> Webmon's file list. A mod's required import (such as the
-- Stadium 2 importer's ROM) is listed from Webmon's N64 ROMs.
local LIST_KIND = { rom = "rom", sav = "sav", mod = "mod", cart = "cart", required_import = "n64" }

local PANEL_START_SECONDS = 10   -- info panel shown at start
local MEMORY_CHECK_SECONDS = 1   -- how often the process size is checked
local MEMORY_GROWTH_MB = 48      -- full collection after this much growth since the last one
local MEMORY_GROWTH_MAX_MB = 384 -- ... a limit that doubles up to this while collections free little
local MEMORY_LOW_MB = 160        -- ... or when the TV has less than this available
local MEMORY_LOW_REPEAT_SECONDS = 5
local MEMORY_LOW_REPEAT_MAX_SECONDS = 60 -- ... an interval that doubles up to this while they free little
local MEMORY_USEFUL_MB = 16      -- a collection freeing less than this was not worth its pause
local MEMORY_CRITICAL_MB = 130   -- still under this after a collection: critical
local MEMORY_CRITICAL_TIMES = 3  -- ... this many times in a row: offer a fresh start
local PERF_LOG_SECONDS = 15      -- how often frame rate and clean-up time are logged
local RESTART_COUNTDOWN_SECONDS = 15
local RESTART_SNOOZE_SECONDS = 120 -- after Back, how long before offering again
local REFRESH_SECONDS = 3        -- picker list refresh while open
local VISIBLE_ROWS = 10

local picker = {
  open = false,
  kind = nil,
  files = {},
  index = 1,
  refreshIn = 0,
  picked = nil,   -- path handed to getPickedFile()
  failure = nil,  -- text handed to getPickError()
}

local panel = {
  visible = true,
  hideIn = PANEL_START_SECONDS,
}

-- ---- Menu -------------------------------------------------------------------------

-- Entries: label, and what OK does (key: a key press handed to the game; run:
-- a function), what Left/Right do (left/right: key presses), and close: shut
-- the menu after OK. The keys are gen1recomp's own keyboard shortcuts (and
-- PotatoVoxel's 8, Q and E); a shortcut a screen does not allow does nothing.
local MENU = {
  { label = "Back to the game", run = "close" },
  { label = "Quick save", key = "f1", close = true },
  { label = "Quick load (the last save)", key = "f2", close = true },
  { label = "Tilt (3D view)", key = "3" },
  { label = "Voxel view, next step (PotatoVoxel)", key = "8" },
  { label = "Zoom level", key = "4" },
  { label = "Zoom out / in  (Left / Right)", left = "-", right = "=" },
  { label = "Camera farther / closer  (Left / Right, voxel mods)", left = "e", right = "q" },
  { label = "Colours", key = "2" },
  { label = "Game speed slower / faster  (Left / Right)", left = "0", right = "1" },
  { label = "Mod manager", key = "f10", close = true },
  { label = "Voxel debug overlay: timings (PotatoVoxel)", key = "f9", close = true },
  { label = "Quit Webmon", run = "quit" },
}

local menu = {
  open = false,
  index = 1,
  confirmQuit = false, -- Quit was chosen once; OK again quits
}

local game = {} -- the game's own input callbacks (filled in by wrapInput below)

-- Hands a key press (and its release) to the game, as if typed.
local function sendKey(key)
  if game.keypressed then game.keypressed(key, key, false) end
  if game.keyreleased then game.keyreleased(key, key) end
end

local function toggleMenu()
  menu.open = not menu.open
  menu.confirmQuit = false
  if menu.open then menu.index = 1 end
  panel.visible = false
  panel.hideIn = nil
end

local function moveMenu(delta)
  menu.index = (menu.index - 1 + delta) % #MENU + 1
  menu.confirmQuit = false
end

local function activateMenu()
  local entry = MENU[menu.index]
  if entry.run == "close" then
    toggleMenu()
  elseif entry.run == "quit" then
    if not menu.confirmQuit then
      menu.confirmQuit = true
      return
    end
    print("[Webmon] quit from the menu")
    menu.open = false
    love.event.quit()
  elseif entry.key or entry.right then
    if entry.close then toggleMenu() end
    sendKey(entry.key or entry.right)
  end
end

local function sideMenu(direction)
  local entry = MENU[menu.index]
  local key = direction < 0 and entry.left or entry.right
  if key then sendKey(key) end
end

-- Frame timing for the log: frames and the slowest one since the last line,
-- and the time spent in the clean-ups below.
local perf = {
  frames = 0,
  worstMs = 0,
  collectMs = 0,
  collections = 0,
  since = nil,
  described = false, -- renderer, window and JIT logged once
}

local memory = {
  checkIn = 0,
  lastCollectMb = nil, -- process size right after the last full collection
  lastLowCollect = -math.huge,
  growthLimitMb = MEMORY_GROWTH_MB,             -- current growth trigger (adaptive)
  lowRepeatSeconds = MEMORY_LOW_REPEAT_SECONDS, -- current low-memory interval (adaptive)
  peakMb = 0,
  criticalCount = 0,
  restartIn = nil,          -- seconds left on the fresh-start countdown, when shown
  quitting = false,         -- a fresh start was requested; the game is shutting down
  snoozeUntil = -math.huge, -- no new countdown before this time
}

-- Quits the game cleanly and asks the launcher for a new process.
local function freshStart()
  memory.restartIn = nil
  memory.quitting = true
  print("[Webmon] memory: the TV is nearly out of memory; starting afresh")
  webmon.requestFreshStart()
  love.event.quit("restart")
end

local function cancelRestart(now)
  memory.restartIn = nil
  memory.criticalCount = 0
  memory.snoozeUntil = now + RESTART_SNOOZE_SECONDS
  print("[Webmon] memory: fresh start cancelled by the player")
end

local fonts = {}

-- Fonts are made on first use: the window may not exist yet when the bridge installs.
local function font(size)
  if not fonts[size] then
    fonts[size] = love.graphics.newFont(size)
  end
  return fonts[size]
end

-- Scales sizes from a 1080p layout to the current window.
local function scale()
  local _, height = love.graphics.getDimensions()
  return math.max(0.5, height / 1080)
end

local function formatSize(bytes)
  if not bytes or bytes <= 0 then return "" end
  if bytes >= 1024 * 1024 then return string.format("%.1f MB", bytes / (1024 * 1024)) end
  return string.format("%d KB", math.floor(bytes / 1024 + 0.5))
end

local function refreshPicker()
  local ok, files = pcall(webmon.files, LIST_KIND[picker.kind])
  picker.files = ok and files or {}
  if picker.index > #picker.files then picker.index = math.max(1, #picker.files) end
  picker.refreshIn = REFRESH_SECONDS
end

local function closePicker()
  picker.open = false
  picker.kind = nil
  webmon.setPendingPick(nil)
end

local function choose()
  local file = picker.files[picker.index]
  if not file then return end
  local path = webmon.readable(file.path)
  if path then
    picker.picked = path
  else
    picker.failure = "Webmon could not read " .. file.name .. " from the USB drive."
  end
  closePicker()
end

local function movePicker(delta)
  if #picker.files == 0 then return end
  picker.index = math.min(#picker.files, math.max(1, picker.index + delta))
end

-- ---- love.system bridge -------------------------------------------------------

local function installPicker()
  love.system.pickFileKinds = function()
    return "rom,sav,mod,cart,required_import"
  end
  love.system.pickFile = function(kind)
    if not KINDS[kind] then return false end
    picker.open = true
    picker.kind = kind
    picker.index = 1
    picker.picked = nil
    picker.failure = nil
    refreshPicker()
    webmon.setPendingPick(kind)
    return true
  end
  love.system.getPickedFile = function()
    local path = picker.picked
    picker.picked = nil
    return path
  end
  love.system.getPickError = function()
    local text = picker.failure
    picker.failure = nil
    return text
  end
end

-- ---- Drawing --------------------------------------------------------------------

local function drawBox(x, y, w, h)
  love.graphics.setColor(0.04, 0.06, 0.16, 0.94)
  love.graphics.rectangle("fill", x, y, w, h, 12, 12)
  love.graphics.setColor(1, 1, 1, 0.25)
  love.graphics.setLineWidth(2)
  love.graphics.rectangle("line", x, y, w, h, 12, 12)
end

local function serverLine()
  local address, pin = webmon.server()
  if address == "" then
    return "Upload page: connect the TV to your network"
  end
  return "Upload page: " .. address .. "   PIN " .. pin
end

local function drawPicker()
  local s = scale()
  local width, height = love.graphics.getDimensions()
  local w, h = math.floor(1300 * s), math.floor(760 * s)
  local x, y = math.floor((width - w) / 2), math.floor((height - h) / 2)
  local pad = math.floor(32 * s)
  drawBox(x, y, w, h)

  love.graphics.setFont(font(math.floor(40 * s)))
  love.graphics.setColor(1, 0.82, 0.24)
  love.graphics.print(KINDS[picker.kind] or "Choose a file", x + pad, y + pad)

  local rowFont = font(math.floor(28 * s))
  local smallFont = font(math.floor(22 * s))
  local rowHeight = math.floor(48 * s)
  local top = y + pad + math.floor(70 * s)

  if #picker.files == 0 then
    love.graphics.setFont(rowFont)
    love.graphics.setColor(0.92, 0.92, 0.96)
    love.graphics.printf("No files yet. Upload one from your phone or computer, or put it in "
      .. KIND_FOLDERS[picker.kind] .. " on a USB drive. This list refreshes by itself.",
      x + pad, top, w - pad * 2)
  else
    local first = math.max(1, math.min(picker.index - math.floor(VISIBLE_ROWS / 2), #picker.files - VISIBLE_ROWS + 1))
    for row = 0, VISIBLE_ROWS - 1 do
      local i = first + row
      local file = picker.files[i]
      if not file then break end
      local rowY = top + row * rowHeight
      if i == picker.index then
        love.graphics.setColor(0.35, 0.67, 1, 0.35)
        love.graphics.rectangle("fill", x + pad - 10, rowY - 4, w - pad * 2 + 20, rowHeight - 4, 8, 8)
      end
      love.graphics.setFont(rowFont)
      love.graphics.setColor(0.95, 0.95, 0.98)
      love.graphics.print(file.name, x + pad, rowY)
      love.graphics.setFont(smallFont)
      love.graphics.setColor(0.6, 0.63, 0.75)
      local info = file.source .. (file.size > 0 and ("   " .. formatSize(file.size)) or "")
      love.graphics.printf(info, x + pad, rowY + math.floor(6 * s), w - pad * 2, "right")
    end
  end

  love.graphics.setFont(smallFont)
  love.graphics.setColor(0.6, 0.63, 0.75)
  love.graphics.print(serverLine(), x + pad, y + h - pad - math.floor(64 * s))
  love.graphics.print("OK: choose    Back: cancel    Up/Down: move    Left/Right: page", x + pad,
    y + h - pad - math.floor(30 * s))
end

local function drawRestartNotice()
  local s = scale()
  local width, height = love.graphics.getDimensions()
  local lines = {
    "The TV is almost out of memory.",
    string.format("Webmon will restart the game in %d seconds to free it.", math.max(0, math.ceil(memory.restartIn))),
    "Unsaved progress is lost. A map cache build continues where it stopped",
    "(choose its prebuild again if the game asks).",
    "OK: restart now    Back: keep playing",
  }
  local titleFont = font(math.floor(34 * s))
  local textFont = font(math.floor(26 * s))
  local pad = math.floor(28 * s)
  local lineHeight = math.floor(38 * s)
  local w = math.floor(1100 * s)
  local h = pad * 2 + math.floor(48 * s) + (#lines - 1) * lineHeight
  local x, y = math.floor((width - w) / 2), math.floor((height - h) / 2)
  drawBox(x, y, w, h)
  love.graphics.setFont(titleFont)
  love.graphics.setColor(1, 0.55, 0.45)
  love.graphics.print(lines[1], x + pad, y + pad)
  love.graphics.setFont(textFont)
  for i = 2, #lines do
    love.graphics.setColor(i == #lines and 0.6 or 0.92, i == #lines and 0.63 or 0.92, i == #lines and 0.75 or 0.96)
    love.graphics.print(lines[i], x + pad, y + pad + math.floor(48 * s) + (i - 2) * lineHeight)
  end
end

local function drawMenu()
  local s = scale()
  local _, height = love.graphics.getDimensions()
  local status = webmon.status()
  local titleFont = font(math.floor(36 * s))
  local textFont = font(math.floor(24 * s))
  local rowFont = font(math.floor(26 * s))
  local pad = math.floor(24 * s)
  local rowHeight = math.floor(40 * s)
  local lineHeight = math.floor(30 * s)

  local info = { serverLine() }
  if status.restart then info[#info + 1] = "New files received: restart Webmon to use them" end
  info[#info + 1] = "github.com/pustal/Webmon"

  local w = math.floor(900 * s)
  local h = pad * 3 + math.floor(48 * s) + #info * lineHeight + #MENU * rowHeight + lineHeight
  -- On the left, so the effect of tilt, zoom and the like stays visible.
  local x, y = math.floor(40 * s), math.max(0, math.floor((height - h) / 2))
  drawBox(x, y, w, h)

  love.graphics.setFont(titleFont)
  love.graphics.setColor(1, 0.82, 0.24)
  love.graphics.print("Webmon", x + pad, y + pad)
  love.graphics.setFont(textFont)
  love.graphics.setColor(0.85, 0.86, 0.92)
  local top = y + pad + math.floor(48 * s)
  for i, line in ipairs(info) do love.graphics.print(line, x + pad, top + (i - 1) * lineHeight) end

  top = top + #info * lineHeight + pad
  for i, entry in ipairs(MENU) do
    local rowY = top + (i - 1) * rowHeight
    if i == menu.index then
      love.graphics.setColor(0.35, 0.67, 1, 0.35)
      love.graphics.rectangle("fill", x + pad - 10, rowY - 4, w - pad * 2 + 20, rowHeight - 4, 8, 8)
    end
    local label = entry.label
    if entry.run == "quit" and menu.confirmQuit and i == menu.index then
      label = "Press OK again to quit (unsaved progress is lost)"
      love.graphics.setColor(1, 0.55, 0.45)
    else
      love.graphics.setColor(0.95, 0.95, 0.98)
    end
    love.graphics.setFont(rowFont)
    love.graphics.print(label, x + pad, rowY)
  end

  love.graphics.setFont(textFont)
  love.graphics.setColor(0.6, 0.63, 0.75)
  love.graphics.print("OK/A: choose    Back/B, 1, Home: close    Left/Right: adjust", x + pad,
    top + #MENU * rowHeight + math.floor(4 * s))
end

local function drawPanel()
  local s = scale()
  local width = love.graphics.getDimensions()
  local status = webmon.status()
  local lines = { serverLine() }
  if status.usbDrive ~= "" then
    lines[#lines + 1] = "USB: " .. status.usbDrive .. "/Webmon"
      .. (status.gameDataOnUsb and " (game data stored there)" or " (game data on the TV)")
  else
    lines[#lines + 1] = "USB: no Webmon folder (create one on the upload page)"
  end
  if status.restart then
    lines[#lines + 1] = "New files received: restart Webmon to use them"
  end
  lines[#lines + 1] = "Remote: Back/Red = B, Green = A, Yellow = Select, Blue/0 = Start, CH+/CH- = L/R"
  lines[#lines + 1] = "Remote 1 or controller Home: Webmon menu (also quick save, tilt, zoom, quit)"

  local textFont = font(math.floor(24 * s))
  local pad = math.floor(18 * s)
  local lineHeight = math.floor(32 * s)
  local w = 0
  for _, line in ipairs(lines) do w = math.max(w, textFont:getWidth(line)) end
  w = w + pad * 2
  local h = #lines * lineHeight + pad * 2
  local x, y = width - w - math.floor(30 * s), math.floor(30 * s)
  drawBox(x, y, w, h)
  love.graphics.setFont(textFont)
  for i, line in ipairs(lines) do
    love.graphics.setColor(i == 1 and 1 or 0.85, i == 1 and 0.82 or 0.86, i == 1 and 0.24 or 0.92)
    love.graphics.print(line, x + pad, y + pad + (i - 1) * lineHeight)
  end
end

-- Draws the picker and/or panel over whatever the game drew, in screen space.
local function drawOverlays()
  if not picker.open and not panel.visible and not menu.open and not memory.restartIn then return end
  love.graphics.push("all")
  love.graphics.setCanvas()
  love.graphics.origin()
  love.graphics.setShader()
  love.graphics.setScissor()
  love.graphics.setBlendMode("alpha")
  if panel.visible then drawPanel() end
  if menu.open then drawMenu() end
  if picker.open then drawPicker() end
  if memory.restartIn then drawRestartNotice() end
  love.graphics.pop()
end

-- ---- Input ----------------------------------------------------------------------

-- Keys while the picker is open. Returns nothing: the game must not see them.
local function pickerKey(key)
  if key == "up" then movePicker(-1)
  elseif key == "down" then movePicker(1)
  elseif key == "pageup" or key == "left" then movePicker(-VISIBLE_ROWS)
  elseif key == "pagedown" or key == "right" then movePicker(VISIBLE_ROWS)
  elseif key == "return" or key == "kpenter" or key == "space" or key == "z" then choose()
  elseif key == "escape" or key == "backspace" or key == "x" then closePicker()
  end
end

local function pickerButton(button)
  if button == "dpup" then movePicker(-1)
  elseif button == "dpdown" then movePicker(1)
  elseif button == "dpleft" then movePicker(-VISIBLE_ROWS)
  elseif button == "dpright" then movePicker(VISIBLE_ROWS)
  elseif button == "a" then choose()
  elseif button == "b" or button == "back" then closePicker()
  end
end

-- Keys and buttons while the menu is open (the game sees none of them).
local function menuKey(key)
  if key == "up" then moveMenu(-1)
  elseif key == "down" then moveMenu(1)
  elseif key == "left" then sideMenu(-1)
  elseif key == "right" then sideMenu(1)
  elseif key == "return" or key == "kpenter" or key == "space" or key == "z" then activateMenu()
  elseif key == "escape" or key == "backspace" or key == "x" then toggleMenu()
  end
end

local function menuButton(button)
  if button == "dpup" then moveMenu(-1)
  elseif button == "dpdown" then moveMenu(1)
  elseif button == "dpleft" then sideMenu(-1)
  elseif button == "dpright" then sideMenu(1)
  elseif button == "a" then activateMenu()
  elseif button == "b" or button == "back" or button == "start" then toggleMenu()
  end
end

-- ---- Wrapping the game's callbacks -----------------------------------------------

-- Runs a full garbage collection when the process grew a lot or the TV runs low.
local function manageMemory(dt, now)
  memory.checkIn = memory.checkIn - dt
  if memory.checkIn > 0 then return end
  memory.checkIn = MEMORY_CHECK_SECONDS

  local processMb, availableMb = webmon.memory()
  if processMb <= 0 then return end
  memory.lastCollectMb = memory.lastCollectMb or processMb
  -- A full collection pauses the game (long on a big heap), so both triggers
  -- back off while collections free little: in steady 3D play the TV often
  -- sits under MEMORY_LOW_MB with nothing to free. Near the critical level
  -- the base interval always applies.
  local critical = availableMb > 0 and availableMb < MEMORY_CRITICAL_MB
  local lowInterval = critical and MEMORY_LOW_REPEAT_SECONDS or memory.lowRepeatSeconds
  local grew = processMb - memory.lastCollectMb >= memory.growthLimitMb
  local low = availableMb > 0 and availableMb < MEMORY_LOW_MB
    and now - memory.lastLowCollect >= lowInterval
  if grew or low then
    local started = love.timer.getTime()
    collectgarbage("collect")
    local pauseMs = (love.timer.getTime() - started) * 1000
    perf.collectMs = perf.collectMs + pauseMs
    perf.collections = perf.collections + 1
    local afterMb, afterAvailableMb = webmon.memory()
    local freedMb = processMb - afterMb
    memory.lastCollectMb = afterMb
    if low then memory.lastLowCollect = now end
    if freedMb < MEMORY_USEFUL_MB then
      if grew then memory.growthLimitMb = math.min(memory.growthLimitMb * 2, MEMORY_GROWTH_MAX_MB) end
      if low then memory.lowRepeatSeconds = math.min(memory.lowRepeatSeconds * 2, MEMORY_LOW_REPEAT_MAX_SECONDS) end
    else
      memory.growthLimitMb = MEMORY_GROWTH_MB
      memory.lowRepeatSeconds = MEMORY_LOW_REPEAT_SECONDS
    end
    print(string.format("[Webmon] memory: collected (%s) in %d ms, %d -> %d MB, TV available %d -> %d MB",
      low and "TV low" or "grew", math.floor(pauseMs + 0.5), processMb, afterMb, availableMb, afterAvailableMb))
    -- Still critical after collecting, several times in a row: offer a fresh start.
    if afterAvailableMb > 0 and afterAvailableMb < MEMORY_CRITICAL_MB then
      memory.criticalCount = memory.criticalCount + 1
    else
      memory.criticalCount = 0
    end
    if memory.criticalCount >= MEMORY_CRITICAL_TIMES and not memory.restartIn and not memory.quitting
        and now >= memory.snoozeUntil then
      memory.restartIn = RESTART_COUNTDOWN_SECONDS
      print(string.format("[Webmon] memory: TV still has only %d MB after collecting; fresh start in %d s",
        afterAvailableMb, RESTART_COUNTDOWN_SECONDS))
    end
  end
  if processMb >= memory.peakMb + 50 then
    memory.peakMb = processMb
    print(string.format("[Webmon] memory: new peak %d MB, TV available %d MB", processMb, availableMb))
  end
end

-- Logs once what the game runs on: the GPU driver, the window and drawing
-- size, and whether LuaJIT compiles (interpreted Lua is several times slower).
local function describeRenderer()
  perf.described = true
  local name, version, vendor, device = "?", "?", "?", "?"
  if love.graphics.getRendererInfo then
    local ok, a, b, c, d = pcall(love.graphics.getRendererInfo)
    if ok then name, version, vendor, device = tostring(a), tostring(b), tostring(c), tostring(d) end
  end
  local width, height = love.graphics.getDimensions()
  local pixelWidth, pixelHeight = width, height
  if love.graphics.getPixelDimensions then
    local w, h = love.graphics.getPixelDimensions()
    pixelWidth, pixelHeight = w or width, h or height
  end
  local jitText = "no LuaJIT"
  if jit then
    jitText = tostring(jit.version) .. " " .. tostring(jit.arch) .. ", JIT " .. (jit.status() and "on" or "OFF")
  end
  print(string.format("[Webmon] renderer: %s %s (%s, %s); window %dx%d, drawing %dx%d; %s",
    name, version, vendor, device, width, height, pixelWidth, pixelHeight, jitText))
end

-- Counts frames and logs the frame rate every PERF_LOG_SECONDS.
local function trackFrames(dt, now)
  if not perf.described then describeRenderer() end
  perf.since = perf.since or now
  perf.frames = perf.frames + 1
  perf.worstMs = math.max(perf.worstMs, dt * 1000)
  local elapsed = now - perf.since
  if elapsed < PERF_LOG_SECONDS then return end
  print(string.format("[Webmon] perf: %.1f fps, slowest frame %d ms, %d clean-ups took %d ms",
    perf.frames / elapsed, math.floor(perf.worstMs + 0.5), perf.collections, math.floor(perf.collectMs + 0.5)))
  perf.frames, perf.worstMs, perf.collectMs, perf.collections, perf.since = 0, 0, 0, 0, now
end

-- Per-frame work, from the love.graphics.present hook: timers, the panel key,
-- re-wrapping replaced callbacks, and drawing over the finished frame.
local lastFrame = nil
local wrapInput -- defined below

local function beforePresent()
  local now = love.timer and love.timer.getTime() or 0
  local dt = lastFrame and (now - lastFrame) or 0
  lastFrame = now

  if picker.open then
    picker.refreshIn = picker.refreshIn - dt
    if picker.refreshIn <= 0 then refreshPicker() end
  end
  if panel.hideIn then
    panel.hideIn = panel.hideIn - dt
    if panel.hideIn <= 0 then
      panel.visible = false
      panel.hideIn = nil
    end
  end
  trackFrames(dt, now)
  manageMemory(dt, now)
  if memory.restartIn then
    memory.restartIn = memory.restartIn - dt
    if memory.restartIn <= 0 then freshStart() end
  end
  if webmon.takePanelToggles() % 2 == 1 and not picker.open then
    toggleMenu()
  end
  wrapInput()
  drawOverlays()
end

-- Input callbacks: while the picker or the menu is open it takes the input,
-- otherwise the game's own function runs. game[name] is the game's current function.
local function overlayOpen() return picker.open or menu.open end
local wrappers = {
  keypressed = function(key, ...)
    if memory.restartIn then
      if key == "return" or key == "kpenter" or key == "z" then freshStart()
      elseif key == "escape" or key == "x" or key == "backspace" then cancelRestart(love.timer.getTime()) end
      return
    end
    if picker.open then return pickerKey(key) end
    if menu.open then return menuKey(key) end
    if game.keypressed then return game.keypressed(key, ...) end
  end,
  keyreleased = function(...)
    if overlayOpen() then return end
    if game.keyreleased then return game.keyreleased(...) end
  end,
  textinput = function(...)
    if overlayOpen() then return end
    if game.textinput then return game.textinput(...) end
  end,
  gamepadpressed = function(joystick, button, ...)
    if memory.restartIn then
      if button == "a" then freshStart()
      elseif button == "b" or button == "back" then cancelRestart(love.timer.getTime()) end
      return
    end
    if picker.open then return pickerButton(button) end
    if button == "guide" then return toggleMenu() end
    if menu.open then return menuButton(button) end
    if game.gamepadpressed then return game.gamepadpressed(joystick, button, ...) end
  end,
  gamepadreleased = function(...)
    if overlayOpen() then return end
    if game.gamepadreleased then return game.gamepadreleased(...) end
  end,
}

-- (Re-)installs the wrappers; a callback the game assigned since becomes game[name].
wrapInput = function()
  for name, wrapper in pairs(wrappers) do
    if love[name] ~= wrapper then
      game[name] = love[name]
      love[name] = wrapper
    end
  end
end

local function hookPresent()
  local present = love.graphics.present
  love.graphics.present = function(...)
    beforePresent()
    return present(...)
  end
end

-- ---- Installation ----------------------------------------------------------------

local installed = false
require = function(name, ...)
  if installed or name ~= "main" then
    return originalRequire(name, ...)
  end
  installed = true
  require = originalRequire
  if love.system then installPicker() end
  local result = originalRequire(name, ...)
  wrapInput()
  if love.graphics then hookPresent() end
  return result
end
