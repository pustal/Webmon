/**
 * @file Downloader.h
 * @brief Downloads gen1recomp's .love straight to the TV with the TV's curl.
 *
 * gen1recomp itself also uses curl for its mod catalogue and updates, so the
 * same check (CurlAvailable) tells the player whether those work too.
 * Only release assets of github.com/bryanthaboi/gen1recomp are downloaded.
 */
#ifndef WEBMON_DOWNLOADER_H
#define WEBMON_DOWNLOADER_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace webmon
{

/** Progress of the current or last download. */
struct DownloadStatus {
    bool active = false;          ///< A download is running.
    bool succeeded = false;       ///< The last download finished and was stored.
    std::string file;             ///< File name being / last downloaded.
    std::uint64_t received = 0;   ///< Bytes so far.
    std::uint64_t total = 0;      ///< Expected bytes (0 if unknown).
    std::string message;          ///< Last result or error, for people.
};

class Downloader
{
public:
    /** The project whose releases may be downloaded. */
    static constexpr const char *kReleasesUrl = "https://github.com/bryanthaboi/gen1recomp/releases";

    /**
     * @param incomingFolder where the download is written while it runs
     * @param place moves the finished file into place: (temporary path, file name) -> error or empty
     */
    Downloader(std::string incomingFolder, std::function<std::string(const std::string &, const std::string &)> place);
    ~Downloader();

    Downloader(const Downloader &)            = delete;
    Downloader &operator=(const Downloader &) = delete;

    /** Path of the TV's curl, or empty if it has none. */
    static std::string CurlPath();

    /** Starts downloading the latest release's .love. @return False if one is already running or curl is missing. */
    bool StartLatest();

    /**
     * Starts downloading @p url, which must be a .love asset under kReleasesUrl/download/.
     * @return False if the URL is not allowed, a download is running or curl is missing.
     */
    bool StartUrl(const std::string &url);

    /** Thread-safe copy of the progress. */
    DownloadStatus Status() const;

private:
    bool Start(const std::string &url);
    void Run(std::string url);
    void Finish(bool succeeded, const std::string &message);

    const std::string incomingFolder_;
    const std::function<std::string(const std::string &, const std::string &)> place_;

    mutable std::mutex mutex_;
    DownloadStatus status_;
    std::thread worker_;
    std::atomic<bool> running_{ false };
};

} // namespace webmon

#endif // WEBMON_DOWNLOADER_H
