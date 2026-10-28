/**
 * @file LuaBridge.cpp
 * @brief Implementation of LuaBridge.h.
 *
 * Lua API (global "webmon"):
 *   webmon.files(kind)         -> { {name=, path=, source=, size=}, ... }  kind: "love","rom","sav","mod","cart","n64"
 *   webmon.readable(path)      -> a path the game can open (a root copy if needed), or nil
 *   webmon.server()            -> address ("http://..." or ""), pin
 *   webmon.setPendingPick(k)   -> tells the upload page which file the game waits for (nil: none)
 *   webmon.status()            -> { usbDrive=, usbWritable=, gameDataOnUsb=, curl=, restart=,
 *                                   download={active=, succeeded=, file=, received=, total=, message=} }
 *   webmon.downloadLatest()    -> true if a download of the latest .love started
 *   webmon.dataFolder()        -> the app's data folder
 *   webmon.takePanelToggles()  -> presses of remote key 1 since the last call
 *   webmon.memory()            -> this process's MB in RAM, MB the TV still has available
 *   webmon.requestFreshStart() -> after the game quits, Webmon starts again as a new process
 */
#include "LuaBridge.h"

#include "BridgeScript.generated.h"
#include "Downloader.h"
#include "Library.h"
#include "Memory.h"
#include "Platform.h"
#include "server/WebServer.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <cstdio>

namespace webmon
{
namespace
{

BridgeContext g_context;

void SetString(lua_State *L, const char *key, const std::string &value)
{
    lua_pushlstring(L, value.data(), value.size());
    lua_setfield(L, -2, key);
}

void SetBool(lua_State *L, const char *key, bool value)
{
    lua_pushboolean(L, value ? 1 : 0);
    lua_setfield(L, -2, key);
}

void SetNumber(lua_State *L, const char *key, double value)
{
    lua_pushnumber(L, value);
    lua_setfield(L, -2, key);
}

int LuaFiles(lua_State *L)
{
    FileKind kind;
    if (!Library::ParseKind(luaL_checkstring(L, 1), &kind))
        return luaL_error(L, "webmon.files: unknown kind");
    const std::vector<LibraryFile> files = g_context.library->List(kind);
    lua_createtable(L, static_cast<int>(files.size()), 0);
    int index = 1;
    for (const LibraryFile &file : files) {
        lua_createtable(L, 0, 4);
        SetString(L, "name", file.name);
        SetString(L, "path", file.path);
        SetString(L, "source", file.source);
        SetNumber(L, "size", static_cast<double>(file.size));
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

int LuaReadable(lua_State *L)
{
    const std::string path = g_context.library->MakeReadable(luaL_checkstring(L, 1));
    if (path.empty())
        lua_pushnil(L);
    else
        lua_pushlstring(L, path.data(), path.size());
    return 1;
}

int LuaServer(lua_State *L)
{
    const std::string address = g_context.server ? g_context.server->Address() : std::string();
    const std::string pin     = g_context.server ? g_context.server->Pin() : std::string();
    lua_pushlstring(L, address.data(), address.size());
    lua_pushlstring(L, pin.data(), pin.size());
    return 2;
}

int LuaSetPendingPick(lua_State *L)
{
    if (g_context.server)
        g_context.server->SetPendingPick(lua_isstring(L, 1) ? lua_tostring(L, 1) : "");
    return 0;
}

int LuaStatus(lua_State *L)
{
    const DownloadStatus download = g_context.downloader->Status();
    lua_createtable(L, 0, 8);
    SetString(L, "usbDrive", g_context.library->ActiveUsbDrive());
    SetBool(L, "usbWritable", g_context.library->UsbWritable());
    SetBool(L, "gameDataOnUsb", g_context.library->GameDataOnUsb());
    SetBool(L, "curl", !Downloader::CurlPath().empty());
    SetBool(L, "restart", g_context.server && g_context.server->RestartSuggested());
    lua_createtable(L, 0, 6);
    SetBool(L, "active", download.active);
    SetBool(L, "succeeded", download.succeeded);
    SetString(L, "file", download.file);
    SetNumber(L, "received", static_cast<double>(download.received));
    SetNumber(L, "total", static_cast<double>(download.total));
    SetString(L, "message", download.message);
    lua_setfield(L, -2, "download");
    return 1;
}

int LuaDownloadLatest(lua_State *L)
{
    lua_pushboolean(L, g_context.downloader->StartLatest() ? 1 : 0);
    return 1;
}

int LuaDataFolder(lua_State *L)
{
    const std::string &folder = g_context.library->DataFolder();
    lua_pushlstring(L, folder.data(), folder.size());
    return 1;
}

int LuaTakePanelToggles(lua_State *L)
{
    lua_pushinteger(L, TakePanelToggles());
    return 1;
}

int LuaMemory(lua_State *L)
{
    const MemoryInfo memory = ReadMemory();
    lua_pushinteger(L, static_cast<lua_Integer>(memory.processMb));
    lua_pushinteger(L, static_cast<lua_Integer>(memory.availableMb));
    return 2;
}

int LuaRequestFreshStart(lua_State * /*L*/)
{
    RequestFreshStart();
    return 0;
}

/** Loads bridge.lua and leaves its install function on the stack. @return False on a syntax error. */
bool PushBridgeScript(lua_State *L)
{
    if (luaL_loadbuffer(L, kBridgeScript, sizeof(kBridgeScript) - 1, "=webmon/bridge.lua") != 0) {
        std::fprintf(stderr, "[Webmon] bridge.lua: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

} // namespace

void InstallLuaBridge(lua_State *L, const BridgeContext &context)
{
    g_context = context;

    static const luaL_Reg kFunctions[] = {
        { "files", LuaFiles },
        { "readable", LuaReadable },
        { "server", LuaServer },
        { "setPendingPick", LuaSetPendingPick },
        { "status", LuaStatus },
        { "downloadLatest", LuaDownloadLatest },
        { "dataFolder", LuaDataFolder },
        { "takePanelToggles", LuaTakePanelToggles },
        { "memory", LuaMemory },
        { "requestFreshStart", LuaRequestFreshStart },
        { nullptr, nullptr },
    };
    lua_newtable(L);
    luaL_register(L, nullptr, kFunctions);
    lua_setglobal(L, "webmon");

    // bridge.lua wraps the global require: it installs the picker and the info
    // panel when the game's (or the setup screen's) main.lua is required.
    if (PushBridgeScript(L)) {
        if (lua_pcall(L, 0, 0, 0) != 0) {
            std::fprintf(stderr, "[Webmon] bridge.lua: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
}

} // namespace webmon
