/**
 * @file Downloader.cpp
 * @brief Implementation of Downloader.h.
 */
#include "Downloader.h"

#include "Process.h"

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <sstream>

namespace webmon
{
namespace
{

constexpr std::array<const char *, 3> kCurlPaths = { "/usr/bin/curl", "/bin/curl", "/usr/local/bin/curl" };
constexpr const char *kUserAgent = "Webmon (gen1recomp for webOS)";

/** Size of @p path, or 0. */
std::uint64_t FileSize(const std::string &path)
{
    struct stat info {};
    return stat(path.c_str(), &info) == 0 ? static_cast<std::uint64_t>(info.st_size) : 0;
}

/** The last Content-Length in curl -I output (after redirects), or 0. */
std::uint64_t LastContentLength(const std::string &headers)
{
    std::uint64_t length = 0;
    std::istringstream lines(headers);
    std::string line;
    while (std::getline(lines, line)) {
        std::string lower;
        for (const char c : line)
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.rfind("content-length:", 0) == 0) {
            try {
                length = std::stoull(lower.substr(15));
            } catch (const std::exception &) {
                length = 0;
            }
        }
    }
    return length;
}

/** True if @p tag looks like a release tag (letters, digits, dots, dashes). */
bool IsSafeTag(const std::string &tag)
{
    if (tag.empty() || tag.size() > 40)
        return false;
    for (const char c : tag) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '_')
            return false;
    }
    return true;
}

} // namespace

Downloader::Downloader(std::function<std::string()> workFolder,
                       std::function<std::string(const std::string &, const std::string &)> place)
    : workFolder_(std::move(workFolder)), place_(std::move(place))
{
}

Downloader::~Downloader()
{
    if (worker_.joinable())
        worker_.join();
}

std::string Downloader::CurlPath()
{
    for (const char *path : kCurlPaths) {
        if (IsExecutable(path))
            return path;
    }
    return std::string();
}

DownloadStatus Downloader::Status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

bool Downloader::StartLatest()
{
    return Start(std::string());
}

bool Downloader::StartUrl(const std::string &url)
{
    const std::string prefix = std::string(kReleasesUrl) + "/download/";
    if (url.rfind(prefix, 0) != 0 || url.size() < 6 || url.compare(url.size() - 5, 5, ".love") != 0)
        return false;
    // tag/file.love, with nothing unexpected in either part.
    const std::string rest  = url.substr(prefix.size());
    const std::size_t slash = rest.find('/');
    if (slash == std::string::npos || rest.find('/', slash + 1) != std::string::npos)
        return false;
    if (!IsSafeTag(rest.substr(0, slash)) || !IsSafeTag(rest.substr(slash + 1, rest.size() - slash - 6)))
        return false;
    return Start(url);
}

bool Downloader::Start(const std::string &url)
{
    if (CurlPath().empty() || running_.exchange(true))
        return false;
    if (worker_.joinable())
        worker_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_          = DownloadStatus{};
        status_.active   = true;
        status_.message  = "Starting...";
    }
    worker_ = std::thread([this, url] { Run(url); });
    return true;
}

void Downloader::Finish(bool succeeded, const std::string &message)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.active    = false;
        status_.succeeded = succeeded;
        status_.message   = message;
    }
    std::printf("[Webmon] download: %s\n", message.c_str());
    running_.store(false);
}

void Downloader::Run(std::string url)
{
    const std::string curl = CurlPath();

    // The latest release: follow GitHub's redirect from /releases/latest to /releases/tag/<tag>.
    if (url.empty()) {
        std::string effective;
        RunProcess({ curl, "-sSL", "-A", kUserAgent, "-o", "/dev/null", "-w", "%{url_effective}",
                     std::string(kReleasesUrl) + "/latest" },
                   &effective);
        const std::string marker = "/releases/tag/";
        const std::size_t at     = effective.rfind(marker);
        const std::string tag    = at == std::string::npos ? std::string() : effective.substr(at + marker.size());
        if (!IsSafeTag(tag)) {
            Finish(false, "Could not find the latest release (is the TV online?).");
            return;
        }
        const std::string version = tag[0] == 'v' ? tag.substr(1) : tag;
        url = std::string(kReleasesUrl) + "/download/" + tag + "/gen1recomp-" + version + ".love";
    }

    const std::string name      = url.substr(url.rfind('/') + 1);
    const std::string temporary = workFolder_() + "/." + name + ".part";
    std::string headers;
    RunProcess({ curl, "-sSIL", "-A", kUserAgent, url }, &headers);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.file    = name;
        status_.total   = LastContentLength(headers);
        status_.message = "Downloading " + name;
    }

    // Run curl in the background of this thread and report the file's growth.
    std::vector<std::string> argv = { curl, "-sSfL", "-A", kUserAgent, "-o", temporary, url };
    std::vector<char *> args;
    for (std::string &arg : argv)
        args.push_back(arg.data());
    args.push_back(nullptr);
    const pid_t pid = fork();
    if (pid == 0) {
        execv(args[0], args.data());
        _exit(127);
    }
    if (pid < 0) {
        Finish(false, "Could not start curl.");
        return;
    }
    int status = 0;
    for (;;) {
        const pid_t done = waitpid(pid, &status, WNOHANG);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.received = FileSize(temporary);
        }
        if (done == pid || done < 0)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || FileSize(temporary) == 0) {
        unlink(temporary.c_str());
        Finish(false, "The download of " + name + " failed (curl status " +
                          std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1) + ").");
        return;
    }
    const std::string error = place_(temporary, name);
    if (!error.empty()) {
        Finish(false, error);
        return;
    }
    Finish(true, name + " downloaded. Restart Webmon to play it.");
}

} // namespace webmon
