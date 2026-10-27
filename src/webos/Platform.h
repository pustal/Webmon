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
 * Asks webOS to deliver the remote's Back and Exit keys to the app, and turns
 * them into Escape for the game (gen1recomp uses Escape for back/menu).
 * Call before LÖVE initialises SDL.
 */
void InstallRemoteHandling();

/** Absolute path of the folder holding the running executable (bin/). */
std::string ExecutableFolder();

} // namespace webmon

#endif // WEBMON_PLATFORM_H
