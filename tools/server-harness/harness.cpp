/**
 * @file harness.cpp
 * @brief Runs Webmon's upload page on a development machine (no TV, no LÖVE)
 *        against a scratch data folder, for testing the API and the page.
 *
 * Build and run: tools/server-harness/run.sh <data folder>
 * The PIN is printed at start. Stop with Ctrl+C.
 */
#include "server/WebServer.h"
#include "webos/Downloader.h"
#include "webos/Library.h"
#include "webos/Usb.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace
{
volatile std::sig_atomic_t g_stop = 0;
void OnSignal(int) { g_stop = 1; }
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <data folder>\n", argv[0]);
        return 1;
    }
    const std::string data = argv[1];
    setenv("XDG_DATA_HOME", (data + "/save").c_str(), 1);

    webmon::Usb usb;
    usb.ProbeRoot();
    webmon::Library library(data, usb);
    library.Prepare();
    webmon::Downloader downloader(
        [&library] {
            const std::string folder = library.UploadFolder(webmon::FileKind::Love, true);
            return webmon::Usb::CanWrite(folder) ? folder : library.IncomingFolder();
        },
        [&library](const std::string &tmp, const std::string &name) {
            return library.Place(tmp, library.UploadFolder(webmon::FileKind::Love, true), name);
        });
    webmon::WebServer server(library, usb, downloader, webmon::WebServer::LoadOrCreatePin(data), "harness");
    if (!server.Start())
        return 1;
    std::printf("PIN %s port %u\n", server.Pin().c_str(), static_cast<unsigned>(server.Port()));
    std::fflush(stdout);

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
    while (!g_stop)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    server.Stop();
    return 0;
}
