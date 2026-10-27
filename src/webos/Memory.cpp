/**
 * @file Memory.cpp
 * @brief Implementation of Memory.h.
 */
#include "Memory.h"

#include <unistd.h>

#include <fstream>
#include <sstream>
#include <string>

namespace webmon
{

MemoryInfo ReadMemory()
{
    MemoryInfo info;
    // statm: size resident shared ... (in pages)
    std::ifstream statm("/proc/self/statm");
    unsigned long long size = 0, resident = 0;
    if (statm >> size >> resident)
        info.processMb = static_cast<std::uint32_t>(resident * static_cast<unsigned long long>(sysconf(_SC_PAGESIZE)) / (1024 * 1024));

    // meminfo lines: "MemAvailable:   123456 kB"
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    unsigned long long freeKb = 0, cachedKb = 0, buffersKb = 0;
    bool haveAvailable = false;
    while (std::getline(meminfo, line)) {
        std::istringstream fields(line);
        std::string key;
        unsigned long long kb = 0;
        if (!(fields >> key >> kb))
            continue;
        if (key == "MemTotal:")
            info.totalMb = static_cast<std::uint32_t>(kb / 1024);
        else if (key == "MemAvailable:") {
            info.availableMb = static_cast<std::uint32_t>(kb / 1024);
            haveAvailable    = true;
        } else if (key == "MemFree:")
            freeKb = kb;
        else if (key == "Cached:")
            cachedKb = kb;
        else if (key == "Buffers:")
            buffersKb = kb;
        else if (key == "SwapFree:")
            info.swapFreeMb = static_cast<std::uint32_t>(kb / 1024);
    }
    if (!haveAvailable) // kernels before 3.14
        info.availableMb = static_cast<std::uint32_t>((freeKb + cachedKb + buffersKb) / 1024);
    return info;
}

} // namespace webmon
