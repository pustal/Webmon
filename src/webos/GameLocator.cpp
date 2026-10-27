/**
 * @file GameLocator.cpp
 * @brief Implementation of GameLocator.h.
 */
#include "GameLocator.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace webmon
{
namespace
{

/** Where webOS mounts USB drives. */
constexpr const char *kUsbMountRoot = "/tmp/usb/";
/** Folder on a USB drive that Webmon uses. */
constexpr const char *kUsbFolder = "Webmon";

/** True if @p name ends with ".love" (case-insensitive). */
bool IsLoveFile(const std::string &name)
{
    constexpr const char kSuffix[] = ".love";
    constexpr std::size_t kLength  = sizeof(kSuffix) - 1;
    if (name.size() <= kLength || name[0] == '.')
        return false;
    for (std::size_t i = 0; i < kLength; ++i) {
        const char c = name[name.size() - kLength + i];
        if ((c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c) != kSuffix[i])
            return false;
    }
    return true;
}

/** Newest .love file directly in @p folder, or empty. */
std::string NewestLoveIn(const std::string &folder)
{
    DIR *dir = opendir(folder.c_str());
    if (!dir)
        return std::string();
    std::string best;
    time_t bestTime = 0;
    while (const dirent *entry = readdir(dir)) {
        const std::string name(entry->d_name);
        if (!IsLoveFile(name))
            continue;
        const std::string path = folder + "/" + name;
        struct stat info;
        if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
            continue;
        if (best.empty() || info.st_mtime > bestTime) {
            best     = path;
            bestTime = info.st_mtime;
        }
    }
    closedir(dir);
    return best;
}

} // namespace

std::vector<std::string> UsbMountPoints()
{
    std::vector<std::string> mounts;
    std::ifstream file("/proc/mounts");
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        std::string device;
        std::string mountPoint;
        fields >> device >> mountPoint;
        if (mountPoint.rfind(kUsbMountRoot, 0) == 0)
            mounts.push_back(mountPoint);
    }
    return mounts;
}

std::string FindGame(const std::string &dataFolder)
{
    std::string found = NewestLoveIn(dataFolder + "/game");
    if (!found.empty())
        return found;
    for (const std::string &mount : UsbMountPoints()) {
        found = NewestLoveIn(mount + "/" + kUsbFolder);
        if (!found.empty())
            return found;
    }
    return std::string();
}

} // namespace webmon
