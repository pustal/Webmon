/**
 * @file Usb.h
 * @brief Root access and USB drives, as in the Animal Crossing webOS port.
 *
 * Root: either the app runs as root, or the Homebrew Channel runs commands as
 * root for it (luna://org.webosbrew.hbchannel.service/exec through
 * luna-send-pub). Root is needed when the app's jail cannot see or read a USB
 * drive.
 *
 * Drives: everything mounted under /tmp/usb/ (or the other usual places). For
 * each one Webmon notes free space, whether the app can read and write it
 * directly, and whether it has a "Webmon" folder.
 */
#ifndef WEBMON_USB_H
#define WEBMON_USB_H

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace webmon
{

/** How (and whether) the app can run commands as root. */
struct RootStatus {
    bool runningAsRoot   = false; ///< The app process itself is root.
    bool homebrewChannel = false; ///< Homebrew Channel exec works from this app.
    std::string detail;           ///< Short explanation for the page.

    bool Available() const { return runningAsRoot || homebrewChannel; }
};

/** One mounted USB drive. */
struct UsbDrive {
    std::string mountPoint;      ///< e.g. /tmp/usb/sda/sda1
    std::string label;           ///< Short name for people, e.g. "sda1"
    std::uint64_t freeBytes  = 0;
    std::uint64_t totalBytes = 0;
    bool spaceKnown = false;     ///< statvfs (or root df) succeeded
    bool readable   = false;     ///< The app can list it directly
    bool writable   = false;     ///< The app can create files on it directly
    bool hasWebmon  = false;     ///< "<drive>/Webmon" exists
};

class Usb
{
public:
    /** Folder Webmon keeps on a USB drive. */
    static constexpr const char *kWebmonFolder = "Webmon";

    /** Finds out whether root commands are possible (may take a few seconds). */
    void ProbeRoot();

    /** The result of the last ProbeRoot(). */
    RootStatus Root() const;

    /** Mounted USB drives (asks root when the jail's /proc/mounts shows none). */
    std::vector<UsbDrive> ListDrives();

    /** True if @p mountPoint is one of the mounted USB drives. */
    bool IsUsbMount(const std::string &mountPoint);

    /**
     * Creates "<drive>/Webmon" and its sub-folders @p subFolders, directly or
     * with root. @return Empty on success, otherwise a message for the page.
     */
    std::string CreateWebmonFolder(const std::string &mountPoint, const std::vector<std::string> &subFolders);

    /**
     * Runs @p command with /bin/sh as root (directly or through the Homebrew
     * Channel). @p output receives stdout. @return True if it exited with 0.
     */
    bool RunAsRoot(const std::string &command, std::string *output = nullptr);

    /** @p text quoted for /bin/sh. */
    static std::string ShellQuote(const std::string &text);

    /** True if the app can list @p dir. */
    static bool CanRead(const std::string &dir);

    /** True if the app can create a file in @p dir. */
    static bool CanWrite(const std::string &dir);

    /** True if @p path is itself a mount point (used to find Webmon's own bind mount). */
    static bool IsMountedAt(const std::string &path);

private:
    std::vector<std::string> UsbMountPoints();

    mutable std::mutex mutex_;
    RootStatus root_;
};

} // namespace webmon

#endif // WEBMON_USB_H
