/**
 * @file ZipReader.cpp
 * @brief Implementation of ZipReader.h.
 */
#include "ZipReader.h"

#include <miniz.h>

#include <algorithm>
#include <cctype>

namespace webmon
{

struct ZipReader::State {
    mz_zip_archive archive{};
    bool open = false;
};

namespace
{

/** miniz's description of the archive's last error. */
std::string LastError(mz_zip_archive &archive)
{
    return mz_zip_get_error_string(mz_zip_get_last_error(&archive));
}

/** True for a name gen1recomp accepts as one storage path segment (a mod id, a game). */
bool IsPlainSegment(const std::string &text)
{
    if (text.empty() || text.size() > 100)
        return false;
    return std::all_of(text.begin(), text.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
    });
}

/** Files operating systems add to archives, never mod data. */
bool IsSystemClutter(const std::vector<std::string> &segments)
{
    const std::string &last = segments.back();
    if (last == ".DS_Store" || last == "Thumbs.db" || last.rfind("._", 0) == 0)
        return true;
    return std::find(segments.begin(), segments.end(), "__MACOSX") != segments.end();
}

/** One kept entry while planning: its position in the input and its path segments. */
struct PlannedEntry {
    std::size_t position = 0;
    std::vector<std::string> segments;
};

/** True if every entry has at least @p depth segments and the same segment at @p level. */
bool ShareSegment(const std::vector<PlannedEntry> &entries, std::size_t level, std::size_t depth)
{
    for (const PlannedEntry &entry : entries) {
        if (entry.segments.size() < depth || entry.segments[level] != entries.front().segments[level])
            return false;
    }
    return true;
}

} // namespace

ZipReader::ZipReader() : state_(std::make_unique<State>()) {}

ZipReader::~ZipReader()
{
    if (state_->open)
        mz_zip_reader_end(&state_->archive);
}

std::string ZipReader::Open(const std::string &path)
{
    if (state_->open)
        return "already open";
    if (!mz_zip_reader_init_file(&state_->archive, path.c_str(), 0))
        return "not a readable .zip (" + LastError(state_->archive) + ")";
    state_->open = true;

    const mz_uint count = mz_zip_reader_get_num_files(&state_->archive);
    entries_.reserve(count);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&state_->archive, i, &stat))
            return "damaged .zip (" + LastError(state_->archive) + ")";
        if (stat.m_is_encrypted)
            return "the .zip is password protected";
        if (!stat.m_is_supported)
            return "the .zip uses a compression method Webmon cannot read (use plain zip/deflate)";
        ZipEntry entry;
        entry.index     = i;
        entry.name      = stat.m_filename;
        entry.size      = stat.m_uncomp_size;
        entry.directory = stat.m_is_directory != 0;
        entries_.push_back(std::move(entry));
    }
    return std::string();
}

std::string ZipReader::Extract(std::uint32_t index, const std::string &path)
{
    if (!state_->open)
        return "not open";
    if (!mz_zip_reader_extract_to_file(&state_->archive, index, path.c_str(), 0))
        return LastError(state_->archive);
    return std::string();
}

bool LooksLikePlaythroughId(const std::string &text)
{
    return text.size() == 32 &&
           std::all_of(text.begin(), text.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}

std::string PlanModStorageArchive(const std::vector<std::string> &names, std::vector<std::string> *relative)
{
    relative->assign(names.size(), std::string());

    std::vector<PlannedEntry> kept;
    for (std::size_t i = 0; i < names.size(); ++i) {
        std::string name = names[i];
        std::replace(name.begin(), name.end(), '\\', '/');
        if (name.empty() || name.back() == '/')
            continue; // a folder entry: folders are made as files need them
        if (name.front() == '/')
            return "The zip holds an absolute path (" + names[i] + ").";
        if (std::any_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; }))
            return "The zip holds a file name with control characters.";

        PlannedEntry entry;
        entry.position = i;
        std::size_t start = 0;
        while (start <= name.size()) {
            const std::size_t slash = name.find('/', start);
            const std::string segment =
                name.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (segment == "..")
                return "The zip holds a path leading outside its folder (" + names[i] + ").";
            if (!segment.empty() && segment != ".")
                entry.segments.push_back(segment);
            if (slash == std::string::npos)
                break;
            start = slash + 1;
        }
        if (entry.segments.empty() || IsSystemClutter(entry.segments))
            continue;
        kept.push_back(std::move(entry));
    }
    if (kept.empty())
        return "The zip has no files.";

    // Drop the levels the target playthrough already gives:
    // mod_storage/, <game>/ (followed by a playthrough id) and <playthrough id>/.
    for (;;) {
        if (ShareSegment(kept, 0, 2)) {
            const std::string &first = kept.front().segments[0];
            const bool dropFirst = first == "mod_storage" || LooksLikePlaythroughId(first) ||
                                   (ShareSegment(kept, 1, 3) && LooksLikePlaythroughId(kept.front().segments[1]));
            if (dropFirst) {
                for (PlannedEntry &entry : kept)
                    entry.segments.erase(entry.segments.begin());
                continue;
            }
        }
        break;
    }

    for (const PlannedEntry &entry : kept) {
        if (entry.segments.size() < 2 || !IsPlainSegment(entry.segments[0]))
            return "The zip should hold mod folders (such as potato_voxel/...), or the playthrough folder that "
                   "contains them; \"" + names[entry.position] + "\" is not inside one.";
        std::string path;
        for (const std::string &segment : entry.segments)
            path += (path.empty() ? "" : "/") + segment;
        (*relative)[entry.position] = path;
    }
    return std::string();
}

} // namespace webmon
