/**
 * @file WebServer.cpp
 * @brief Implementation of WebServer.h.
 */
#include "WebServer.h"

#include "WebPage.generated.h"
#include "webos/Downloader.h"
#include "webos/Json.h"
#include "webos/Library.h"
#include "webos/NetInfo.h"
#include "webos/Memory.h"
#include "webos/Swap.h"
#include "webos/Usb.h"

// Four workers: the page uploads one file at a time and polls the status alongside.
#define CPPHTTPLIB_THREAD_POOL_COUNT 4
#include <httplib.h>

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <random>
#include <sstream>

namespace webmon
{
namespace
{

constexpr const char *kJsonType  = "application/json";
constexpr const char *kTextType  = "text/plain; charset=utf-8";
constexpr const char *kPinHeader = "X-Pin";
constexpr const char *kPinFile   = "webmon_pin.txt";
constexpr int kPortAttempts      = 10;
/** Largest upload accepted (a .love is ~100 MB; N64 ROMs are at most 64 MB). */
constexpr std::uint64_t kMaxUploadBytes = 512ull * 1024ull * 1024ull;

void Fail(httplib::Response &res, int status, const std::string &message)
{
    res.status = status;
    res.set_content(message, kTextType);
}

void Ok(httplib::Response &res, const std::string &message)
{
    res.set_content("{\"ok\":true,\"message\":" + json::Quote(message) + "}", kJsonType);
}

/** The announced body size. @return False if missing or malformed. */
bool ContentLength(const httplib::Request &req, std::uint64_t *out)
{
    const std::string value = req.get_header_value("Content-Length");
    if (value.empty() ||
        !std::all_of(value.begin(), value.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
        return false;
    try {
        *out = static_cast<std::uint64_t>(std::stoull(value));
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

/** Free bytes on the file system holding @p path. */
std::uint64_t FreeBytes(const std::string &path)
{
    struct statvfs info {};
    if (statvfs(path.c_str(), &info) != 0)
        return 0;
    return static_cast<std::uint64_t>(info.f_bavail) * info.f_frsize;
}

/**
 * Streams the request body into @p path, refusing more than @p expected
 * bytes, and flushes it to storage.
 * @return Empty on success, otherwise what went wrong (with the system's reason).
 */
std::string StreamBodyToFile(const httplib::ContentReader &reader, const std::string &path, std::uint64_t expected)
{
    std::FILE *out = std::fopen(path.c_str(), "wb");
    if (!out)
        return std::string("cannot create the file (") + std::strerror(errno) + ")";
    std::uint64_t written = 0;
    int writeError        = 0;
    const bool completed  = reader([&](const char *data, std::size_t length) {
        if (written + length > expected)
            return false;
        if (std::fwrite(data, 1, length, out) != length) {
            writeError = errno;
            return false;
        }
        written += length;
        return true;
    });
    const bool flushed = std::fflush(out) == 0 && fsync(fileno(out)) == 0;
    const int flushError = flushed ? 0 : errno;
    std::fclose(out);
    if (writeError != 0)
        return std::string("writing failed (") + std::strerror(writeError) + ")";
    if (!completed || written != expected)
        return "the upload was interrupted (" + std::to_string(written) + " of " + std::to_string(expected) + " bytes)";
    if (!flushed)
        return std::string("saving failed (") + std::strerror(flushError) + ")";
    return std::string();
}

/** Percent-encodes @p text for a URL or an RFC 5987 header value. */
std::string PercentEncode(const std::string &text)
{
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char c : text) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) != 0 || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            out += '%';
            out += kHex[byte >> 4];
            out += kHex[byte & 0x0F];
        }
    }
    return out;
}

/**
 * Answers with the file @p path as a download named @p name, read in chunks
 * as the browser takes it. @return False if it is not a readable regular file.
 */
bool SendFile(httplib::Response &res, const std::string &path, const std::string &name)
{
    struct stat info {};
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
        return false;
    std::FILE *raw = std::fopen(path.c_str(), "rb");
    if (!raw)
        return false;
    // Shared so the provider (copied by cpp-httplib) closes it once, when the response is done.
    const std::shared_ptr<std::FILE> file(raw, [](std::FILE *f) { std::fclose(f); });

    // A plain ASCII name for old browsers, and the exact (UTF-8) one for the rest.
    std::string plain;
    for (const char c : name)
        plain += (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E || c == '"' || c == '\\') ? '_' : c;
    res.set_header("Content-Disposition", "attachment; filename=\"" + plain + "\"; filename*=UTF-8''" + PercentEncode(name));
    res.set_header("Cache-Control", "no-store");
    res.set_content_provider(
        static_cast<std::size_t>(info.st_size), "application/octet-stream",
        [file](std::size_t offset, std::size_t length, httplib::DataSink &sink) {
            char buffer[64 * 1024];
            if (fseeko(file.get(), static_cast<off_t>(offset), SEEK_SET) != 0)
                return false;
            const std::size_t read = std::fread(buffer, 1, std::min(length, sizeof(buffer)), file.get());
            return read > 0 && sink.write(buffer, read);
        });
    return true;
}

/** Largest mod data archive accepted (2 GB: files past that need large-file support on the TV). */
constexpr std::uint64_t kMaxModArchiveBytes = 2000ull * 1024ull * 1024ull;

const char *UsbLinkName(UsbLink link)
{
    switch (link) {
        case UsbLink::Symlink: return "link";
        case UsbLink::BindMount: return "bind";
        case UsbLink::None: break;
    }
    return "none";
}

} // namespace

std::string WebServer::LoadOrCreatePin(const std::string &dataFolder)
{
    const std::string path = dataFolder + "/" + kPinFile;
    std::ifstream in(path);
    std::string pin;
    if (in >> pin && pin.size() == 4 &&
        std::all_of(pin.begin(), pin.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
        return pin;

    std::random_device device;
    std::uniform_int_distribution<int> digits(0, 9999);
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%04d", digits(device));
    std::ofstream(path) << buffer << "\n";
    return buffer;
}

WebServer::WebServer(Library &library, Usb &usb, Downloader &downloader, Swap &swap, std::string pin,
                     std::string version)
    : library_(library), usb_(usb), downloader_(downloader), swap_(swap), pin_(std::move(pin)), version_(std::move(version))
{
}

WebServer::~WebServer()
{
    Stop();
}

bool WebServer::Start()
{
    if (server_)
        return port_.load() != 0;
    server_                 = std::make_unique<httplib::Server>();
    server_->new_task_queue = [] { return new httplib::ThreadPool(CPPHTTPLIB_THREAD_POOL_COUNT); };
    server_->set_payload_max_length(std::numeric_limits<std::size_t>::max());
    server_->set_read_timeout(30, 0);
    server_->set_write_timeout(30, 0);
    RegisterRoutes();

    for (int attempt = 0; attempt < kPortAttempts; ++attempt) {
        const int port = kPreferredPort + attempt;
        if (server_->bind_to_port("0.0.0.0", port)) {
            port_.store(static_cast<std::uint16_t>(port));
            thread_ = std::thread([this] { server_->listen_after_bind(); });
            std::printf("[Webmon] upload page on port %d\n", port);
            return true;
        }
    }
    server_.reset();
    return false;
}

void WebServer::Stop()
{
    if (!server_)
        return;
    server_->stop();
    if (thread_.joinable())
        thread_.join();
    server_.reset();
    port_.store(0);
}

std::string WebServer::Address() const
{
    const std::string lan = FindLanAddress();
    if (lan.empty() || port_.load() == 0)
        return std::string();
    return "http://" + lan + ":" + std::to_string(port_.load());
}

void WebServer::SetPendingPick(const std::string &kind)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pendingPick_ = kind;
}

std::string WebServer::StatusJson()
{
    const RootStatus root = usb_.Root();
    const DownloadStatus download = downloader_.Status();
    const SwapStatus swap         = swap_.Status();
    const MemoryInfo memory       = ReadMemory();
    const std::vector<LibraryFile> games = library_.List(FileKind::Love);
    std::string pendingPick;
    std::string lastEvent;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingPick = pendingPick_;
        lastEvent   = lastEvent_;
    }

    std::ostringstream out;
    out << "{\"version\":" << json::Quote(version_)
        << ",\"tvFreeBytes\":" << FreeBytes(library_.DataFolder())
        << ",\"memory\":{\"processMb\":" << memory.processMb << ",\"availableMb\":" << memory.availableMb
        << ",\"totalMb\":" << memory.totalMb << ",\"swapFreeMb\":" << memory.swapFreeMb << "}"
        << ",\"swap\":{\"possible\":" << json::Bool(swap.possible) << ",\"exists\":" << json::Bool(swap.exists)
        << ",\"active\":" << json::Bool(swap.active) << ",\"busy\":" << json::Bool(swap.busy)
        << ",\"sizeMb\":" << swap.sizeMb << ",\"message\":" << json::Quote(swap.message) << "}"
        << ",\"game\":" << (games.empty() ? "null" : "{\"name\":" + json::Quote(games[0].name) +
                                                         ",\"source\":" + json::Quote(games[0].source) + "}")
        << ",\"restartSuggested\":" << json::Bool(restartSuggested_.load())
        << ",\"pendingPick\":" << json::Quote(pendingPick)
        << ",\"lastEvent\":" << json::Quote(lastEvent)
        << ",\"curl\":" << json::Bool(!Downloader::CurlPath().empty())
        << ",\"download\":{\"active\":" << json::Bool(download.active) << ",\"succeeded\":"
        << json::Bool(download.succeeded) << ",\"file\":" << json::Quote(download.file)
        << ",\"received\":" << download.received << ",\"total\":" << download.total
        << ",\"message\":" << json::Quote(download.message) << "}"
        << ",\"root\":{\"available\":" << json::Bool(root.Available()) << ",\"detail\":" << json::Quote(root.detail)
        << "}"
        << ",\"usb\":{\"link\":\"" << UsbLinkName(library_.ActiveUsbLink()) << "\",\"drive\":"
        << json::Quote(library_.ActiveUsbDrive()) << ",\"writable\":" << json::Bool(library_.UsbWritable())
        << ",\"gameDataOnUsb\":" << json::Bool(library_.GameDataOnUsb()) << "}"
        << ",\"drives\":[";
    bool first = true;
    for (const UsbDrive &drive : usb_.ListDrives()) {
        out << (first ? "" : ",") << "{\"mount\":" << json::Quote(drive.mountPoint)
            << ",\"label\":" << json::Quote(drive.label) << ",\"spaceKnown\":" << json::Bool(drive.spaceKnown)
            << ",\"freeBytes\":" << drive.freeBytes << ",\"totalBytes\":" << drive.totalBytes
            << ",\"readable\":" << json::Bool(drive.readable) << ",\"writable\":" << json::Bool(drive.writable)
            << ",\"hasWebmon\":" << json::Bool(drive.hasWebmon) << "}";
        first = false;
    }
    out << "]}";
    return out.str();
}

void WebServer::RegisterRoutes()
{
    httplib::Server &server = *server_;

    // Every request: private networks only, and /api needs the PIN shown on the TV.
    server.set_pre_routing_handler([this](const httplib::Request &req, httplib::Response &res) {
        if (!IsPrivateNetworkAddress(req.remote_addr)) {
            Fail(res, 403, "Only devices on the local network may connect.");
            return httplib::Server::HandlerResponse::Handled;
        }
        if (req.path.rfind("/api/", 0) == 0 && req.get_header_value(kPinHeader) != pin_) {
            Fail(res, 401, "Wrong or missing PIN. Use the PIN shown on the TV.");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    server.Get("/", [](const httplib::Request &, httplib::Response &res) {
        res.set_header("Cache-Control", "no-store");
        res.set_content(kWebPageHtml, "text/html; charset=utf-8");
    });

    server.Get("/api/status", [this](const httplib::Request &, httplib::Response &res) {
        res.set_content(StatusJson(), kJsonType);
    });

    server.Get("/api/files", [this](const httplib::Request &req, httplib::Response &res) {
        FileKind kind;
        if (!Library::ParseKind(req.get_param_value("kind"), &kind)) {
            Fail(res, 400, "Unknown kind.");
            return;
        }
        std::ostringstream out;
        out << "[";
        bool first = true;
        for (const LibraryFile &file : library_.List(kind)) {
            out << (first ? "" : ",") << "{\"name\":" << json::Quote(file.name) << ",\"path\":" << json::Quote(file.path)
                << ",\"source\":" << json::Quote(file.source) << ",\"size\":" << file.size
                << ",\"removable\":" << json::Bool(file.removable) << "}";
            first = false;
        }
        out << "]";
        res.set_content(out.str(), kJsonType);
    });

    server.Put("/api/files/:kind/:name", [this](const httplib::Request &req, httplib::Response &res,
                                                const httplib::ContentReader &reader) {
        FileKind kind;
        const std::string name = req.path_params.at("name");
        if (!Library::ParseKind(req.path_params.at("kind"), &kind) || !Library::AcceptsName(kind, name)) {
            Fail(res, 400, "That file name or type is not accepted here.");
            return;
        }
        std::uint64_t size = 0;
        if (!ContentLength(req, &size) || size == 0 || size > kMaxUploadBytes) {
            Fail(res, 411, "The upload needs a size between 1 byte and 512 MB.");
            return;
        }
        const bool preferUsb     = req.get_param_value("dest") != "tv";
        const std::string folder = library_.UploadFolder(kind, preferUsb);
        // Straight into the destination when the app can write there; otherwise into TV
        // storage first, and root moves it (a drive the app can only reach through root).
        const bool direct = Usb::CanWrite(folder);
        const std::string temporary = direct ? folder + "/." + name + ".part" : library_.IncomingFolder() + "/" + name + ".part";
        const std::string streamError = StreamBodyToFile(reader, temporary, size);
        if (!streamError.empty()) {
            unlink(temporary.c_str());
            const std::string where = direct ? folder : "TV storage (on its way to " + folder + ")";
            std::fprintf(stderr, "[Webmon] upload of %s to %s: %s\n", name.c_str(), where.c_str(), streamError.c_str());
            Fail(res, 500, "Could not store " + name + " in " + where + ": " + streamError + ".");
            return;
        }
        const std::string error = library_.Place(temporary, folder, name);
        if (!error.empty()) {
            Fail(res, 500, error);
            return;
        }
        if (kind == FileKind::Love)
            restartSuggested_.store(true);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastEvent_ = "Received " + name;
        }
        std::printf("[Webmon] received %s into %s\n", name.c_str(), folder.c_str());
        Ok(res, "Stored " + name + ".");
    });

    // A listed file, as a download. Only paths the list for that kind gives are served.
    server.Get("/api/files/content", [this](const httplib::Request &req, httplib::Response &res) {
        FileKind kind;
        if (!Library::ParseKind(req.get_param_value("kind"), &kind)) {
            Fail(res, 400, "Unknown kind.");
            return;
        }
        const std::string path = req.get_param_value("path");
        const std::vector<LibraryFile> files = library_.List(kind);
        const auto file = std::find_if(files.begin(), files.end(), [&](const LibraryFile &f) { return f.path == path; });
        if (file == files.end()) {
            Fail(res, 404, "That file is not in the list.");
            return;
        }
        // On a drive only root can read, MakeReadable() copies it into TV storage first.
        const std::string readable = library_.MakeReadable(file->path);
        if (readable.empty() || !SendFile(res, readable, file->name))
            Fail(res, 500, "Could not read " + file->name + ".");
    });

    server.Delete("/api/files", [this](const httplib::Request &req, httplib::Response &res) {
        const std::string error = library_.Remove(req.get_param_value("path"));
        if (!error.empty()) {
            Fail(res, 400, error);
            return;
        }
        Ok(res, "Deleted.");
    });

    server.Post("/api/download", [this](const httplib::Request &req, httplib::Response &res) {
        if (Downloader::CurlPath().empty()) {
            Fail(res, 501, "This TV has no curl, so it cannot download by itself. Upload the .love instead.");
            return;
        }
        const std::string url = req.get_param_value("url");
        const bool started    = url.empty() ? downloader_.StartLatest() : downloader_.StartUrl(url);
        if (!started) {
            Fail(res, 409, url.empty() ? "A download is already running." : "That download is not allowed or one is already running.");
            return;
        }
        restartSuggested_.store(true);
        Ok(res, "Download started.");
    });

    server.Post("/api/usb/webmon", [this](const httplib::Request &req, httplib::Response &res) {
        const std::string mount = req.get_param_value("mount");
        if (!usb_.IsUsbMount(mount)) {
            Fail(res, 400, "That is not a mounted USB drive.");
            return;
        }
        std::string error = usb_.CreateWebmonFolder(mount, Library::UsbSubFolders());
        if (error.empty())
            error = library_.LinkUsb();
        if (error.empty())
            swap_.UseDrive(library_.ActiveUsbDrive());
        if (!error.empty()) {
            Fail(res, 500, error);
            return;
        }
        restartSuggested_.store(true);
        Ok(res, "Webmon folder ready. Restart Webmon so the game keeps its data there.");
    });

    server.Get("/api/mod-storage", [this](const httplib::Request &, httplib::Response &res) {
        std::ostringstream out;
        out << "[";
        bool first = true;
        for (const ModStorageFolder &folder : library_.ModStorage()) {
            out << (first ? "" : ",") << "{\"game\":" << json::Quote(folder.game)
                << ",\"playthrough\":" << json::Quote(folder.playthrough) << ",\"bytes\":" << folder.bytes
                << ",\"modified\":" << folder.modified << ",\"newest\":" << json::Bool(folder.newest) << "}";
            first = false;
        }
        out << "]";
        res.set_content(out.str(), kJsonType);
    });

    server.Delete("/api/mod-storage", [this](const httplib::Request &req, httplib::Response &res) {
        const std::string error =
            library_.RemoveModStorage(req.get_param_value("game"), req.get_param_value("playthrough"));
        if (!error.empty()) {
            Fail(res, 400, error);
            return;
        }
        Ok(res, "Deleted.");
    });

    // A .zip of mod data (such as a voxel map cache built on a computer) for one playthrough.
    server.Put("/api/mod-storage", [this](const httplib::Request &req, httplib::Response &res,
                                          const httplib::ContentReader &reader) {
        const std::string game        = req.get_param_value("game");
        const std::string playthrough = req.get_param_value("playthrough");
        std::uint64_t size            = 0;
        if (!ContentLength(req, &size) || size == 0 || size > kMaxModArchiveBytes) {
            Fail(res, 411, "The upload needs a size between 1 byte and 2000 MB.");
            return;
        }
        std::unique_lock<std::mutex> busy(modUploadMutex_, std::try_to_lock);
        if (!busy.owns_lock()) {
            Fail(res, 409, "Another mod data upload is being unpacked. Try again when it is done.");
            return;
        }
        // The archive goes beside the mod data, on the same drive, and is deleted once unpacked.
        const std::string root = library_.ModStorageRoot();
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        if (FreeBytes(root) < size * 2) {
            Fail(res, 507, "Not enough free space for the archive and its unpacked files.");
            return;
        }
        const std::string archive = root + "/.webmon-upload.zip";
        std::string error = StreamBodyToFile(reader, archive, size);
        std::string summary;
        if (error.empty())
            error = library_.InstallModStorage(archive, game, playthrough, &summary);
        else
            error = "Could not store the upload: " + error + ".";
        unlink(archive.c_str());
        if (!error.empty()) {
            std::fprintf(stderr, "[Webmon] mod data upload for %s/%s: %s\n", game.c_str(), playthrough.c_str(),
                         error.c_str());
            Fail(res, 400, error);
            return;
        }
        // A running mod may still hold the old data in memory and write it back.
        restartSuggested_.store(true);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastEvent_ = summary;
        }
        Ok(res, summary);
    });

    server.Get("/api/game-saves", [this](const httplib::Request &, httplib::Response &res) {
        std::ostringstream out;
        out << "[";
        bool first = true;
        for (const GameSaveFile &save : library_.GameSaves()) {
            out << (first ? "" : ",") << "{\"path\":" << json::Quote(save.relative) << ",\"size\":" << save.size
                << ",\"modified\":" << save.modified << "}";
            first = false;
        }
        out << "]";
        res.set_content(out.str(), kJsonType);
    });

    server.Get("/api/game-saves/content", [this](const httplib::Request &req, httplib::Response &res) {
        const std::string relative = req.get_param_value("path");
        const std::string path     = library_.GameSavePath(relative);
        // saves/yellow/slot1.lua downloads as yellow-slot1.lua; exports keep their own names.
        std::string name = relative;
        if (name.rfind("exports/", 0) == 0) {
            name.erase(0, name.rfind('/') + 1);
        } else {
            if (name.rfind("saves/", 0) == 0)
                name.erase(0, std::strlen("saves/"));
            std::replace(name.begin(), name.end(), '/', '-');
        }
        if (path.empty() || !SendFile(res, path, name))
            Fail(res, 404, "That save is not in the list.");
    });

    server.Post("/api/swap", [this](const httplib::Request &req, httplib::Response &res) {
        std::uint32_t size = 0;
        try {
            size = static_cast<std::uint32_t>(std::stoul(req.get_param_value("size")));
        } catch (const std::exception &) {
            size = 0;
        }
        if (!swap_.Create(size)) {
            Fail(res, 409, "A swap file needs root, the USB Webmon folder and a size of " +
                               std::to_string(Swap::kMinSizeMb) + "-" + std::to_string(Swap::kMaxSizeMb) +
                               " MB (or one is being made).");
            return;
        }
        Ok(res, "Creating the swap file.");
    });

    server.Delete("/api/swap", [this](const httplib::Request &, httplib::Response &res) {
        if (!swap_.Remove()) {
            Fail(res, 409, "There is no swap file to remove, or it is busy.");
            return;
        }
        Ok(res, "Removing the swap file.");
    });
}

} // namespace webmon
