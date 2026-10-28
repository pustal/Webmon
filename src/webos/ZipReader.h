/**
 * @file ZipReader.h
 * @brief Reads .zip archives (through miniz) and decides where the files of an
 *        uploaded mod data archive belong.
 */
#ifndef WEBMON_ZIP_READER_H
#define WEBMON_ZIP_READER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace webmon
{

/** One entry of a .zip archive. */
struct ZipEntry {
    std::uint32_t index = 0;   ///< Position in the archive's directory
    std::string name;          ///< Path inside the archive, as stored ("/"-separated)
    std::uint64_t size = 0;    ///< Uncompressed size
    bool directory     = false;
};

/** A .zip archive opened for reading. Not thread-safe; one per upload. */
class ZipReader
{
public:
    ZipReader();
    ~ZipReader();

    ZipReader(const ZipReader &)            = delete;
    ZipReader &operator=(const ZipReader &) = delete;

    /** Opens @p path. @return Empty on success, otherwise why it is not a usable .zip. */
    std::string Open(const std::string &path);

    /** The archive's entries (after a successful Open()). */
    const std::vector<ZipEntry> &Entries() const { return entries_; }

    /**
     * Unpacks entry @p index into the file @p path (its folder must exist).
     * @return Empty on success, otherwise what went wrong.
     */
    std::string Extract(std::uint32_t index, const std::string &path);

private:
    struct State;
    std::unique_ptr<State> state_;
    std::vector<ZipEntry> entries_;
};

/**
 * Where each entry of an archive uploaded into one playthrough's mod data goes.
 *
 * gen1recomp keeps mod data in mod_storage/<game>/<playthrough id>/<mod id>/.
 * People zip whichever folder they found, so the archive may start at any of
 * those levels; the part the target playthrough already gives is dropped:
 *   mod_storage/<game>/<id>/<mod>/...   <game>/<id>/<mod>/...
 *   <id>/<mod>/...                      <mod>/...
 * Folder entries and the files macOS and Windows add (__MACOSX/, ._*,
 * .DS_Store, Thumbs.db) are skipped.
 *
 * @param names the archive's entry names
 * @param[out] relative for each name, its path below the playthrough folder
 *             ("<mod id>/..."), or empty to skip it
 * @return Empty on success, otherwise why the archive does not fit (an
 *         unsafe path, or files that are not inside a mod folder).
 */
std::string PlanModStorageArchive(const std::vector<std::string> &names, std::vector<std::string> *relative);

/** True for a gen1recomp playthrough id (32 hexadecimal digits). */
bool LooksLikePlaythroughId(const std::string &text);

} // namespace webmon

#endif // WEBMON_ZIP_READER_H
