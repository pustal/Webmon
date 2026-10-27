/**
 * @file LuaBridge.h
 * @brief Connects the running LÖVE game to Webmon.
 *
 * Install() adds a global "webmon" table of C functions (file lists, the
 * upload page address, downloads) and arranges for src/lua/bridge.lua to run
 * just before the game's main.lua. bridge.lua then provides the native file
 * picker gen1recomp looks for (love.system.pickFile / getPickedFile /
 * getPickError / pickFileKinds) and the info panel on remote key 1.
 */
#ifndef WEBMON_LUA_BRIDGE_H
#define WEBMON_LUA_BRIDGE_H

struct lua_State;

namespace webmon
{

class Downloader;
class Library;
class WebServer;

/** The objects the Lua functions use; they live for the whole process. */
struct BridgeContext {
    Library *library      = nullptr;
    WebServer *server     = nullptr;
    Downloader *downloader = nullptr;
};

/** Registers the webmon table and the bridge in @p L (before love.boot runs). */
void InstallLuaBridge(lua_State *L, const BridgeContext &context);

} // namespace webmon

#endif // WEBMON_LUA_BRIDGE_H
