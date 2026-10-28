/**
 * @file WebServer.h
 * @brief Webmon's upload page: a small HTTP server on the TV for phones and
 *        computers on the same network.
 *
 * It answers only private-network addresses, and every /api request must
 * carry the PIN shown on the TV (X-Pin header). The page (web/index.html,
 * embedded) can:
 *   - show root access, USB drives and where Webmon keeps files;
 *   - create a Webmon folder on a USB drive;
 *   - upload and delete the game (.love), ROMs, saves, mod zips and carts;
 *   - make the TV download a gen1recomp release itself.
 *
 * API:
 *   GET    /api/status
 *   GET    /api/files?kind=love|rom|sav|mod|cart|n64
 *   PUT    /api/files/<kind>/<name>[?dest=tv|usb]   body: the file
 *   DELETE /api/files?path=<path from /api/files>
 *   POST   /api/download[?url=<release asset>]      no url: the latest release
 *   POST   /api/usb/webmon?mount=<mount point>
 *   POST   /api/swap?size=<MB>                      create a swap file on the USB drive (root)
 *   DELETE /api/swap                                remove it
 *   GET    /api/mod-storage                         the game's mod data per playthrough
 *   DELETE /api/mod-storage?game=<g>&playthrough=<id>   delete one
 *   PUT    /api/mod-storage?game=<g>&playthrough=<id>   body: a .zip unpacked into it
 *   GET    /api/files/content?kind=<k>&path=<path>      a listed file, as a download
 *   GET    /api/game-saves                              the game's own saves and exports
 *   GET    /api/game-saves/content?path=<relative>      one of them, as a download
 */
#ifndef WEBMON_WEB_SERVER_H
#define WEBMON_WEB_SERVER_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace httplib
{
class Server;
}

namespace webmon
{

class Downloader;
class Library;
class Swap;
class Usb;

class WebServer
{
public:
    /** Port tried first (the Animal Crossing port uses 47811 too; only one app runs at a time). */
    static constexpr std::uint16_t kPreferredPort = 47811;

    WebServer(Library &library, Usb &usb, Downloader &downloader, Swap &swap, std::string pin, std::string version);
    ~WebServer();

    WebServer(const WebServer &)            = delete;
    WebServer &operator=(const WebServer &) = delete;

    /** Binds the first free port in [kPreferredPort, kPreferredPort + 9] and serves on a background thread. */
    bool Start();

    /** Stops the server and joins its thread. */
    void Stop();

    /** The bound port, or 0. */
    std::uint16_t Port() const { return port_.load(); }

    const std::string &Pin() const { return pin_; }

    /** "http://<address>:<port>", or empty when offline or not running. */
    std::string Address() const;

    /**
     * The kind of file the game's picker is waiting for ("rom", "sav", ...),
     * shown on the page; empty when no picker is open.
     */
    void SetPendingPick(const std::string &kind);

    /** Files changed that the game only reads when it starts. */
    bool RestartSuggested() const { return restartSuggested_.load(); }

    /** Reads the PIN stored in @p dataFolder, creating a new random one the first time. */
    static std::string LoadOrCreatePin(const std::string &dataFolder);

private:
    void RegisterRoutes();
    std::string StatusJson();

    Library &library_;
    Usb &usb_;
    Downloader &downloader_;
    Swap &swap_;
    const std::string pin_;
    const std::string version_;

    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    std::atomic<std::uint16_t> port_{ 0 };
    std::atomic<bool> restartSuggested_{ false };

    mutable std::mutex mutex_;
    std::string pendingPick_;
    std::string lastEvent_;
    std::mutex modUploadMutex_; ///< One mod data archive is unpacked at a time
};

} // namespace webmon

#endif // WEBMON_WEB_SERVER_H
