-- Behaviour test of src/lua/bridge.lua with stand-ins for LÖVE and the launcher.
-- Run: lua5.1 tools/bridge-test/test.lua src/lua/bridge.lua
-- Stand-ins for LÖVE and the launcher's webmon table.
local pending = "unset"
webmon = {
  files = function(kind) return { { name = "A.gb", path = "/usb/A.gb", source = "USB", size = 1 },
                                  { name = "B.gb", path = "/usb/B.gb", source = "TV", size = 2 } } end,
  readable = function(p) return p == "/usb/B.gb" and "/copy/B.gb" or nil end,
  server = function() return "http://1.2.3.4:47811", "1234" end,
  setPendingPick = function(k) pending = k end,
  status = function() return { usbDrive = "", modsOnUsb = false, restart = false } end,
}
love = { system = {}, keyboard = { hasTextInput = function() return false end } }
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
love.keypressed("1")             -- panel toggle, not for the game
love.keypressed("x")
assert(#gameKeys == 1 and gameKeys[1] == "x")
assert(love.system.pickFile("rom")); love.keypressed("return")   -- A.gb is unreadable
assert(love.system.getPickedFile() == nil and love.system.getPickError():find("A.gb"))
assert(love.system.pickFile("bogus") == false)
print("bridge test ok")
