-- Behaviour test of src/lua/bridge.lua with stand-ins for LÖVE and the launcher.
-- Run: lua5.1 tools/bridge-test/test.lua src/lua/bridge.lua
-- Stand-ins for LÖVE and the launcher's webmon table.
memoryNow = { 100, 900 }
freshRequested = false
local quitWith = nil
local clock = 0
local pending = "unset"
webmon = {
  files = function(kind) return { { name = "A.gb", path = "/usb/A.gb", source = "USB", size = 1 },
                                  { name = "B.gb", path = "/usb/B.gb", source = "TV", size = 2 } } end,
  readable = function(p) return p == "/usb/B.gb" and "/copy/B.gb" or nil end,
  server = function() return "http://1.2.3.4:47811", "1234" end,
  setPendingPick = function(k) pending = k end,
  status = function() return { usbDrive = "", gameDataOnUsb = false, restart = false } end,
  takePanelToggles = function() return 0 end,
  memory = function() return memoryNow[1], memoryNow[2] end,
  requestFreshStart = function() freshRequested = true end,
}
-- Drawing calls do nothing; the few that return values return plausible ones.
local graphics = setmetatable({
  present = function() end,
  getDimensions = function() return 1920, 1080 end,
  newFont = function() return { getWidth = function(_, text) return #text * 10 end } end,
}, { __index = function() return function() end end })
love = { system = {}, keyboard = { hasTextInput = function() return false end },
         graphics = graphics, timer = { getTime = function() return clock end },
         event = { quit = function(how) quitWith = how end } }
local gameKeys = {}
package.preload.main = function()
  function love.keypressed(k) gameKeys[#gameKeys + 1] = k end
  function love.update(dt) end
  function love.draw() end
end
dofile(arg[1])
require("main")
assert(love.system.pickFileKinds() == "rom,sav,mod,cart")
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
print("bridge test ok")
