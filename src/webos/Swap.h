/**
 * @file Swap.h
 * @brief Optional swap file on the USB drive, for memory-hungry mods.
 *
 * webOS restarts the app in the foreground when the TV runs low on free RAM
 * (heavy mods such as voxel world builders can get there). With root, Webmon
 * can give the kernel a swap file in the USB Webmon folder: it is created from
 * the upload page, switched on whenever Webmon starts and off when it closes.
 * Flash drives are slow and wear with writes, so it is opt-in.
 */
#ifndef WEBMON_SWAP_H
#define WEBMON_SWAP_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace webmon
{

class Usb;

/** What the page shows about the swap file. */
struct SwapStatus {
    bool possible = false;      ///< Root and a writable-by-root USB Webmon folder are available.
    bool exists   = false;      ///< The swap file is on the drive.
    bool active   = false;      ///< The kernel is using it.
    bool busy     = false;      ///< Being created or removed.
    std::uint32_t sizeMb = 0;   ///< Size of the file.
    std::string message;        ///< Last result or error, for people.
};

class Swap
{
public:
    /** Name of the swap file inside <drive>/Webmon. */
    static constexpr const char *kFileName = "webmon.swap";
    /** Sizes the page may ask for. */
    static constexpr std::uint32_t kMinSizeMb = 256;
    static constexpr std::uint32_t kMaxSizeMb = 2048;

    explicit Swap(Usb &usb);
    ~Swap();

    Swap(const Swap &)            = delete;
    Swap &operator=(const Swap &) = delete;

    /** Points at the USB drive in use (empty: none) and switches an existing swap file on. */
    void UseDrive(const std::string &mountPoint);

    /** Creates and switches on a swap file of @p sizeMb in the background. @return False if not possible now. */
    bool Create(std::uint32_t sizeMb);

    /** Switches the swap file off and deletes it, in the background. @return False if not possible now. */
    bool Remove();

    /** Switches the swap file off (when Webmon closes; the file stays). */
    void Deactivate();

    SwapStatus Status();

private:
    std::string FilePath() const;
    bool IsActive(const std::string &path) const;
    void SetMessage(const std::string &message);
    void RunInBackground(void (Swap::*job)(std::uint32_t), std::uint32_t argument);
    void CreateJob(std::uint32_t sizeMb);
    void RemoveJob(std::uint32_t unused);

    Usb &usb_;
    mutable std::mutex mutex_;
    std::string mountPoint_;
    std::string message_;
    std::atomic<bool> busy_{ false };
    std::thread worker_;
};

} // namespace webmon

#endif // WEBMON_SWAP_H
