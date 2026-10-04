#include "core/Drives.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <system_error>

#if defined(__unix__) || defined(__APPLE__)
#define UNNAMED_POSIX_DEVICES 1
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef __linux__
#include <linux/fs.h> // BLKGETSIZE64
#include <sys/ioctl.h>
#endif

namespace unnamed::core {

namespace fs = std::filesystem;

namespace {

std::string readLine(const fs::path& p) {
    std::ifstream in(p);
    std::string s;
    std::getline(in, s);
    const auto first = s.find_first_not_of(" \t\r\n");
    const auto last = s.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? std::string() : s.substr(first, last - first + 1);
}

std::uint64_t readNumber(const fs::path& p) {
    const std::string s = readLine(p);
    try {
        return s.empty() ? 0 : std::stoull(s);
    } catch (...) {
        return 0;
    }
}

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

// Whole disks worth offering: SATA/SCSI/USB, NVMe, virtio, MMC/SD, Xen.
bool isCandidate(const std::string& name, bool includeLoop) {
    if (startsWith(name, "loop"))
        return includeLoop;
    for (const char* p : {"sd", "nvme", "vd", "hd", "mmcblk", "xvd"}) {
        if (startsWith(name, p))
            return true;
    }
    return false; // ram, zram, dm-, md, sr (optical), fd, ...
}

// "/dev/sdb1" -> "sdb" for a disk named "sdb": its partitions are the disk
// name followed by digits, or by "p" and digits (nvme0n1p1, mmcblk0p1, loop0p1).
bool isOnDisk(const std::string& device, const std::string& devDir, const std::string& disk) {
    const std::string node = devDir + "/" + disk;
    if (!startsWith(device, node.c_str()))
        return false;
    std::string rest = device.substr(node.size());
    if (rest.empty())
        return true;
    if (rest[0] == 'p')
        rest.erase(0, 1);
    return !rest.empty() && std::all_of(rest.begin(), rest.end(), [](char c) { return c >= '0' && c <= '9'; });
}

#ifdef UNNAMED_POSIX_DEVICES
class FdDevice final : public BlockDevice {
public:
    FdDevice(int fd, std::uint64_t size, bool writable, std::string description)
        : m_fd(fd), m_size(size), m_writable(writable), m_description(std::move(description)) {}
    ~FdDevice() override { ::close(m_fd); }

    std::uint64_t size() const override { return m_size; }
    bool writable() const override { return m_writable; }
    std::string description() const override { return m_description; }

    bool readAt(std::uint64_t offset, void* data, std::size_t size) override {
        if (offset > m_size || size > m_size - offset)
            return false;
        auto* p = static_cast<char*>(data);
        while (size > 0) {
            const ssize_t n = ::pread(m_fd, p, size, static_cast<off_t>(offset));
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                return false;
            p += n;
            offset += static_cast<std::uint64_t>(n);
            size -= static_cast<std::size_t>(n);
        }
        return true;
    }

    bool writeAt(std::uint64_t offset, const void* data, std::size_t size) override {
        if (!m_writable || offset > m_size || size > m_size - offset)
            return false;
        const auto* p = static_cast<const char*>(data);
        while (size > 0) {
            const ssize_t n = ::pwrite(m_fd, p, size, static_cast<off_t>(offset));
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                return false;
            p += n;
            offset += static_cast<std::uint64_t>(n);
            size -= static_cast<std::size_t>(n);
        }
        return true;
    }

    bool flush() override { return !m_writable || ::fsync(m_fd) == 0; }

private:
    int m_fd;
    std::uint64_t m_size;
    bool m_writable;
    std::string m_description;
};

std::string errnoText(int err) { return std::generic_category().message(err); }
#endif

// The process's own rights.
class DirectAccess final : public DriveAccess {
public:
    std::string name() const override { return "direct"; }
    Result<std::shared_ptr<BlockDevice>> open(const std::string& path, bool writable) override {
        return openDeviceNode(path, writable);
    }
};

} // namespace

#ifdef UNNAMED_WITH_UDISKS2
std::unique_ptr<DriveAccess> makeUDisks2Access(); // UDisks2.cpp
#endif

std::vector<DriveInfo> listLinuxDrives(const LinuxDrivePaths& paths) {
    std::vector<DriveInfo> out;
    std::error_code ec;
    if (!fs::is_directory(paths.sysBlock, ec))
        return out;

    // mounted devices, from the mount table
    std::vector<std::string> mounted;
    {
        std::ifstream in(paths.mounts);
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream fields(line);
            std::string device;
            fields >> device;
            if (startsWith(device, "/dev/"))
                mounted.push_back(device);
        }
    }

    for (const auto& entry : fs::directory_iterator(paths.sysBlock, ec)) {
        const std::string name = entry.path().filename().string();
        if (!isCandidate(name, paths.includeLoop))
            continue;
        const fs::path sys = entry.path();
        DriveInfo d;
        d.size = readNumber(sys / "size") * 512; // always in 512-byte sectors
        if (d.size == 0)
            continue; // empty card reader, detached loop device
        d.path = (paths.dev / name).string();
        d.removable = readNumber(sys / "removable") != 0;
        d.readOnly = readNumber(sys / "ro") != 0;
        d.model = readLine(sys / "device" / "model");
        d.vendor = readLine(sys / "device" / "vendor");
        if (startsWith(d.vendor, "0x"))
            d.vendor.clear(); // a PCI id (virtio), not a name
        d.serial = readLine(sys / "device" / "serial");
        if (d.serial.empty())
            d.serial = readLine(sys / "serial");
        if (startsWith(name, "loop") && d.model.empty())
            d.model = "Loop device (" + fs::path(readLine(sys / "loop" / "backing_file")).filename().string() + ")";

        // mounted, or used by device-mapper / RAID (holders) for the disk or a partition
        d.inUse = std::any_of(mounted.begin(), mounted.end(),
                              [&](const std::string& m) { return isOnDisk(m, paths.dev.string(), name); });
        auto hasHolders = [](const fs::path& dir) {
            std::error_code e;
            return fs::is_directory(dir, e) && fs::directory_iterator(dir, e) != fs::directory_iterator();
        };
        if (hasHolders(sys / "holders"))
            d.inUse = true;
        for (const auto& part : fs::directory_iterator(sys, ec)) {
            if (startsWith(part.path().filename().string(), name.c_str()) && hasHolders(part.path() / "holders"))
                d.inUse = true;
        }
#ifdef UNNAMED_POSIX_DEVICES
        d.readable = ::access(d.path.c_str(), R_OK) == 0;
#endif
        out.push_back(std::move(d));
    }
    std::sort(out.begin(), out.end(), [](const DriveInfo& a, const DriveInfo& b) { return a.path < b.path; });
    return out;
}

bool drivesSupported() {
#ifdef __linux__
    return true;
#else
    return false;
#endif
}

std::vector<DriveInfo> listDrives(bool includeLoop) {
#ifdef __linux__
    LinuxDrivePaths paths;
    paths.includeLoop = includeLoop;
    return listLinuxDrives(paths);
#else
    (void)includeLoop;
    return {};
#endif
}

Result<std::shared_ptr<BlockDevice>> deviceFromDescriptor(int fd, std::string description, bool writable) {
#ifdef UNNAMED_POSIX_DEVICES
    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        const int err = errno;
        ::close(fd);
        return Status::failure("Cannot use " + description + ": " + errnoText(err));
    }
    std::uint64_t size = 0;
    if (S_ISBLK(st.st_mode)) {
#ifdef __linux__
        if (::ioctl(fd, BLKGETSIZE64, &size) != 0)
            size = 0;
#endif
        if (size == 0) {
            const off_t end = ::lseek(fd, 0, SEEK_END);
            size = end > 0 ? static_cast<std::uint64_t>(end) : 0;
        }
    } else {
        size = static_cast<std::uint64_t>(st.st_size);
    }
    return std::shared_ptr<BlockDevice>(std::make_shared<FdDevice>(fd, size, writable, std::move(description)));
#else
    (void)fd;
    (void)writable;
    return Status::failure("Cannot use " + description + ": not supported on this system yet");
#endif
}

Result<std::shared_ptr<BlockDevice>> openDeviceNode(const std::string& path, bool writable) {
#ifdef UNNAMED_POSIX_DEVICES
    int flags = (writable ? O_RDWR : O_RDONLY) | O_CLOEXEC;
#ifdef __linux__
    if (writable) {
        struct stat st {};
        if (::stat(path.c_str(), &st) == 0 && S_ISBLK(st.st_mode))
            flags |= O_EXCL; // refused while mounted
    }
#endif
    const int fd = ::open(path.c_str(), flags);
    if (fd < 0) {
        const int err = errno;
        if (err == EBUSY)
            return Status::failure("Cannot open " + path + " for writing: it is in use (mounted?)");
        return Status::failure("Cannot open " + path + (writable ? " for writing: " : ": ") + errnoText(err));
    }
    return deviceFromDescriptor(fd, path, writable);
#else
    (void)writable;
    return Status::failure("Cannot open " + path + ": drive access is not supported on this system yet");
#endif
}

std::vector<std::unique_ptr<DriveAccess>> driveAccessMethods() {
    std::vector<std::unique_ptr<DriveAccess>> out;
    out.push_back(std::make_unique<DirectAccess>());
#ifdef UNNAMED_WITH_UDISKS2
    out.push_back(makeUDisks2Access());
#endif
    return out;
}

Result<std::shared_ptr<BlockDevice>> openDrive(const std::string& path, bool writable) {
    static std::mutex lock;
    static std::map<std::string, std::weak_ptr<BlockDevice>> openForWriting;
    if (writable) {
        std::lock_guard<std::mutex> guard(lock);
        if (auto shared = openForWriting[path].lock())
            return shared;
    }
    std::string errors;
    for (const auto& method : driveAccessMethods()) {
        auto dev = method->open(path, writable);
        if (dev) {
            if (writable) {
                std::lock_guard<std::mutex> guard(lock);
                openForWriting[path] = dev.value();
            }
            return dev;
        }
        errors += (errors.empty() ? "" : "; ") + method->name() + ": " + dev.status().message;
    }
    return Status::failure(errors.empty() ? "Cannot open " + path : errors);
}

} // namespace unnamed::core
