/**
 * @file Swap.cpp
 * @brief Implementation of Swap.h. Every swap command runs as root.
 */
#include "Swap.h"

#include "Usb.h"

#include <sys/stat.h>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace webmon
{
namespace
{

/** Root commands may take this long: writing a 2 GB file to a slow flash drive. */
constexpr int kLongCommandMs = 15 * 60 * 1000;

} // namespace

Swap::Swap(Usb &usb) : usb_(usb) {}

Swap::~Swap()
{
    if (worker_.joinable())
        worker_.join();
}

std::string Swap::FilePath() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return mountPoint_.empty() ? std::string() : mountPoint_ + "/" + Usb::kWebmonFolder + "/" + kFileName;
}

bool Swap::IsActive(const std::string &path) const
{
    // /proc/swaps: "Filename Type Size Used Priority"
    std::ifstream swaps("/proc/swaps");
    std::string line;
    while (std::getline(swaps, line)) {
        std::istringstream fields(line);
        std::string name;
        if (fields >> name && name == path)
            return true;
    }
    return false;
}

void Swap::SetMessage(const std::string &message)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        message_ = message;
    }
    std::printf("[Webmon] swap: %s\n", message.c_str());
}

void Swap::UseDrive(const std::string &mountPoint)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        mountPoint_ = mountPoint;
    }
    const std::string path = FilePath();
    if (path.empty() || !usb_.Root().Available() || IsActive(path))
        return;
    if (usb_.RunAsRoot("test -f " + Usb::ShellQuote(path))) {
        if (usb_.RunAsRoot("swapon " + Usb::ShellQuote(path)))
            SetMessage("swap file on the USB drive switched on");
        else
            SetMessage("the swap file could not be switched on");
    }
}

SwapStatus Swap::Status()
{
    SwapStatus status;
    const std::string path = FilePath();
    status.possible = !path.empty() && usb_.Root().Available();
    status.busy     = busy_.load();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status.message = message_;
    }
    if (!status.possible)
        return status;
    struct stat info {};
    status.exists = stat(path.c_str(), &info) == 0;
    if (status.exists)
        status.sizeMb = static_cast<std::uint32_t>(info.st_size / (1024 * 1024));
    status.active = IsActive(path);
    return status;
}

void Swap::RunInBackground(void (Swap::*job)(std::uint32_t), std::uint32_t argument)
{
    if (worker_.joinable())
        worker_.join();
    worker_ = std::thread([this, job, argument] {
        (this->*job)(argument);
        busy_.store(false);
    });
}

bool Swap::Create(std::uint32_t sizeMb)
{
    if (sizeMb < kMinSizeMb || sizeMb > kMaxSizeMb || FilePath().empty() || !usb_.Root().Available())
        return false;
    if (busy_.exchange(true))
        return false;
    RunInBackground(&Swap::CreateJob, sizeMb);
    return true;
}

bool Swap::Remove()
{
    if (FilePath().empty() || !usb_.Root().Available())
        return false;
    if (busy_.exchange(true))
        return false;
    RunInBackground(&Swap::RemoveJob, 0);
    return true;
}

void Swap::CreateJob(std::uint32_t sizeMb)
{
    const std::string path   = FilePath();
    const std::string quoted = Usb::ShellQuote(path);
    SetMessage("creating a " + std::to_string(sizeMb) + " MB swap file (this takes a few minutes)...");
    // An existing file is switched off and replaced. dd writes real blocks: swap files must have no holes.
    const std::string command = "swapoff " + quoted + " 2>/dev/null; rm -f " + quoted + "; dd if=/dev/zero of=" +
                                quoted + " bs=1048576 count=" + std::to_string(sizeMb) + " && chmod 600 " + quoted +
                                " && mkswap " + quoted + " && swapon " + quoted;
    std::string output;
    if (usb_.RunAsRoot(command, &output, kLongCommandMs) && IsActive(path)) {
        SetMessage("swap file of " + std::to_string(sizeMb) + " MB created and switched on");
        return;
    }
    usb_.RunAsRoot("rm -f " + quoted);
    // The usual reason: the drive's file system cannot hold swap (NTFS, exFAT on some TVs).
    SetMessage("could not create a swap file on this drive (its file system may not support swap; FAT32 or ext4 work)");
}

void Swap::RemoveJob(std::uint32_t /*unused*/)
{
    const std::string quoted = Usb::ShellQuote(FilePath());
    if (usb_.RunAsRoot("swapoff " + quoted + " 2>/dev/null; rm -f " + quoted))
        SetMessage("swap file removed");
    else
        SetMessage("could not remove the swap file");
}

void Swap::Deactivate()
{
    if (worker_.joinable())
        worker_.join();
    const std::string path = FilePath();
    if (!path.empty() && IsActive(path))
        usb_.RunAsRoot("swapoff " + Usb::ShellQuote(path));
}

} // namespace webmon
