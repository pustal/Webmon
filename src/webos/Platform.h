/**
 * @file Platform.h
 * @brief webOS start-up for Webmon: the app's data folder, logs, where LÖVE
 *        keeps saves, and the TV remote's Back/Exit keys.
 */
#ifndef WEBMON_PLATFORM_H
#define WEBMON_PLATFORM_H

#include <string>

namespace webmon
{

/**
 * Chooses and enters the app's data folder ($WEBMON_DATA_DIR, else $HOME/data,
 * else /tmp/webmon), creates its sub-folders, sends stdout/stderr to log files
 * there (unless $WEBMON_LOG_TO_CONSOLE is set) and points LÖVE's save
 * directory into it.
 * @return The data folder's absolute path.
 */
std::string PrepareDataFolder();

/**
 * Asks webOS to deliver the remote's Back and Exit keys to the app and maps
 * the remote onto gen1recomp's keyboard layout: Back/Red = B, OK/Green = A,
 * Yellow = Select, Blue/0/Play = Start, CH+/CH- = L/R, Exit closes Webmon,
 * 1 toggles Webmon's info panel. Call before LÖVE initialises SDL.
 */
void InstallRemoteHandling();

/** Presses of remote key 1 since the last call (the Lua bridge toggles its panel). */
int TakePanelToggles();

/**
 * Asks for the process to be replaced by a new one once LÖVE has quit (the
 * Lua bridge does this when the TV is about to run out of memory: a new
 * process hands back everything the old one held, which a LÖVE restart
 * inside the same process does not).
 */
void RequestFreshStart();

/** True after RequestFreshStart(). */
bool FreshStartRequested();

/**
 * Replaces this process with a new run of the same executable (@p argv as
 * given to main), whose logs continue the current ones. Returns only on failure.
 */
void FreshStart(char **argv);

/** Absolute path of the folder holding the running executable (bin/). */
std::string ExecutableFolder();

} // namespace webmon

#endif // WEBMON_PLATFORM_H
