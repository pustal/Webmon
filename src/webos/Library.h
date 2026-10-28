/**
 * @file Library.h
 * @brief Where Webmon keeps the player's files, and finding them again.
 *
 * TV storage (the app's data folder):
 *   game/                 gen1recomp .love
 *   inbox/roms|saves|mod-zips|carts|n64/   files uploaded for the game's import screens
 *                         (n64: N64 ROMs a mod imports from, such as Pokemon Stadium 2)
 *   usb                   link (or root bind mount) to the active USB drive's Webmon folder
 *
 * USB drive (heavier content), "<drive>/Webmon/":
 *   *.love                gen1recomp .love
 *   roms/ saves/ mod-zips/ carts/ n64/  as in inbox/
 *   game-data/            the game's own data (saves, settings, mods, its import
 *                         cache), when the drive can be written; else on TV storage
 *
 * In the game's data (LÖVE's save folder, $XDG_DATA_HOME/love/pokemon-love2d/):
 *   saves/<game>/slotN.lua   the game's save slots
 *   exports/<game>/<name>.sav     battery saves the game exported
 *   mod_storage/<game>/<playthrough id>/<mod id>/   each mod's files
 *
 * The game's file pickers also list Pokemon ROMs (GB/GBC/GBA and N64) and
 * saves found in RetroArch-style "roms/" and "saves/" folders at the root of
 * any USB drive.
 */
#ifndef WEBMON_LIBRARY_H
#define WEBMON_LIBRARY_H

#include "Usb.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace webmon
{

/** The kinds of file Webmon handles. */
enum class FileKind { Love, Rom, Save, ModZip, Cart, N64Rom };

/** A file the player can pick or manage. */
struct LibraryFile {
    FileKind kind;
    std::string path;       ///< Absolute path (on a USB drive the app may not be able to read it)
    std::string name;       ///< File name
    std::string source;     ///< Where it was found, for people ("TV", "USB Webmon", "USB roms/gb", ...)
    std::uint64_t size = 0;
    bool removable     = false; ///< In a Webmon folder (not a RetroArch folder), so the page may delete it
};

/**
 * One playthrough's mod data: gen1recomp keeps each mod's files per game and
 * playthrough in <game data>/mod_storage/<game>/<playthrough id>/. A voxel
 * world mod's map cache lives there, and a new playthrough id starts a new
 * cache, so old ones can pile up.
 */
struct ModStorageFolder {
    std::string game;            ///< "red", "yellow", ...
    std::string playthrough;     ///< The game's opaque playthrough id
    std::uint64_t bytes = 0;     ///< Total size of its files
    std::int64_t modified = 0;   ///< Newest file change (Unix time)
    bool newest = false;         ///< The most recently changed one of its game
};

/**
 * One of the game's own save files: its slots (saves/<game>/slotN.lua, the
 * game's format) and the battery saves it exports (exports/<game>/<name>.sav).
 */
struct GameSaveFile {
    std::string relative;        ///< Path below the game's data folder ("saves/yellow/slot1.lua")
    std::uint64_t size = 0;
    std::int64_t modified = 0;   ///< Unix time
};

/** How the USB Webmon folder is reached from the data folder. */
enum class UsbLink { None, Symlink, BindMount };

class Library
{
public:
    Library(std::string dataFolder, Usb &usb);

    /** Creates the TV folders, links the USB Webmon folder and the game's mods folder. */
    void Prepare();

    /**
     * Re-links data/usb to the first drive with a Webmon folder (after the
     * page created one). @return Empty on success, otherwise why not.
     */
    std::string LinkUsb();

    /** How data/usb is linked, and to which drive. */
    UsbLink ActiveUsbLink() const;
    std::string ActiveUsbDrive() const;
    /** True when files can be written into the USB Webmon folder by the app itself. */
    bool UsbWritable() const;
    /** True when the game's data (XDG_DATA_HOME) is on the USB drive. */
    bool GameDataOnUsb() const;

    /** Every file of @p kind, newest first within each place. */
    std::vector<LibraryFile> List(FileKind kind);

    /**
     * Folder an upload of @p kind should go to: the USB Webmon folder when it
     * is active (and @p preferUsb), else TV storage.
     */
    std::string UploadFolder(FileKind kind, bool preferUsb) const;

    /** Folder for files that are written first and moved into place afterwards. */
    std::string IncomingFolder() const { return dataFolder_ + "/inbox/.incoming"; }

    /**
     * Moves the finished upload @p temporary to @p folder/@p name, with root
     * if the app cannot write there. @return Empty on success, otherwise a message.
     */
    std::string Place(const std::string &temporary, const std::string &folder, const std::string &name);

    /**
     * A path the game can open for @p file: the file itself, or a copy made
     * with root when the app cannot read the drive. Empty on failure.
     */
    std::string MakeReadable(const std::string &path);

    /**
     * The gen1recomp .love to run: the most recently modified one in TV
     * storage or the USB Webmon folder. Empty if there is none.
     */
    std::string NewestGame();

    /** Deletes @p path if it is a removable library file. @return Empty on success, otherwise a message. */
    std::string Remove(const std::string &path);

    /** True if @p name is a plain file name (no folders, not hidden) with an extension allowed for @p kind. */
    static bool AcceptsName(FileKind kind, const std::string &name);

    /** Sub-folders of a USB Webmon folder. */
    static const std::vector<std::string> &UsbSubFolders();

    /** "love", "rom", "sav", "mod", "cart", "n64". */
    static const char *KindName(FileKind kind);
    /** Inverse of KindName. @return False for unknown names. */
    static bool ParseKind(const std::string &name, FileKind *kind);

    const std::string &DataFolder() const { return dataFolder_; }

    /**
     * True if @p name looks like a Pokemon game or save ("pokemon", "pokémon",
     * "pkmn", "pocket monsters", any case). RetroArch folders are filtered
     * with it, so a whole collection does not crowd the picker.
     */
    static bool LooksLikePokemon(const std::string &name);

    /** Every playthrough folder in the game's mod_storage, by game, newest first. */
    std::vector<ModStorageFolder> ModStorage();

    /**
     * Deletes mod_storage/<@p game>/<@p playthrough> (both plain names).
     * @return Empty on success, otherwise a message for the page.
     */
    std::string RemoveModStorage(const std::string &game, const std::string &playthrough);

    /** The game's mod_storage folder (it may not exist yet). */
    std::string ModStorageRoot() const;

    /**
     * Mod data uploaded file by file (a folder chosen on a computer, such as a
     * voxel map cache built there) for mod_storage/<@p game>/<@p playthrough>/.
     * Files collect in a staging folder; Finish swaps each uploaded mod folder
     * in (replacing that mod's data, leaving other mods alone), Abort drops them.
     * Each returns empty on success, otherwise a message for the page.
     */
    std::string BeginModStorageUpload(const std::string &game, const std::string &playthrough);

    /**
     * Where to write the uploaded file @p relative ("<mod id>/<path>", checked
     * against the game's storage naming rules) of @p size bytes; creates its folder.
     */
    std::string ModStorageUploadPath(const std::string &game, const std::string &playthrough,
                                     const std::string &relative, std::uint64_t size, std::string *path);

    /** @param[out] summary what was stored, for the page */
    std::string FinishModStorageUpload(const std::string &game, const std::string &playthrough,
                                       std::string *summary);

    void AbortModStorageUpload(const std::string &game, const std::string &playthrough);

    /** The game's own save files, newest first. */
    std::vector<GameSaveFile> GameSaves();

    /**
     * Absolute path of the game save @p relative (as listed by GameSaves()),
     * or empty if it is not one of them.
     */
    std::string GameSavePath(const std::string &relative);

private:
    /** mod_storage/<game>/<playthrough> after checking both are plain names. */
    std::string PlaythroughFolder(const std::string &game, const std::string &playthrough, std::string *folder) const;

    void PlaceGameData();
    void AddFolder(std::vector<LibraryFile> &out, FileKind kind, const std::string &folder, const std::string &source,
                   bool removable);
    void AddRetroArch(std::vector<LibraryFile> &out, FileKind kind);

    const std::string dataFolder_;
    Usb &usb_;

    mutable std::mutex mutex_;
    UsbLink usbLink_ = UsbLink::None;
    std::string usbDrive_;
    bool usbWritable_ = false;
    bool gameDataOnUsb_ = false;
    std::string gameFolder_; ///< $XDG_DATA_HOME/love/<identity>, chosen by PlaceGameData()
};

} // namespace webmon

#endif // WEBMON_LIBRARY_H
