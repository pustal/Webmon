/**
 * @file Memory.h
 * @brief Memory figures for the log, the upload page and the Lua bridge's
 *        garbage collection (webOS restarts the app when the TV runs short).
 */
#ifndef WEBMON_MEMORY_H
#define WEBMON_MEMORY_H

#include <cstdint>

namespace webmon
{

/** Current memory use, in MB. */
struct MemoryInfo {
    std::uint32_t processMb   = 0; ///< This process's resident memory (RSS).
    std::uint32_t availableMb = 0; ///< Memory the system could still hand out (MemAvailable).
    std::uint32_t totalMb     = 0; ///< The TV's RAM as the kernel sees it.
    std::uint32_t swapFreeMb  = 0; ///< Free swap space.
};

/** Reads /proc/self/statm and /proc/meminfo. Fields stay 0 when unreadable. */
MemoryInfo ReadMemory();

} // namespace webmon

#endif // WEBMON_MEMORY_H
