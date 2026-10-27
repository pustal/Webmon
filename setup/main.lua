-- Webmon setup screen.
--
-- Shown when no gen1recomp .love has been found. It offers to download the
-- latest release straight onto the TV (OK), shows the upload page address
-- and PIN, and doubles as a check that LÖVE works on the TV (renderer, frame
-- rate, a moving test pattern). When a download finishes, Webmon restarts
-- into the game.
--
-- Uses the global "webmon" table provided by the launcher (LuaBridge.cpp).
-- Arguments (from the launcher): --webmon-data <data folder>.

local CREDIT = "Based on the Pokemon Gen 1 Recompilation Project by BOIS CLUB GAMES, LLC "
  .. "(https://github.com/bryanthaboi/gen1recomp)"

local state = {
  dataFolder = "?",
  renderer = "?",
  joysticks = 0,
  time = 0,
  restartIn = nil,   -- seconds until the restart after a finished download
  sawDownload = false, -- a download ran while this screen was up
}

local fonts = {}

-- Reads "--webmon-data <folder>" from the command line.
local function readArguments(args)
  for i = 1, #args do
    if args[i] == "--webmon-data" and args[i + 1] then
      state.dataFolder = args[i + 1]
    end
  end
end

local function formatMegabytes(bytes)
  return string.format("%.1f MB", (bytes or 0) / (1024 * 1024))
end

local function startDownload()
  local status = webmon.status()
  if status.curl and not status.download.active then
    webmon.downloadLatest()
  end
end

function love.load(args)
  readArguments(args or {})
  local name, version, vendor, device = love.graphics.getRendererInfo()
  state.renderer = string.format("%s %s - %s %s", name, version, vendor, device)
  fonts.title = love.graphics.newFont(56)
  fonts.text = love.graphics.newFont(28)
  fonts.small = love.graphics.newFont(22)
  print("[Webmon setup] renderer: " .. state.renderer)
end

function love.update(dt)
  state.time = state.time + dt
  state.joysticks = love.joystick.getJoystickCount()
  local download = webmon.status().download
  -- Restart only for a download this screen watched: a success left over from
  -- before a restart must not cause another one.
  if download.active then
    state.sawDownload = true
  end
  if download.succeeded and state.sawDownload and not state.restartIn then
    state.restartIn = 2
  end
  if state.restartIn then
    state.restartIn = state.restartIn - dt
    if state.restartIn <= 0 then
      love.event.quit("restart") -- the launcher finds the new .love
    end
  end
end

function love.keypressed(key)
  -- Back / Exit on the remote arrive as Escape.
  if key == "escape" then
    love.event.quit()
  elseif key == "return" or key == "kpenter" then
    startDownload()
  end
end

function love.gamepadpressed(_, button)
  if button == "back" or button == "b" then
    love.event.quit()
  elseif button == "a" then
    startDownload()
  end
end

-- Moving bars along the bottom: a visual check that frames keep coming.
local function drawTestPattern(width, height)
  local bars = 24
  local barWidth = width / bars
  for i = 0, bars - 1 do
    local phase = state.time * 2 + i * 0.4
    local level = 0.35 + 0.3 * math.sin(phase)
    love.graphics.setColor(0.75, level * 0.4, level * 0.3)
    local barHeight = 40 + 30 * math.sin(phase * 1.3)
    love.graphics.rectangle("fill", i * barWidth + 4, height - barHeight - 30, barWidth - 8, barHeight)
  end
end

-- The lines about getting the game, depending on curl and the download's progress.
local function downloadLines(status)
  local download = status.download
  if download.active then
    local progress = download.total > 0
      and string.format(" (%s of %s)", formatMegabytes(download.received), formatMegabytes(download.total))
      or string.format(" (%s)", formatMegabytes(download.received))
    return { download.message .. progress }
  end
  if download.succeeded then
    return { download.message, "Starting the game..." }
  end
  local lines = {}
  if download.message ~= "" then
    lines[#lines + 1] = download.message
  end
  if status.curl then
    lines[#lines + 1] = "Press OK to download the latest gen1recomp release onto the TV."
  else
    lines[#lines + 1] = "This TV cannot download by itself: upload the .love from the page below."
  end
  return lines
end

function love.draw()
  local width, height = love.graphics.getDimensions()
  love.graphics.clear(0.10, 0.05, 0.05)
  local status = webmon.status()
  local address, pin = webmon.server()

  local x, y = 80, 60
  love.graphics.setColor(1, 0.82, 0.24)
  love.graphics.setFont(fonts.title)
  love.graphics.print("Webmon", x, y)
  y = y + 95

  love.graphics.setColor(0.95, 0.92, 0.92)
  love.graphics.setFont(fonts.text)
  love.graphics.print("The game file (gen1recomp .love) is not on this TV yet.", x, y)
  y = y + 56

  love.graphics.setColor(1, 0.82, 0.24)
  for _, line in ipairs(downloadLines(status)) do
    love.graphics.print(line, x, y)
    y = y + 40
  end
  y = y + 24

  love.graphics.setColor(0.95, 0.92, 0.92)
  local lines = {
    address ~= "" and ("Or upload it (and your ROMs) from a phone or computer: " .. address .. "   PIN " .. pin)
      or "Connect the TV to your network to use the upload page.",
    "Or copy it into a folder named Webmon on a USB drive, then restart Webmon.",
  }
  for _, line in ipairs(lines) do
    love.graphics.print(line, x, y)
    y = y + 40
  end

  y = y + 30
  love.graphics.setColor(0.9, 0.55, 0.5)
  love.graphics.print("TV check", x, y)
  y = y + 44
  love.graphics.setColor(0.75, 0.68, 0.68)
  love.graphics.setFont(fonts.small)
  local major, minor, revision = love.getVersion()
  local info = {
    string.format("LOVE %d.%d.%d on %s, %s", major, minor, revision, love.system.getOS(), jit and jit.version or _VERSION),
    "Renderer: " .. state.renderer,
    string.format("Screen: %dx%d   Frame rate: %d fps   Controllers: %d", width, height,
      love.timer.getFPS(), state.joysticks),
    "Data folder: " .. state.dataFolder,
    "USB: " .. (status.usbDrive ~= "" and (status.usbDrive .. "/Webmon") or "no Webmon folder (create one on the upload page)"),
  }
  for _, line in ipairs(info) do
    love.graphics.print(line, x, y)
    y = y + 32
  end

  love.graphics.setColor(0.75, 0.68, 0.68)
  love.graphics.print(CREDIT, x, height - 170)
  love.graphics.print("OK: download   Back: quit", x, height - 135)

  drawTestPattern(width, height)
end
