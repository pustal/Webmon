-- Behaviour test of src/lua/bridge.lua with stand-ins for LÖVE and the launcher.
-- Run: lua5.1 tools/bridge-test/test.lua src/lua/bridge.lua
-- Stand-ins for LÖVE and the launcher's webmon table.
memoryNow = { 100, 900 }
listed = nil
freshRequested = false
local quitWith = nil
local clock = 0
local pending = "unset"
webmon = {
  files = function(kind) listed = kind; return { { name = "A.gb", path = "/usb/A.gb", source = "USB", size = 1 },
                                  { name = "B.gb", path = "/usb/B.gb", source = "TV", size = 2 } } end,
  readable = function(p) return p == "/usb/B.gb" and "/copy/B.gb" or nil end,
  server = function() return "http://1.2.3.4:47811", "1234" end,
  setPendingPick = function(k) pending = k end,
  status = function() return { usbDrive = "", gameDataOnUsb = false, restart = false } end,
  takePanelToggles = function() return 0 end,
  memory = function() return memoryNow[1], memoryNow[2] end,
  requestFreshStart = function() freshRequested = true end,
  dataFolder = function() return testFolder end,
}
-- The data folder for the compiler switch; LÖVE starts with the compiler off on ARM.
testFolder = os.getenv("TMPDIR") or "/tmp"
os.remove(testFolder .. "/jit-off")
if jit then jit.off() end
-- Drawing calls do nothing; the few that return values return plausible ones.
local graphics = setmetatable({
  present = function() end,
  getDimensions = function() return 1920, 1080 end,
  newFont = function() return { getWidth = function(_, text) return #text * 10 end } end,
}, { __index = function() return function() end end })
love = { system = {}, keyboard = { hasTextInput = function() return false end },
         graphics = graphics, timer = { getTime = function() return clock end },
         event = { quit = function(how) quitWith = how; quitCalled = true end } }
local gameKeys = {}
package.preload.main = function()
  function love.keypressed(k) gameKeys[#gameKeys + 1] = k end
  function love.update(dt) end
  function love.draw() end
end
dofile(arg[1])
require("main")
if jit then assert(jit.status(), "the bridge switches the compiler back on") end
assert(love.system.pickFileKinds() == "rom,sav,mod,cart,required_import")
assert(love.system.pickFile("rom") == true and pending == "rom")
love.keypressed("down")          -- to B.gb
love.keypressed("return")        -- choose
assert(love.system.getPickedFile() == "/copy/B.gb", "picked path")
assert(love.system.getPickedFile() == nil, "consumed once")
assert(pending == nil, "page told the pick is over")
assert(#gameKeys == 0, "game must not see picker keys")
love.keypressed("x")
assert(#gameKeys == 1 and gameKeys[1] == "x")
-- The game replacing its callback must not lose the picker's input.
love.keypressed = function(k) gameKeys[#gameKeys + 1] = "new:" .. k end
love.graphics.present()          -- re-wraps
assert(love.system.pickFile("rom")); love.keypressed("x")      -- cancels, game sees nothing
assert(#gameKeys == 1 and love.system.getPickedFile() == nil)
love.keypressed("up")
assert(gameKeys[2] == "new:up", "the game's new callback runs")
assert(love.system.pickFile("rom")); love.keypressed("return")   -- A.gb is unreadable
assert(love.system.getPickedFile() == nil and love.system.getPickError():find("A.gb"))
assert(love.system.pickFile("bogus") == false)
-- A mod's required import (Stadium 2 importer) is picked from the N64 ROMs.
assert(love.system.pickFile("required_import") and listed == "n64"); love.keypressed("x")
-- Menu: the controller's Home button opens it; the game sees no menu input,
-- only the shortcut keys the menu sends.
local before = #gameKeys
love.gamepadpressed(nil, "guide")
love.keypressed("down"); love.keypressed("down"); love.keypressed("down")   -- Tilt
love.keypressed("return")
assert(gameKeys[before + 1] == "new:3" and #gameKeys == before + 1, "tilt sent as key 3")
love.keypressed("down"); love.keypressed("down"); love.keypressed("down")   -- Zoom out / in
love.keypressed("left"); love.keypressed("right")
assert(gameKeys[before + 2] == "new:-" and gameKeys[before + 3] == "new:=", "zoom keys")
love.keypressed("x")                                  -- Back closes
love.keypressed("up")
assert(gameKeys[#gameKeys] == "new:up", "menu closed, game has the input again")
if jit then
  -- Menu entry "Lua compiler": off, remembered, and on again.
  love.gamepadpressed(nil, "guide"); love.keypressed("up"); love.keypressed("up")
  love.keypressed("return")
  assert(not jit.status() and io.open(testFolder .. "/jit-off"), "compiler off and remembered")
  love.keypressed("return")
  assert(jit.status() and not io.open(testFolder .. "/jit-off"), "compiler on again")
  love.keypressed("x")
end
love.gamepadpressed(nil, "guide"); love.keypressed("up")   -- opens at the top; up wraps to Quit
love.keypressed("return")
assert(quitWith == nil, "first OK only asks")
love.keypressed("return")
assert(quitWith == nil and quitCalled, "second OK quits")
quitCalled = false
-- Remote 1 (through the launcher) toggles the menu too.
local toggles = 1
webmon.takePanelToggles = function() local n = toggles; toggles = 0; return n end
love.graphics.present()
love.keypressed("down"); love.keypressed("return")   -- Quick save, closes the menu
assert(gameKeys[#gameKeys] == "new:f1", "quick save sent as F1")
love.keypressed("down")
assert(gameKeys[#gameKeys] == "new:down", "menu closed after quick save")
-- Memory: critically low after collecting, three times in a row, starts a countdown;
-- Back cancels it, and once it runs out the game quits for a fresh start.
local function frame(seconds) clock = clock + seconds; love.graphics.present() end
memoryNow = { 900, 100 }
for _ = 1, 4 do frame(6) end
love.keypressed("x")                          -- Back: keep playing
assert(not freshRequested and quitWith == nil, "cancelled")
clock = clock + 130                           -- after the snooze
for _ = 1, 4 do frame(6) end
for _ = 1, 20 do frame(1) end                 -- countdown runs out
assert(freshRequested and quitWith == "restart", "fresh start requested")
-- A model of Lua's collector for the clean-up tests: each "step" costs 6 ms,
-- a cycle ends every 75 steps (450 ms of work), a full "collect" costs 900 ms.
-- Finished clean-ups are counted from the log.
local realCollect, realPrint = collectgarbage, print
local finished, fullCollects, steps, freeOnCycle = 0, 0, 0, 0
print = function(text, ...)
  if type(text) == "string" and text:find("memory: collected", 1, true) then finished = finished + 1 end
  return realPrint(text, ...)
end
collectgarbage = function(what, ...)
  if what == "step" then
    steps = steps + 1
    clock = clock + 0.006
    if steps % 75 == 0 then memoryNow[1] = memoryNow[1] - freeOnCycle; return true end
    return false
  elseif what == "collect" then
    fullCollects = fullCollects + 1
    clock = clock + 0.9
    memoryNow[1] = memoryNow[1] - 2 * freeOnCycle
    return 0
  end
  return realCollect(what, ...)
end
local function reset() finished, fullCollects = 0, 0 end

-- Low (not critical) memory that clean-ups cannot free: they back off
-- (5, 10, 20, 40 s ...) instead of running every 5 seconds.
reset(); freeOnCycle = 0
memoryNow = { 900, 150 }
for _ = 1, 1200 do frame(0.1) end
assert(finished >= 1 and finished <= 7, "backed off (24 without): " .. finished .. " clean-ups in 2 minutes")
assert(fullCollects == 0, "spread over frames, never all at once")

-- A voxel town: ~100 MB of garbage a second, plenty of TV memory. Clean-ups
-- are spread over frames (the game never stops for one) and kept to the time budget.
reset(); freeOnCycle = 50
memoryNow = { 450, 400 }
for _ = 1, 600 do
  memoryNow[1] = memoryNow[1] + 10
  frame(0.1)
end
assert(fullCollects == 0, "town: no stop-the-world clean-up, got " .. fullCollects)
assert(finished >= 1 and finished <= 6, "town: time budget, " .. finished .. " clean-ups in a minute")

-- Under the emergency level: a full clean-up at once, every few seconds.
reset(); freeOnCycle = 0
memoryNow = { 900, 60 }
for _ = 1, 30 do frame(1) end
assert(fullCollects >= 5, "emergency: clean-ups every few seconds, got " .. fullCollects)

-- Near the critical level with memory bouncing straight back after each
-- clean-up (the game re-creates what was freed): the interval backs off
-- instead of freezing the game every few seconds, and the fresh start is offered.
reset(); freeOnCycle = 0
memoryNow = { 800, 117 }
for _ = 1, 60 do frame(1) end
assert(fullCollects >= 2 and fullCollects <= 6, "rebound: backed off, got " .. fullCollects .. " in a minute (12 without)")
-- (The fresh start this offers is exercised by the countdown test above; the
-- game has already quit once here, and Webmon offers none while quitting.)
collectgarbage, print = realCollect, realPrint
print("bridge test ok")
