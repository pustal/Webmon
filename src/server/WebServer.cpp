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
#include "webos/Usb.h"

// Four workers: the page uploads one file at a time and polls the status alongside.
#define CPPHTTPLIB_THREAD_POOL_COUNT 4
#include <httplib.h>

#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <limits>
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
/** Largest upload accepted (a .love is ~100 MB; GBA ROMs are at most 32 MB). */
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
 * bytes, and flushes it to storage. @return True if all of it was written.
 */
bool StreamBodyToFile(const httplib::ContentReader &reader, const std::string &path, std::uint64_t expected)
{
    std::FILE *out = std::fopen(path.c_str(), "wb");
    if (!out)
        return false;
    std::uint64_t written = 0;
    const bool completed  = reader([&](const char *data, std::size_t length) {
        if (written + length > expected)
            return false;
        if (std::fwrite(data, 1, length, out) != length)
            return false;
        written += length;
        return true;
    });
    const bool flushed = std::fflush(out) == 0 && fsync(fileno(out)) == 0;
    std::fclose(out);
    return completed && flushed && written == expected;
}

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

WebServer::WebServer(Library &library, Usb &usb, Downloader &downloader, std::string pin, std::string version)
    : library_(library), usb_(usb), downloader_(downloader), pin_(std::move(pin)), version_(std::move(version))
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
        << ",\"modsOnUsb\":" << json::Bool(library_.ModsOnUsb()) << "}"
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
        const bool preferUsb       = req.get_param_value("dest") != "tv";
        const std::string folder   = library_.UploadFolder(kind, preferUsb);
        const std::string temporary = library_.IncomingFolder() + "/" + name + ".part";
        if (!StreamBodyToFile(reader, temporary, size)) {
            unlink(temporary.c_str());
            Fail(res, 500, "The file could not be stored (is the storage full?).");
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
        std::string error = usb_.CreateWebmonFolder(mount, { "roms", "saves", "mod-zips", "carts", "mods" });
        if (error.empty())
            error = library_.LinkUsb();
        if (!error.empty()) {
            Fail(res, 500, error);
            return;
        }
        restartSuggested_.store(true);
        Ok(res, "Webmon folder ready. Restart Webmon so the game keeps its mods there.");
    });
}

} // namespace webmon
