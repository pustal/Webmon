-- Webmon setup screen.
--
-- Shown when no gen1recomp .love has been found. It says where to put the
-- game and doubles as a check that LÖVE works on the TV: it lists the
-- renderer and the frame rate and draws a moving test pattern.
--
-- Arguments (from Webmon's launcher): --webmon-data <data folder>.

local CREDIT = "Based on the Pokemon Gen 1 Recompilation Project by BOIS CLUB GAMES, LLC "
  .. "(https://github.com/bryanthaboi/gen1recomp)"

local state = {
  dataFolder = "?",
  renderer = "?",
  joysticks = 0,
  time = 0,
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
end

function love.keypressed(key)
  -- Back / Exit on the remote arrive as Escape.
  if key == "escape" then
    love.event.quit()
  end
end

function love.gamepadpressed(_, button)
  if button == "back" or button == "b" then
    love.event.quit()
  end
end

-- Moving bars along the bottom: a visual check that frames keep coming.
local function drawTestPattern(width, height)
  local bars = 24
  local barWidth = width / bars
  for i = 0, bars - 1 do
    local phase = state.time * 2 + i * 0.4
    local level = 0.35 + 0.3 * math.sin(phase)
    love.graphics.setColor(0.2, level, 0.9 - level * 0.5)
    local barHeight = 60 + 50 * math.sin(phase * 1.3)
    love.graphics.rectangle("fill", i * barWidth + 4, height - barHeight - 40, barWidth - 8, barHeight)
  end
end

function love.draw()
  local width, height = love.graphics.getDimensions()
  love.graphics.clear(0.05, 0.07, 0.16)

  local x, y = 80, 70
  love.graphics.setColor(1, 0.82, 0.24)
  love.graphics.setFont(fonts.title)
  love.graphics.print("Webmon", x, y)
  y = y + 100

  love.graphics.setColor(0.92, 0.92, 0.96)
  love.graphics.setFont(fonts.text)
  local lines = {
    "No gen1recomp game file (.love) found yet.",
    "",
    "Put the official release's .love file in one of these places, then restart Webmon:",
    "    " .. state.dataFolder .. "/game/",
    "    a USB drive, in a folder named Webmon",
    "",
    "Download it from github.com/bryanthaboi/gen1recomp/releases",
    "(the file ending in .love). You also need your own ROM.",
  }
  for _, line in ipairs(lines) do
    love.graphics.print(line, x, y)
    y = y + 40
  end

  y = y + 30
  love.graphics.setColor(0.35, 0.67, 1)
  love.graphics.print("LOVE check", x, y)
  y = y + 44
  love.graphics.setColor(0.6, 0.63, 0.75)
  love.graphics.setFont(fonts.small)
  local major, minor, revision = love.getVersion()
  local info = {
    string.format("LOVE %d.%d.%d on %s, %s", major, minor, revision, love.system.getOS(), jit and jit.version or _VERSION),
    "Renderer: " .. state.renderer,
    string.format("Screen: %dx%d   Frame rate: %d fps   Controllers: %d", width, height,
      love.timer.getFPS(), state.joysticks),
  }
  for _, line in ipairs(info) do
    love.graphics.print(line, x, y)
    y = y + 32
  end

  love.graphics.setColor(0.6, 0.63, 0.75)
  love.graphics.print(CREDIT, x, height - 210)
  love.graphics.print("Back: quit", x, height - 175)

  drawTestPattern(width, height)
end
