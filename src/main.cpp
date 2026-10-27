/**
 * @file main.cpp
 * @brief Webmon: runs gen1recomp (a LÖVE game) on LG webOS TVs.
 *
 * Based on LÖVE 11.5's own main program (src/love.cpp, zlib licence,
 * Copyright (c) 2006-2024 LOVE Development Team), with webOS start-up added:
 *   1. enter the data folder, open the logs, point LÖVE's saves there;
 *   2. check root access, link the USB drive's Webmon folder and the game's
 *      mods folder (Library.h), start the upload page (WebServer.h);
 *   3. take over the remote's Back/Exit keys;
 *   4. run the player's gen1recomp .love if one is found, otherwise Webmon's
 *      setup screen (share/setup/, Lua), with the Lua bridge (LuaBridge.h)
 *      providing the game's file picker and the info panel.
 *
 * The game asks LÖVE to restart itself after imports and updates
 * (love.event.quit("restart")); each restart searches for the .love again,
 * so a newly added one is picked up.
 */
#include "server/WebServer.h"
#include "webos/Downloader.h"
#include "webos/Library.h"
#include "webos/LuaBridge.h"
#include "webos/Platform.h"
#include "webos/Usb.h"

#include "common/runtime.h"
#include "common/version.h"
#include "modules/love/love.h"

#include <SDL.h>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef WEBMON_VERSION
#define WEBMON_VERSION "dev"
#endif

namespace
{

/** How a LÖVE run ended. */
enum class DoneAction { Quit, Restart };

/** Registers @p function as package.preload[@p name]. */
void Preload(lua_State *L, lua_CFunction function, const char *name)
{
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "preload");
    lua_pushcfunction(L, function);
    lua_setfield(L, -2, name);
    lua_pop(L, 2);
}

/**
 * Runs one LÖVE session with @p args as the command line (args[0] is the
 * program, args[1] the game) and @p bridge's services available to Lua, and
 * returns how it ended; @p exitCode receives the game's exit status.
 */
DoneAction RunLove(const std::vector<std::string> &args, const webmon::BridgeContext &bridge, int &exitCode)
{
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    // LuaJIT set-up first, as LÖVE does: it is also loaded in LÖVE's threads.
    Preload(L, luaopen_love_jitsetup, "love.jitsetup");
    lua_getglobal(L, "require");
    lua_pushstring(L, "love.jitsetup");
    lua_call(L, 1, 0);

    Preload(L, luaopen_love, "love");

    // Webmon's Lua API and the file picker / info panel bridge.
    webmon::InstallLuaBridge(L, bridge);

    // Global arg, as in stand-alone Lua: arg[-2] program, arg[-1] boot script, arg[1..] the rest.
    lua_newtable(L);
    lua_pushstring(L, args[0].c_str());
    lua_rawseti(L, -2, -2);
    lua_pushstring(L, "embedded boot.lua");
    lua_rawseti(L, -2, -1);
    for (std::size_t i = 1; i < args.size(); ++i) {
        lua_pushstring(L, args[i].c_str());
        lua_rawseti(L, -2, static_cast<int>(i));
    }
    lua_setglobal(L, "arg");

    // require "love", and mark this as the stand-alone program (love._exe).
    lua_getglobal(L, "require");
    lua_pushstring(L, "love");
    lua_call(L, 1, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "_exe");
    lua_pop(L, 1);

    // Run love.boot as a coroutine until it finishes.
    lua_getglobal(L, "require");
    lua_pushstring(L, "love.boot");
    lua_call(L, 1, 1);
    lua_newthread(L);
    lua_pushvalue(L, -2);
    const int stackPosition = lua_gettop(L);
    int results             = 0;
    while (love::luax_resume(L, 0, &results) == LUA_YIELD)
        lua_pop(L, lua_gettop(L) - stackPosition);

    exitCode        = 0;
    DoneAction done = DoneAction::Quit;
    if (lua_type(L, -1) == LUA_TSTRING && std::strcmp(lua_tostring(L, -1), "restart") == 0)
        done = DoneAction::Restart;
    if (lua_isnumber(L, -1))
        exitCode = static_cast<int>(lua_tonumber(L, -1));

    lua_close(L);
    return done;
}

} // namespace

int main(int argc, char **argv)
{
    if (std::strcmp(LOVE_VERSION_STRING, love_version()) != 0) {
        std::fprintf(stderr, "[Webmon] LÖVE library %s does not match %s\n", love_version(), LOVE_VERSION_STRING);
        return 1;
    }

    const std::string dataFolder = webmon::PrepareDataFolder();

    // Services that live for the whole process, across the game's restarts.
    webmon::Usb usb;
    usb.ProbeRoot();
    webmon::Library library(dataFolder, usb);
    library.Prepare();
    webmon::Downloader downloader(library.IncomingFolder(), [&library](const std::string &temporary,
                                                                       const std::string &name) {
        return library.Place(temporary, library.UploadFolder(webmon::FileKind::Love, true), name);
    });
    webmon::WebServer server(library, usb, downloader, webmon::WebServer::LoadOrCreatePin(dataFolder),
                             WEBMON_VERSION);
    if (!server.Start())
        std::fprintf(stderr, "[Webmon] the upload page could not start (no free port)\n");
    const webmon::BridgeContext bridge{ &library, &server, &downloader };
    const std::string setupGame = webmon::ExecutableFolder() + "/../share/setup";

    int exitCode    = 0;
    DoneAction done = DoneAction::Quit;
    do {
        std::vector<std::string> args = { argc > 0 ? argv[0] : "webmon" };
        const std::string game        = library.NewestGame();
        if (!game.empty()) {
            std::printf("[Webmon] running %s\n", game.c_str());
            args.push_back(game);
        } else {
            // The setup screen gets the data folder so it can say where to put the game.
            std::printf("[Webmon] no .love found; showing the setup screen\n");
            args.push_back(setupGame);
            args.push_back("--webmon-data");
            args.push_back(dataFolder);
        }
        // Again on every run: LÖVE shuts SDL down between runs, which drops the event filter.
        webmon::InstallRemoteHandling();
        done = RunLove(args, bridge, exitCode);
    } while (done == DoneAction::Restart);

    server.Stop();
    return exitCode;
}
