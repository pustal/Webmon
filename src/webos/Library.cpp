/**
 * @file Library.cpp
 * @brief Implementation of Library.h.
 */
#include "Library.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <system_error>

namespace webmon
{
namespace fs = std::filesystem;

namespace
{

/** Where each kind lives and which file name extensions it accepts. */
struct KindInfo {
    FileKind kind;
    const char *name;        ///< KindName()
    const char *tvFolder;    ///< relative to the data folder
    const char *usbFolder;   ///< relative to <drive>/Webmon ("" = its root)
    std::array<const char *, 3> extensions; ///< lower case, without the dot; nullptr-terminated
};

constexpr std::array<KindInfo, 5> kKinds = { {
    { FileKind::Love, "love", "game", "", { "love", nullptr, nullptr } },
    { FileKind::Rom, "rom", "inbox/roms", "roms", { "gb", "gbc", "gba" } },
    { FileKind::Save, "sav", "inbox/saves", "saves", { "sav", "srm", nullptr } },
    { FileKind::ModZip, "mod", "inbox/mod-zips", "mod-zips", { "zip", nullptr, nullptr } },
    { FileKind::Cart, "cart", "inbox/carts", "carts", { "g1rcart", nullptr, nullptr } },
} };

/** USB Webmon sub-folders created with the folder itself. */
const std::vector<std::string> kUsbSubFolders = { "roms", "saves", "mod-zips", "carts", "mods" };

/** Deepest folder level searched below a RetroArch "roms"/"saves" folder. */
constexpr int kRetroArchDepth = 3;
/** Most files listed from one RetroArch folder. */
constexpr std::size_t kRetroArchLimit = 2000;

const KindInfo &Info(FileKind kind)
{
    for (const KindInfo &info : kKinds) {
        if (info.kind == kind)
            return info;
    }
    return kKinds[0];
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/** True if @p name ends with one of @p kind's extensions. */
bool HasExtension(FileKind kind, const std::string &name)
{
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0)
        return false;
    const std::string extension = Lower(name.substr(dot + 1));
    for (const char *allowed : Info(kind).extensions) {
        if (allowed && extension == allowed)
            return true;
    }
    return false;
}

bool IsDirectory(const std::string &path)
{
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

} // namespace

const char *Library::KindName(FileKind kind)
{
    return Info(kind).name;
}

bool Library::ParseKind(const std::string &name, FileKind *kind)
{
    for (const KindInfo &info : kKinds) {
        if (name == info.name) {
            *kind = info.kind;
            return true;
        }
    }
    return false;
}

bool Library::AcceptsName(FileKind kind, const std::string &name)
{
    if (name.empty() || name.size() > 200 || name[0] == '.')
        return false;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        return false;
    return HasExtension(kind, name);
}

Library::Library(std::string dataFolder, Usb &usb) : dataFolder_(std::move(dataFolder)), usb_(usb) {}

std::string Library::GameSaveFolder() const
{
    const char *identity = std::getenv("POKEPORT_IDENTITY");
    return dataFolder_ + "/save/love/" + (identity && *identity ? identity : "pokemon-love2d");
}

void Library::Prepare()
{
    std::error_code ec;
    for (const KindInfo &info : kKinds)
        fs::create_directories(dataFolder_ + "/" + info.tvFolder, ec);
    fs::create_directories(IncomingFolder(), ec);
    fs::create_directories(dataFolder_ + "/inbox/.picked", ec);
    fs::create_directories(GameSaveFolder(), ec);

    const std::string message = LinkUsb();
    if (!message.empty())
        std::printf("[Webmon] USB: %s\n", message.c_str());
    LinkGameMods();
}

std::string Library::LinkUsb()
{
    const std::string link = dataFolder_ + "/usb";
    std::error_code ec;

    // Undo the previous link first.
    if (Usb::IsMountedAt(link))
        usb_.RunAsRoot("umount " + Usb::ShellQuote(link));
    if (fs::is_symlink(link, ec))
        fs::remove(link, ec);

    UsbLink result = UsbLink::None;
    std::string drive;
    std::string message = "No USB drive with a Webmon folder.";
    for (const UsbDrive &candidate : usb_.ListDrives()) {
        if (!candidate.hasWebmon)
            continue;
        const std::string target = candidate.mountPoint + "/" + Usb::kWebmonFolder;
        if (candidate.readable) {
            fs::remove(link, ec); // an empty folder left by an old bind mount
            fs::create_directory_symlink(target, link, ec);
            if (!ec) {
                result = UsbLink::Symlink;
                drive  = candidate.mountPoint;
                break;
            }
            message = "Could not link the drive: " + ec.message();
        } else if (usb_.Root().Available()) {
            fs::create_directories(link, ec);
            if (usb_.RunAsRoot("mount --bind " + Usb::ShellQuote(target) + " " + Usb::ShellQuote(link)) &&
                Usb::CanRead(link)) {
                result = UsbLink::BindMount;
                drive  = candidate.mountPoint;
                break;
            }
            usb_.RunAsRoot("umount " + Usb::ShellQuote(link));
            message = "Root could not make the drive readable to the app.";
        } else {
            message = "The app cannot read the drive and has no root access.";
        }
    }

    const bool writable = result != UsbLink::None && Usb::CanWrite(link);
    if (result != UsbLink::None) {
        // Make sure the sub-folders exist (older Webmon folders, or created by hand).
        usb_.CreateWebmonFolder(drive, kUsbSubFolders);
        std::printf("[Webmon] USB: using %s/%s (%s, %s)\n", drive.c_str(), Usb::kWebmonFolder,
                    result == UsbLink::Symlink ? "link" : "bind mount", writable ? "writable" : "read-only");
        message.clear();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    usbLink_     = result;
    usbDrive_    = drive;
    usbWritable_ = writable;
    return message;
}

void Library::LinkGameMods()
{
    const std::string mods = GameSaveFolder() + "/mods";
    const std::string target = dataFolder_ + "/usb/mods";
    std::error_code ec;
    bool onUsb = false;

    const bool useUsb = UsbWritable() && IsDirectory(target);
    if (fs::is_symlink(mods, ec)) {
        fs::remove(mods, ec); // re-made below, so it always points at the current drive
    }
    if (useUsb) {
        const bool emptyOrMissing = !fs::exists(mods, ec) || fs::is_empty(mods, ec);
        if (emptyOrMissing) {
            fs::remove(mods, ec);
            fs::create_directory_symlink(target, mods, ec);
            onUsb = !ec;
        } else {
            std::printf("[Webmon] mods: already on TV storage, keeping them there\n");
        }
    }
    if (!onUsb)
        fs::create_directories(mods, ec);
    std::printf("[Webmon] mods: %s\n", onUsb ? "on the USB drive" : "on TV storage");

    std::lock_guard<std::mutex> lock(mutex_);
    modsOnUsb_ = onUsb;
}

UsbLink Library::ActiveUsbLink() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return usbLink_;
}

std::string Library::ActiveUsbDrive() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return usbDrive_;
}

bool Library::UsbWritable() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return usbWritable_;
}

bool Library::ModsOnUsb() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return modsOnUsb_;
}

void Library::AddFolder(std::vector<LibraryFile> &out, FileKind kind, const std::string &folder,
                        const std::string &source, bool removable)
{
    std::error_code ec;
    std::vector<LibraryFile> found;
    std::vector<fs::file_time_type> times;
    for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (!AcceptsName(kind, name) || !it->is_regular_file(ec))
            continue;
        LibraryFile file{ kind, it->path().string(), name, source, it->file_size(ec), removable };
        found.push_back(file);
        times.push_back(it->last_write_time(ec));
    }
    // Newest first.
    std::vector<std::size_t> order(found.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return times[a] > times[b]; });
    for (const std::size_t i : order)
        out.push_back(found[i]);
}

void Library::AddRetroArch(std::vector<LibraryFile> &out, FileKind kind)
{
    const char *folderName = kind == FileKind::Rom ? "roms" : "saves";
    for (const UsbDrive &drive : usb_.ListDrives()) {
        if (drive.readable) {
            // Case-insensitive "roms"/"saves" at the root of the drive.
            std::error_code ec;
            for (fs::directory_iterator top(drive.mountPoint, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && top != end; top.increment(ec)) {
                if (!top->is_directory(ec) || Lower(top->path().filename().string()) != folderName)
                    continue;
                std::size_t listed = 0;
                for (fs::recursive_directory_iterator it(top->path(), fs::directory_options::skip_permission_denied, ec),
                     stop;
                     !ec && it != stop && listed < kRetroArchLimit; it.increment(ec)) {
                    if (it.depth() >= kRetroArchDepth)
                        it.disable_recursion_pending();
                    const std::string name = it->path().filename().string();
                    if (!it->is_regular_file(ec) || !AcceptsName(kind, name))
                        continue;
                    const std::string where =
                        fs::relative(it->path().parent_path(), drive.mountPoint, ec).string();
                    out.push_back({ kind, it->path().string(), name, "USB " + drive.label + ": " + where,
                                    it->file_size(ec), false });
                    ++listed;
                }
            }
        } else if (usb_.Root().Available()) {
            // The app cannot list the drive: ask root (busybox find, no -iname on every TV).
            std::string listing;
            const std::string command = "for d in " + Usb::ShellQuote(drive.mountPoint) + "/*; do "
                                        "n=$(basename \"$d\" | tr 'A-Z' 'a-z'); "
                                        "[ -d \"$d\" ] && [ \"$n\" = " + std::string(folderName) + " ] && "
                                        "find \"$d\" -maxdepth " + std::to_string(kRetroArchDepth + 1) +
                                        " -type f; done | head -n " + std::to_string(kRetroArchLimit);
            if (!usb_.RunAsRoot(command, &listing))
                continue;
            std::istringstream lines(listing);
            std::string path;
            while (std::getline(lines, path)) {
                const std::string name = fs::path(path).filename().string();
                if (!AcceptsName(kind, name))
                    continue;
                std::error_code ec;
                const std::string where = fs::relative(fs::path(path).parent_path(), drive.mountPoint, ec).string();
                out.push_back({ kind, path, name, "USB " + drive.label + ": " + where, 0, false });
            }
        }
    }
}

std::vector<LibraryFile> Library::List(FileKind kind)
{
    const KindInfo &info = Info(kind);
    std::vector<LibraryFile> files;
    AddFolder(files, kind, dataFolder_ + "/" + info.tvFolder, "TV", true);
    if (ActiveUsbLink() != UsbLink::None) {
        const std::string usbFolder = dataFolder_ + "/usb" + (*info.usbFolder ? std::string("/") + info.usbFolder : "");
        AddFolder(files, kind, usbFolder, "USB Webmon", true);
    }
    if (kind == FileKind::Rom || kind == FileKind::Save)
        AddRetroArch(files, kind);
    return files;
}

std::string Library::UploadFolder(FileKind kind, bool preferUsb) const
{
    const KindInfo &info = Info(kind);
    if (preferUsb && ActiveUsbLink() != UsbLink::None)
        return dataFolder_ + "/usb" + (*info.usbFolder ? std::string("/") + info.usbFolder : "");
    return dataFolder_ + "/" + info.tvFolder;
}

std::string Library::Place(const std::string &temporary, const std::string &folder, const std::string &name)
{
    const std::string destination = folder + "/" + name;
    std::error_code ec;
    if (Usb::CanWrite(folder)) {
        fs::rename(temporary, destination, ec);
        if (ec) {
            // Another file system (the USB drive): copy, then drop the temporary file.
            ec.clear();
            fs::copy_file(temporary, destination, fs::copy_options::overwrite_existing, ec);
            fs::remove(temporary);
        }
        return ec ? "Could not store the file: " + ec.message() : std::string();
    }
    if (usb_.Root().Available() &&
        usb_.RunAsRoot("mv -f " + Usb::ShellQuote(temporary) + " " + Usb::ShellQuote(destination))) {
        return std::string();
    }
    fs::remove(temporary, ec);
    return "The app cannot write to that folder and has no root access.";
}

std::string Library::MakeReadable(const std::string &path)
{
    if (std::FILE *file = std::fopen(path.c_str(), "rb")) {
        std::fclose(file);
        return path;
    }
    if (!usb_.Root().Available())
        return std::string();
    const std::string copy = dataFolder_ + "/inbox/.picked/" + fs::path(path).filename().string();
    if (!usb_.RunAsRoot("cp -f " + Usb::ShellQuote(path) + " " + Usb::ShellQuote(copy) + " && chmod 644 " +
                        Usb::ShellQuote(copy)))
        return std::string();
    return copy;
}

std::string Library::NewestGame()
{
    std::string newest;
    std::int64_t newestTime = 0;
    for (const LibraryFile &file : List(FileKind::Love)) {
        struct stat info {};
        if (stat(file.path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
            continue;
        const std::int64_t modified = static_cast<std::int64_t>(info.st_mtime);
        if (newest.empty() || modified > newestTime) {
            newest     = file.path;
            newestTime = modified;
        }
    }
    return newest;
}

std::string Library::Remove(const std::string &path)
{
    // Only files Webmon manages: TV storage and the USB Webmon folder, never RetroArch folders.
    const std::string name = fs::path(path).filename().string();
    const std::string parent = fs::path(path).parent_path().string();
    bool managed = false;
    for (const KindInfo &info : kKinds) {
        if (!HasExtension(info.kind, name))
            continue;
        const std::string usbFolder = dataFolder_ + "/usb" + (*info.usbFolder ? std::string("/") + info.usbFolder : "");
        if (parent == dataFolder_ + "/" + info.tvFolder || parent == usbFolder)
            managed = true;
    }
    if (!managed || name.empty() || name[0] == '.')
        return "That file is not managed by Webmon.";

    if (unlink(path.c_str()) == 0)
        return std::string();
    if (usb_.Root().Available() && usb_.RunAsRoot("rm -f " + Usb::ShellQuote(path)))
        return std::string();
    return "Could not delete the file.";
}

} // namespace webmon
