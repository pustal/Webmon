/**
 * @file Library.cpp
 * @brief Implementation of Library.h.
 */
#include "Library.h"

#include "ZipReader.h"

#include <sys/stat.h>
#include <sys/statvfs.h>
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

constexpr std::array<KindInfo, 6> kKinds = { {
    { FileKind::Love, "love", "game", "", { "love", nullptr, nullptr } },
    { FileKind::Rom, "rom", "inbox/roms", "roms", { "gb", "gbc", "gba" } },
    { FileKind::Save, "sav", "inbox/saves", "saves", { "sav", "srm", nullptr } },
    { FileKind::ModZip, "mod", "inbox/mod-zips", "mod-zips", { "zip", nullptr, nullptr } },
    { FileKind::Cart, "cart", "inbox/carts", "carts", { "g1rcart", nullptr, nullptr } },
    // N64 ROMs for mods that import from one (the Stadium 2 importer); the game
    // accepts all three byte orders.
    { FileKind::N64Rom, "n64", "inbox/n64", "n64", { "z64", "n64", "v64" } },
} };

/** USB Webmon sub-folders created with the folder itself. */
const std::vector<std::string> kUsbSubFolders = { "roms", "saves", "mod-zips", "carts", "n64", "game-data" };

/** Deepest folder level searched below a RetroArch "roms"/"saves" folder. */
constexpr int kRetroArchDepth = 3;
/** Most entries visited in one RetroArch folder (big collections stay quick to scan). */
constexpr std::size_t kRetroArchScanLimit = 50000;
/** LÖVE identity of gen1recomp (conf.lua), unless POKEPORT_IDENTITY overrides it. */
constexpr const char *kGameIdentity = "pokemon-love2d";

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

const std::vector<std::string> &Library::UsbSubFolders()
{
    return kUsbSubFolders;
}

bool Library::LooksLikePokemon(const std::string &name)
{
    const std::string lower = Lower(name);
    // "pokémon" in UTF-8 lower case; Lower() leaves the two bytes of "é" alone.
    static const char *const kNeedles[] = { "pokemon", "pok\xc3\xa9mon", "pok\xc3\x89mon", "pkmn", "pocket monsters" };
    for (const char *needle : kNeedles) {
        if (lower.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

void Library::Prepare()
{
    std::error_code ec;
    for (const KindInfo &info : kKinds)
        fs::create_directories(dataFolder_ + "/" + info.tvFolder, ec);
    fs::create_directories(IncomingFolder(), ec);
    fs::create_directories(dataFolder_ + "/inbox/.picked", ec);

    const std::string message = LinkUsb();
    if (!message.empty())
        std::printf("[Webmon] USB: %s\n", message.c_str());
    PlaceGameData();
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

void Library::PlaceGameData()
{
    const char *identityOverride = std::getenv("POKEPORT_IDENTITY");
    const std::string identity   = identityOverride && *identityOverride ? identityOverride : kGameIdentity;
    const std::string tvRoot     = dataFolder_ + "/save";
    const std::string usbRoot    = dataFolder_ + "/usb/game-data";
    const std::string tvGame     = tvRoot + "/love/" + identity;
    const std::string usbGame    = usbRoot + "/love/" + identity;
    std::error_code ec;

    // Version 0.2.0 linked the game's mods folder to <USB>/Webmon/mods: undo that first.
    if (fs::is_symlink(tvGame + "/mods", ec))
        fs::remove(tvGame + "/mods", ec);

    bool onUsb = false;
    if (UsbWritable()) {
        fs::create_directories(usbRoot, ec);
        const bool usbHasData = fs::exists(usbGame, ec);
        const bool tvHasData  = fs::exists(tvGame, ec) && !fs::is_empty(tvGame, ec);
        bool moved = true;
        if (!usbHasData && tvHasData) {
            // First run with this drive: move the game's data over (copy, then free the TV storage).
            std::printf("[Webmon] moving the game's data to the USB drive...\n");
            fs::create_directories(usbRoot + "/love", ec);
            fs::copy(tvGame, usbGame, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
            if (ec) {
                // Keep playing from TV storage rather than with an empty data folder on the drive.
                std::printf("[Webmon] could not move the game's data: %s\n", ec.message().c_str());
                std::error_code ignored;
                fs::remove_all(usbGame, ignored);
                moved = false;
            } else {
                fs::remove_all(tvGame, ec);
            }
        }
        // Mods installed into <USB>/Webmon/mods by version 0.2.0 join the game's data.
        const std::string oldMods = dataFolder_ + "/usb/mods";
        if (fs::is_directory(oldMods, ec) && !fs::exists(usbGame + "/mods", ec)) {
            fs::create_directories(usbGame, ec);
            fs::rename(oldMods, usbGame + "/mods", ec);
        }
        onUsb = moved && Usb::CanWrite(usbRoot);
    }

    // LÖVE keeps a game's data in $XDG_DATA_HOME/love/<identity>.
    setenv("XDG_DATA_HOME", (onUsb ? usbRoot : tvRoot).c_str(), 1);
    std::printf("[Webmon] game data: %s\n", onUsb ? "on the USB drive" : "on TV storage");

    std::lock_guard<std::mutex> lock(mutex_);
    gameDataOnUsb_ = onUsb;
    gameFolder_    = onUsb ? usbGame : tvGame;
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

bool Library::GameDataOnUsb() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return gameDataOnUsb_;
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
    // ROMs of every system (GB, GBA, N64) share RetroArch's roms/ folder.
    const char *folderName = kind == FileKind::Save ? "saves" : "roms";
    for (const UsbDrive &drive : usb_.ListDrives()) {
        if (drive.readable) {
            // Case-insensitive "roms"/"saves" at the root of the drive.
            std::error_code ec;
            for (fs::directory_iterator top(drive.mountPoint, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && top != end; top.increment(ec)) {
                if (!top->is_directory(ec) || Lower(top->path().filename().string()) != folderName)
                    continue;
                std::size_t visited = 0;
                for (fs::recursive_directory_iterator it(top->path(), fs::directory_options::skip_permission_denied, ec),
                     stop;
                     !ec && it != stop && visited < kRetroArchScanLimit; it.increment(ec), ++visited) {
                    if (it.depth() >= kRetroArchDepth)
                        it.disable_recursion_pending();
                    const std::string name = it->path().filename().string();
                    if (!LooksLikePokemon(name) || !AcceptsName(kind, name) || !it->is_regular_file(ec))
                        continue;
                    const std::string where =
                        fs::relative(it->path().parent_path(), drive.mountPoint, ec).string();
                    out.push_back({ kind, it->path().string(), name, "USB " + drive.label + ": " + where,
                                    it->file_size(ec), false });
                }
            }
        } else if (usb_.Root().Available()) {
            // The app cannot list the drive: ask root (busybox find, no -iname on every TV).
            std::string listing;
            const std::string command = "for d in " + Usb::ShellQuote(drive.mountPoint) + "/*; do "
                                        "n=$(basename \"$d\" | tr 'A-Z' 'a-z'); "
                                        "[ -d \"$d\" ] && [ \"$n\" = " + std::string(folderName) + " ] && "
                                        "find \"$d\" -maxdepth " + std::to_string(kRetroArchDepth + 1) +
                                        " -type f; done | grep -i -e pok.mon -e pok..mon -e pkmn -e 'pocket monsters'"
                                        " | head -n " + std::to_string(kRetroArchScanLimit);
            if (!usb_.RunAsRoot(command, &listing))
                continue;
            std::istringstream lines(listing);
            std::string path;
            while (std::getline(lines, path)) {
                const std::string name = fs::path(path).filename().string();
                if (!LooksLikePokemon(name) || !AcceptsName(kind, name))
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
    if (kind == FileKind::Rom || kind == FileKind::Save || kind == FileKind::N64Rom)
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

namespace
{

/** True for the plain names gen1recomp uses for games and playthrough ids. */
bool IsPlainSegment(const std::string &text)
{
    if (text.empty() || text.size() > 100)
        return false;
    for (const char c : text) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
            return false;
    }
    return true;
}

} // namespace

std::vector<ModStorageFolder> Library::ModStorage()
{
    std::string root;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        root = gameFolder_ + "/mod_storage";
    }
    std::vector<ModStorageFolder> folders;
    std::error_code ec;
    for (fs::directory_iterator game(root, fs::directory_options::skip_permission_denied, ec), end; !ec && game != end;
         game.increment(ec)) {
        const std::string gameName = game->path().filename().string();
        if (!game->is_directory(ec) || !IsPlainSegment(gameName))
            continue;
        const std::size_t firstOfGame = folders.size();
        for (fs::directory_iterator run(game->path(), fs::directory_options::skip_permission_denied, ec), stop;
             !ec && run != stop; run.increment(ec)) {
            const std::string id = run->path().filename().string();
            if (!run->is_directory(ec) || !IsPlainSegment(id))
                continue;
            ModStorageFolder folder{ gameName, id, 0, 0, false };
            for (fs::recursive_directory_iterator it(run->path(), fs::directory_options::skip_permission_denied, ec),
                 done;
                 !ec && it != done; it.increment(ec)) {
                if (!it->is_regular_file(ec))
                    continue;
                folder.bytes += it->file_size(ec);
                struct stat info {};
                if (stat(it->path().c_str(), &info) == 0 && info.st_mtime > folder.modified)
                    folder.modified = static_cast<std::int64_t>(info.st_mtime);
            }
            ec.clear();
            folders.push_back(folder);
        }
        ec.clear();
        // Newest first within the game, and mark it.
        std::sort(folders.begin() + static_cast<std::ptrdiff_t>(firstOfGame), folders.end(),
                  [](const ModStorageFolder &a, const ModStorageFolder &b) { return a.modified > b.modified; });
        if (folders.size() > firstOfGame)
            folders[firstOfGame].newest = true;
    }
    return folders;
}

std::string Library::RemoveModStorage(const std::string &game, const std::string &playthrough)
{
    if (!IsPlainSegment(game) || !IsPlainSegment(playthrough))
        return "Unknown game or playthrough.";
    std::string folder;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        folder = gameFolder_ + "/mod_storage/" + game + "/" + playthrough;
    }
    std::error_code ec;
    if (!fs::is_directory(folder, ec))
        return "That folder does not exist.";
    fs::remove_all(folder, ec);
    if (ec) {
        if (usb_.Root().Available() && usb_.RunAsRoot("rm -rf " + Usb::ShellQuote(folder), nullptr, 10 * 60 * 1000))
            return std::string();
        return "Could not delete it: " + ec.message();
    }
    std::printf("[Webmon] deleted mod data %s/%s\n", game.c_str(), playthrough.c_str());
    return std::string();
}

std::string Library::ModStorageRoot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return gameFolder_ + "/mod_storage";
}

namespace
{

/** Room left free beside an unpacked archive, for the game's own writes. */
constexpr std::uint64_t kSpareBytes = 32ull * 1024ull * 1024ull;

/** Free bytes on the file system holding @p path (0 if unknown). */
std::uint64_t FreeBytesAt(const std::string &path)
{
    struct statvfs info {};
    if (statvfs(path.c_str(), &info) != 0)
        return 0;
    return static_cast<std::uint64_t>(info.f_bavail) * info.f_frsize;
}

/** @p bytes in MB with one decimal, for messages. */
std::string Megabytes(std::uint64_t bytes)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

} // namespace

std::string Library::InstallModStorage(const std::string &zipPath, const std::string &game,
                                       const std::string &playthrough, std::string *summary)
{
    if (!IsPlainSegment(game) || !IsPlainSegment(playthrough))
        return "Unknown game or playthrough.";
    const std::string target = ModStorageRoot() + "/" + game + "/" + playthrough;

    ZipReader zip;
    std::string error = zip.Open(zipPath);
    if (!error.empty())
        return "The upload is " + error + ".";
    std::vector<std::string> names;
    names.reserve(zip.Entries().size());
    for (const ZipEntry &entry : zip.Entries())
        names.push_back(entry.directory ? entry.name + (entry.name.empty() || entry.name.back() != '/' ? "/" : "")
                                        : entry.name);
    std::vector<std::string> relative;
    error = PlanModStorageArchive(names, &relative);
    if (!error.empty())
        return error;

    // What will be unpacked: total size, file count and the mod folders it replaces.
    std::uint64_t total = 0;
    std::size_t files   = 0;
    std::vector<std::string> mods;
    for (std::size_t i = 0; i < relative.size(); ++i) {
        if (relative[i].empty())
            continue;
        total += zip.Entries()[i].size;
        ++files;
        const std::string mod = relative[i].substr(0, relative[i].find('/'));
        if (std::find(mods.begin(), mods.end(), mod) == mods.end())
            mods.push_back(mod);
    }

    std::error_code ec;
    fs::create_directories(target, ec);
    if (ec)
        return "Could not create the playthrough folder: " + ec.message();
    const std::uint64_t free = FreeBytesAt(target);
    if (free < total + kSpareBytes)
        return "Not enough free space: the files need " + Megabytes(total) + ", " + Megabytes(free) + " are free.";

    // Unpack beside the target first, so a failure leaves the current data as it was.
    const std::string staging = target + "/.webmon-incoming";
    fs::remove_all(staging, ec);
    ec.clear();
    for (std::size_t i = 0; i < relative.size(); ++i) {
        if (relative[i].empty())
            continue;
        const fs::path destination = staging + "/" + relative[i];
        fs::create_directories(destination.parent_path(), ec);
        const std::string extractError = ec ? ec.message() : zip.Extract(zip.Entries()[i].index, destination.string());
        if (!extractError.empty()) {
            fs::remove_all(staging, ec);
            return "Could not unpack " + relative[i] + ": " + extractError + ".";
        }
    }

    // Swap each mod folder in.
    for (const std::string &mod : mods) {
        const std::string current = target + "/" + mod;
        fs::remove_all(current, ec);
        if (!ec)
            fs::rename(staging + "/" + mod, current, ec);
        if (ec) {
            const std::string message = ec.message();
            fs::remove_all(staging, ec);
            return "Could not put " + mod + " in place: " + message + ".";
        }
    }
    fs::remove_all(staging, ec);

    std::string modList;
    for (const std::string &mod : mods)
        modList += (modList.empty() ? "" : ", ") + mod;
    *summary = "Unpacked " + std::to_string(files) + (files == 1 ? " file (" : " files (") + Megabytes(total) + ") into " + modList + " of " +
               game + " playthrough " + playthrough + ".";
    std::printf("[Webmon] %s\n", summary->c_str());
    return std::string();
}

std::vector<GameSaveFile> Library::GameSaves()
{
    std::string root;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        root = gameFolder_;
    }
    std::vector<GameSaveFile> saves;
    const auto add = [&](const fs::path &path) {
        const std::string name = path.filename().string();
        // Hidden files and the game's own temporary and backup copies are left out.
        if (name.empty() || name[0] == '.' || Lower(name).find(".tmp") != std::string::npos ||
            Lower(name).find(".bak") != std::string::npos)
            return;
        struct stat info {};
        if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
            return;
        GameSaveFile save;
        save.relative = path.string().substr(root.size() + 1);
        save.size     = static_cast<std::uint64_t>(info.st_size);
        save.modified = static_cast<std::int64_t>(info.st_mtime);
        saves.push_back(std::move(save));
    };

    std::error_code ec;
    for (const char *folder : { "saves", "exports" }) {
        for (fs::recursive_directory_iterator it(root + "/" + folder, fs::directory_options::skip_permission_denied, ec),
             end;
             !ec && it != end; it.increment(ec))
            add(it->path());
        ec.clear();
    }
    // Saves from before the game had slots: save.lua, save_yellow.lua, save_cart_<id>.lua.
    for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind("save", 0) == 0 && name.size() > 4 && name.compare(name.size() - 4, 4, ".lua") == 0)
            add(it->path());
    }

    std::sort(saves.begin(), saves.end(),
              [](const GameSaveFile &a, const GameSaveFile &b) { return a.modified > b.modified; });
    return saves;
}

std::string Library::GameSavePath(const std::string &relative)
{
    for (const GameSaveFile &save : GameSaves()) {
        if (save.relative == relative) {
            std::lock_guard<std::mutex> lock(mutex_);
            return gameFolder_ + "/" + relative;
        }
    }
    return std::string();
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
