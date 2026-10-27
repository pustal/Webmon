/**
 * @file Platform.cpp
 * @brief Implementation of Platform.h.
 */
#include "Platform.h"

#include <SDL.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

namespace webmon
{
namespace
{

/** Remote control scancodes from the SDL-webOS backport (SDL_SCANCODE_WEBOS_*). */
constexpr int kScancodeWebOSBack = 482;
constexpr int kScancodeWebOSExit = 505;

/** Used when neither $WEBMON_DATA_DIR nor $HOME/data can be created; lost at reboot. */
constexpr const char *kFallbackDataFolder = "/tmp/webmon";

/** Sub-folders of the data folder. */
constexpr const char *kSubFolders[] = {
    "game",  // the gen1recomp .love (uploaded, downloaded or copied)
    "save",  // LÖVE's save directory root (XDG_DATA_HOME)
};

/** Creates @p path if missing. @return True if it exists afterwards. */
bool EnsureFolder(const std::string &path)
{
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

/**
 * SDL event filter: the remote's Back and Exit keys arrive with webOS-only
 * scancodes that LÖVE does not know; turn them into Escape.
 * @return 1 to keep the (possibly rewritten) event.
 */
int RemoteEventFilter(void * /*userdata*/, SDL_Event *event)
{
    if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) {
        const int scancode = event->key.keysym.scancode;
        if (scancode == kScancodeWebOSBack || scancode == kScancodeWebOSExit) {
            event->key.keysym.scancode = SDL_SCANCODE_ESCAPE;
            event->key.keysym.sym      = SDLK_ESCAPE;
        }
    }
    return 1;
}

} // namespace

std::string PrepareDataFolder()
{
    std::string folder;
    if (const char *overridden = std::getenv("WEBMON_DATA_DIR"); overridden && *overridden) {
        folder = overridden;
    } else if (const char *home = std::getenv("HOME"); home && *home) {
        folder = std::string(home) + "/data";
    }
    if (folder.empty() || !EnsureFolder(folder)) {
        folder = kFallbackDataFolder;
        EnsureFolder(folder);
    }
    if (chdir(folder.c_str()) != 0)
        std::fprintf(stderr, "[Webmon] cannot enter %s\n", folder.c_str());

    for (const char *sub : kSubFolders)
        EnsureFolder(folder + "/" + sub);

    if (!std::getenv("WEBMON_LOG_TO_CONSOLE")) {
        if (!std::freopen("stdout.log", "w", stdout) || !std::freopen("stderr.log", "w", stderr))
            std::fprintf(stderr, "[Webmon] cannot open the log files\n");
        // Line-buffer so a crash still leaves the last lines in the logs.
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
        std::setvbuf(stderr, nullptr, _IOLBF, 0);
    }

    // LÖVE on Linux keeps saves in $XDG_DATA_HOME/love/<identity>.
    const std::string saveRoot = folder + "/save";
    setenv("XDG_DATA_HOME", saveRoot.c_str(), 1);

    std::printf("[Webmon] data folder %s\n", folder.c_str());
    return folder;
}

void InstallRemoteHandling()
{
    SDL_SetHint("SDL_WEBOS_ACCESS_POLICY_KEYS_BACK", "true");
    SDL_SetHint("SDL_WEBOS_ACCESS_POLICY_KEYS_EXIT", "true");
    SDL_SetHint("SDL_WEBOS_CURSOR_SLEEP_TIME", "1000");
    SDL_SetEventFilter(RemoteEventFilter, nullptr);
}

std::string ExecutableFolder()
{
    char path[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length <= 0)
        return ".";
    path[length] = '\0';
    std::string result(path);
    const std::size_t slash = result.rfind('/');
    return slash == std::string::npos ? "." : result.substr(0, slash);
}

} // namespace webmon
