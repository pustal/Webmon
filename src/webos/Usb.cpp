/**
 * @file Usb.cpp
 * @brief Implementation of Usb.h (adapted from the Animal Crossing webOS port's UsbAccess).
 */
#include "Usb.h"

#include "Json.h"
#include "Process.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace webmon
{
namespace fs = std::filesystem;

namespace
{

/** Where webOS (and some other Linux setups) mount removable drives. */
constexpr std::array<const char *, 4> kUsbMountPrefixes = { "/tmp/usb/", "/media/usb", "/mnt/usb", "/run/media/" };
/** The Homebrew Channel's root command endpoint. */
constexpr const char *kHbChannelExec = "luna://org.webosbrew.hbchannel.service/exec";
/** luna-send-pub is the bus client apps may use; luna-send needs root. */
constexpr std::array<const char *, 2> kLunaClients = { "/usr/bin/luna-send-pub", "/usr/bin/luna-send" };
/** Appended to root commands to learn their own exit status through luna. */
constexpr const char *kOkMarker = "__WEBMON_OK__";

/** Runs @p command with /bin/sh (no root) and captures stdout. @return Exit status, or -1. */
int RunShell(const std::string &command, std::string *output)
{
    return RunProcess({ "/bin/sh", "-c", command }, output);
}

/** The first luna client this app may run, or empty. */
std::string LunaClient()
{
    for (const char *client : kLunaClients) {
        if (IsExecutable(client))
            return client;
    }
    return std::string();
}

/** Decodes the octal escapes /proc/mounts uses in paths ("\040" = space). */
std::string DecodeMountPath(const std::string &raw)
{
    std::string out;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 3 < raw.size()) {
            const std::string digits = raw.substr(i + 1, 3);
            if (std::all_of(digits.begin(), digits.end(), [](char d) { return d >= '0' && d <= '7'; })) {
                out += static_cast<char>(std::strtol(digits.c_str(), nullptr, 8));
                i += 3;
                continue;
            }
        }
        out += raw[i];
    }
    return out;
}

bool IsUsbMountPoint(const std::string &path)
{
    return std::any_of(kUsbMountPrefixes.begin(), kUsbMountPrefixes.end(),
                       [&](const char *prefix) { return path.rfind(prefix, 0) == 0; });
}

/** USB mount points (field 2) in the /proc/mounts text @p mounts. */
std::vector<std::string> ParseUsbMounts(const std::string &mounts)
{
    std::vector<std::string> found;
    std::istringstream lines(mounts);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string device;
        std::string mountPoint;
        if (!(fields >> device >> mountPoint))
            continue;
        const std::string decoded = DecodeMountPath(mountPoint);
        if (IsUsbMountPoint(decoded) && std::find(found.begin(), found.end(), decoded) == found.end())
            found.push_back(decoded);
    }
    return found;
}

} // namespace

std::string Usb::ShellQuote(const std::string &text)
{
    std::string out = "'";
    for (const char c : text) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    return out + "'";
}

bool Usb::CanRead(const std::string &dir)
{
    DIR *handle = opendir(dir.c_str());
    if (!handle)
        return false;
    closedir(handle);
    return true;
}

bool Usb::CanWrite(const std::string &dir)
{
    const std::string probe = dir + "/.webmon_write_test";
    const int fd            = open(probe.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0)
        return false;
    close(fd);
    unlink(probe.c_str());
    return true;
}

bool Usb::IsMountedAt(const std::string &path)
{
    std::ifstream mounts("/proc/mounts");
    std::string line;
    while (std::getline(mounts, line)) {
        std::istringstream fields(line);
        std::string device;
        std::string mountPoint;
        if (fields >> device >> mountPoint && DecodeMountPath(mountPoint) == path)
            return true;
    }
    return false;
}

void Usb::ProbeRoot()
{
    RootStatus status;
    status.runningAsRoot = geteuid() == 0;
    if (status.runningAsRoot) {
        status.detail = "The app runs as root.";
    } else if (const std::string luna = LunaClient(); luna.empty()) {
        status.detail = "No luna-send client available to this app.";
    } else {
        // Ask the Homebrew Channel to run "id -u" as root.
        std::string reply;
        RunProcess({ luna, "-n", "1", "-w", "5000", kHbChannelExec, "{\"command\":\"id -u\"}" }, &reply);
        std::string stdoutText;
        if (json::StringField(reply, "stdoutString", &stdoutText) && stdoutText.rfind("0", 0) == 0) {
            status.homebrewChannel = true;
            status.detail          = "Root commands run through the Homebrew Channel.";
        } else {
            std::string error;
            if (!json::StringField(reply, "errorText", &error))
                error = reply.substr(0, 160);
            while (!error.empty() && (error.back() == '\n' || error.back() == ' '))
                error.pop_back();
            status.detail = "Homebrew Channel root commands unavailable" + (error.empty() ? std::string(".") : ": " + error);
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    root_ = status;
    std::printf("[Webmon] root: %s\n", status.detail.c_str());
}

RootStatus Usb::Root() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return root_;
}

bool Usb::RunAsRoot(const std::string &command, std::string *output)
{
    const RootStatus status = Root();
    if (status.runningAsRoot)
        return RunShell(command, output) == 0;
    if (!status.homebrewChannel)
        return false;

    const std::string luna = LunaClient();
    if (luna.empty())
        return false;
    // The exec method reports success through returnValue; the marker tells us the command's own status.
    const std::string wrapped = "(" + command + ") && echo " + kOkMarker;
    std::string reply;
    RunProcess({ luna, "-n", "1", "-w", "60000", kHbChannelExec, "{\"command\":" + json::Quote(wrapped) + "}" },
               &reply);

    std::string stdoutText;
    if (!json::StringField(reply, "stdoutString", &stdoutText))
        return false;
    const std::size_t marker = stdoutText.rfind(kOkMarker);
    if (output)
        *output = stdoutText.substr(0, marker == std::string::npos ? stdoutText.size() : marker);
    return marker != std::string::npos;
}

std::vector<std::string> Usb::UsbMountPoints()
{
    std::string mounts;
    {
        std::ifstream file("/proc/mounts");
        std::ostringstream text;
        text << file.rdbuf();
        mounts = text.str();
    }
    std::vector<std::string> found = ParseUsbMounts(mounts);

    // Inside the jail /proc/mounts may not list the drives; root sees the real table.
    if (found.empty() && Root().Available()) {
        std::string rootMounts;
        if (RunAsRoot("cat /proc/mounts", &rootMounts))
            found = ParseUsbMounts(rootMounts);
    }
    return found;
}

std::vector<UsbDrive> Usb::ListDrives()
{
    std::vector<UsbDrive> drives;
    const bool root = Root().Available();
    for (const std::string &mountPoint : UsbMountPoints()) {
        UsbDrive drive;
        drive.mountPoint = mountPoint;
        drive.label      = fs::path(mountPoint).filename().string();
        drive.readable   = CanRead(mountPoint);
        drive.writable   = drive.readable && CanWrite(mountPoint);

        struct statvfs info {};
        if (statvfs(mountPoint.c_str(), &info) == 0 && info.f_blocks > 0) {
            drive.freeBytes  = static_cast<std::uint64_t>(info.f_bavail) * info.f_frsize;
            drive.totalBytes = static_cast<std::uint64_t>(info.f_blocks) * info.f_frsize;
            drive.spaceKnown = true;
        } else if (root) {
            // df -k prints: Filesystem 1K-blocks Used Available Use% Mounted-on
            std::string df;
            if (RunAsRoot("df -k " + ShellQuote(mountPoint) + " | tail -n 1", &df)) {
                std::istringstream fields(df);
                std::string filesystem;
                unsigned long long blocks = 0, used = 0, available = 0;
                if (fields >> filesystem >> blocks >> used >> available) {
                    drive.totalBytes = blocks * 1024ull;
                    drive.freeBytes  = available * 1024ull;
                    drive.spaceKnown = true;
                }
            }
        }

        const std::string webmonDir = mountPoint + "/" + kWebmonFolder;
        if (drive.readable) {
            struct stat st {};
            drive.hasWebmon = stat(webmonDir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        } else if (root) {
            drive.hasWebmon = RunAsRoot("test -d " + ShellQuote(webmonDir));
        }
        drives.push_back(drive);
    }
    return drives;
}

bool Usb::IsUsbMount(const std::string &mountPoint)
{
    const std::vector<std::string> mounts = UsbMountPoints();
    return std::find(mounts.begin(), mounts.end(), mountPoint) != mounts.end();
}

std::string Usb::CreateWebmonFolder(const std::string &mountPoint, const std::vector<std::string> &subFolders)
{
    const std::string base = mountPoint + "/" + kWebmonFolder;
    if (CanWrite(mountPoint)) {
        std::error_code ec;
        for (const std::string &sub : subFolders) {
            fs::create_directories(base + "/" + sub, ec);
            if (ec)
                return "Could not create the folder: " + ec.message();
        }
        fs::create_directories(base, ec);
        return ec ? "Could not create the folder: " + ec.message() : std::string();
    }
    if (Root().Available()) {
        std::string command = "mkdir -p " + ShellQuote(base);
        for (const std::string &sub : subFolders)
            command += " " + ShellQuote(base + "/" + sub);
        return RunAsRoot(command) ? std::string() : "Root could not create the folder on the drive.";
    }
    return "The app cannot write to this drive and has no root access.";
}

} // namespace webmon
