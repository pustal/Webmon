-- Webmon setup screen: LÖVE configuration.
function love.conf(t)
  t.identity = "webmon-setup"
  t.version = "11.5"
  t.window.title = "Webmon"
  t.window.width = 1920
  t.window.height = 1080
  t.window.fullscreen = true
  t.window.vsync = 1
  t.modules.physics = false
end
