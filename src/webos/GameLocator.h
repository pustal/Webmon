/**
 * @file GameLocator.h
 * @brief Finds the gen1recomp .love file the player supplied.
 *
 * Webmon does not ship the game: the player adds the official release's
 * .love (gen1recomp's launcher may only be distributed in unmodified official
 * releases). Places searched, in order:
 *   1. <data>/game/ (any .love)
 *   2. <USB drive>/Webmon/ (any .love), for every mounted USB drive
 * When a folder holds several, the newest file wins.
 */
#ifndef WEBMON_GAME_LOCATOR_H
#define WEBMON_GAME_LOCATOR_H

#include <string>
#include <vector>

namespace webmon
{

/** Mount points of USB drives (from /proc/mounts, under /tmp/usb on webOS). */
std::vector<std::string> UsbMountPoints();

/** @return Absolute path of the .love to run, or empty if there is none. */
std::string FindGame(const std::string &dataFolder);

} // namespace webmon

#endif // WEBMON_GAME_LOCATOR_H
