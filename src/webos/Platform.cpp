/**
 * @file Platform.cpp
 * @brief Implementation of Platform.h.
 */
#include "Platform.h"

#include <SDL.h>

#include <atomic>

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
constexpr int kScancodeWebOSChannelUp   = 480;
constexpr int kScancodeWebOSChannelDown = 481;
constexpr int kScancodeWebOSBack        = 482;
constexpr int kScancodeWebOSRed         = 486;
constexpr int kScancodeWebOSGreen       = 487;
constexpr int kScancodeWebOSYellow      = 488;
constexpr int kScancodeWebOSBlue        = 489;
constexpr int kScancodeWebOSExit        = 505;

/** A remote key and the keyboard key gen1recomp should see instead. */
struct KeyMapping {
    int remoteScancode;
    SDL_Scancode scancode;
    SDL_Keycode keycode;
};

/**
 * gen1recomp's keyboard layout (src/core/Input.lua): Enter/Z = A, X/Backspace
 * = B, Escape = Start, Tab = Select, Q/E = L/R. The remote's arrows and OK
 * already arrive as the arrow keys and Enter.
 */
constexpr KeyMapping kRemoteKeys[] = {
    { kScancodeWebOSBack, SDL_SCANCODE_X, SDLK_x },            // Back: B
    { kScancodeWebOSRed, SDL_SCANCODE_X, SDLK_x },             // Red: B
    { kScancodeWebOSGreen, SDL_SCANCODE_RETURN, SDLK_RETURN }, // Green: A
    { kScancodeWebOSYellow, SDL_SCANCODE_TAB, SDLK_TAB },      // Yellow: Select
    { kScancodeWebOSBlue, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE },  // Blue: Start
    { SDL_SCANCODE_AUDIOPLAY, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE }, // Play: Start
    { kScancodeWebOSChannelUp, SDL_SCANCODE_Q, SDLK_q },       // CH+: L
    { kScancodeWebOSChannelDown, SDL_SCANCODE_E, SDLK_e },     // CH-: R
};

/** Number of key presses of remote key 1 not yet handled by the Lua bridge. */
std::atomic<int> g_panelToggles{ 0 };

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

/** True for the remote's (and a keyboard's) 1 and 0 keys, which arrive as either row or keypad keys. */
bool IsDigit(int scancode, int digit)
{
    return digit == 1 ? (scancode == SDL_SCANCODE_1 || scancode == SDL_SCANCODE_KP_1)
                      : (scancode == SDL_SCANCODE_0 || scancode == SDL_SCANCODE_KP_0);
}

/**
 * SDL event filter, run before LÖVE (and so the game) sees an event:
 *   - Exit closes Webmon (turned into a quit event);
 *   - 1 toggles Webmon's info panel and is not passed on;
 *   - 0 is Start; the other remote keys follow kRemoteKeys.
 * Digits are left alone while the game takes text input (naming screens).
 * @return 1 to keep the (possibly rewritten) event, 0 to drop it.
 */
int RemoteEventFilter(void * /*userdata*/, SDL_Event *event)
{
    if (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP)
        return 1;
    const int scancode = event->key.keysym.scancode;
    const bool pressed = event->type == SDL_KEYDOWN;

    if (scancode == kScancodeWebOSExit) {
        if (!pressed)
            return 0;
        const Uint32 timestamp = event->key.timestamp;
        event->type            = SDL_QUIT;
        event->quit.timestamp  = timestamp;
        return 1;
    }

    const bool typing = SDL_IsTextInputActive() == SDL_TRUE;
    if (!typing && IsDigit(scancode, 1)) {
        if (pressed && !event->key.repeat)
            g_panelToggles.fetch_add(1);
        return 0;
    }
    if (!typing && IsDigit(scancode, 0)) {
        event->key.keysym.scancode = SDL_SCANCODE_ESCAPE;
        event->key.keysym.sym      = SDLK_ESCAPE;
        return 1;
    }
    for (const KeyMapping &mapping : kRemoteKeys) {
        if (scancode == mapping.remoteScancode) {
            event->key.keysym.scancode = mapping.scancode;
            event->key.keysym.sym      = mapping.keycode;
            break;
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

int TakePanelToggles()
{
    return g_panelToggles.exchange(0);
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
