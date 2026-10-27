-- Webmon bridge (embedded in the webmon executable, run before LÖVE boots).
--
-- 1. File picker. gen1recomp asks platforms without a desktop file dialog for
--    a "native picker": love.system.pickFile(kind) opens it, then the game
--    polls love.system.getPickedFile() for the chosen path and
--    love.system.getPickError() for a failure. Webmon's picker is drawn over
--    the game and lists the files Webmon knows of that kind: TV storage
--    (uploaded through the upload page), the USB Webmon folder, and for ROMs
--    and saves RetroArch-style roms/ and saves/ folders on USB drives.
-- 2. Info panel. Remote key 1 shows or hides the upload page address and PIN
--    (also shown for a few seconds at start). The key is caught by the
--    launcher's SDL filter (webmon.takePanelToggles), so the game never sees it.
--
-- 3. Memory. LÖVE objects (meshes, image data, canvases) hold large native
--    buffers that Lua's garbage collector does not count, so a mod building
--    lots of them (a voxel world cache) can run the TV out of RAM long before
--    Lua thinks it is time to collect, and webOS then restarts the app. Once a
--    second the bridge checks the process size and runs a full collection
--    when it has grown a lot, or when the TV is running low.
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
}
local KIND_FOLDERS = {
  rom = "Webmon/roms (or, for Pokemon titles, a RetroArch roms folder)",
  sav = "Webmon/saves (or, for Pokemon titles, a RetroArch saves folder)",
  mod = "Webmon/mod-zips",
  cart = "Webmon/carts",
}

local PANEL_START_SECONDS = 10   -- info panel shown at start
local MEMORY_CHECK_SECONDS = 1   -- how often the process size is checked
local MEMORY_GROWTH_MB = 48      -- full collection after this much growth since the last one
local MEMORY_LOW_MB = 160        -- ... or when the TV has less than this available
local MEMORY_LOW_REPEAT_SECONDS = 5
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

local memory = {
  checkIn = 0,
  lastCollectMb = nil, -- process size right after the last full collection
  lastLowCollect = -math.huge,
  peakMb = 0,
}

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
  local ok, files = pcall(webmon.files, picker.kind)
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
    return "rom,sav,mod,cart"
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
  lines[#lines + 1] = "Exit: close Webmon    1: show or hide this panel"
  lines[#lines + 1] = "Help and updates: github.com/pustal/Webmon"

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
  if not picker.open and not panel.visible then return end
  love.graphics.push("all")
  love.graphics.setCanvas()
  love.graphics.origin()
  love.graphics.setShader()
  love.graphics.setScissor()
  love.graphics.setBlendMode("alpha")
  if panel.visible then drawPanel() end
  if picker.open then drawPicker() end
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

-- ---- Wrapping the game's callbacks -----------------------------------------------

-- Runs a full garbage collection when the process grew a lot or the TV runs low.
local function manageMemory(dt, now)
  memory.checkIn = memory.checkIn - dt
  if memory.checkIn > 0 then return end
  memory.checkIn = MEMORY_CHECK_SECONDS

  local processMb, availableMb = webmon.memory()
  if processMb <= 0 then return end
  memory.lastCollectMb = memory.lastCollectMb or processMb
  local grew = processMb - memory.lastCollectMb >= MEMORY_GROWTH_MB
  local low = availableMb > 0 and availableMb < MEMORY_LOW_MB
    and now - memory.lastLowCollect >= MEMORY_LOW_REPEAT_SECONDS
  if grew or low then
    collectgarbage("collect")
    local afterMb, afterAvailableMb = webmon.memory()
    memory.lastCollectMb = afterMb
    if low then memory.lastLowCollect = now end
    print(string.format("[Webmon] memory: collected (%s), %d -> %d MB, TV available %d -> %d MB",
      low and "TV low" or "grew", processMb, afterMb, availableMb, afterAvailableMb))
  end
  if processMb >= memory.peakMb + 50 then
    memory.peakMb = processMb
    print(string.format("[Webmon] memory: new peak %d MB, TV available %d MB", processMb, availableMb))
  end
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
  manageMemory(dt, now)
  if webmon.takePanelToggles() % 2 == 1 then
    panel.visible = not panel.visible
    panel.hideIn = nil
  end
  wrapInput()
  drawOverlays()
end

-- Input callbacks: while the picker is open it takes the input, otherwise the
-- game's own function runs. game[name] is the game's current function.
local game = {}
local wrappers = {
  keypressed = function(key, ...)
    if picker.open then return pickerKey(key) end
    if game.keypressed then return game.keypressed(key, ...) end
  end,
  keyreleased = function(...)
    if picker.open then return end
    if game.keyreleased then return game.keyreleased(...) end
  end,
  textinput = function(...)
    if picker.open then return end
    if game.textinput then return game.textinput(...) end
  end,
  gamepadpressed = function(joystick, button, ...)
    if picker.open then return pickerButton(button) end
    if game.gamepadpressed then return game.gamepadpressed(joystick, button, ...) end
  end,
  gamepadreleased = function(...)
    if picker.open then return end
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
