#pragma once

// Physical drives: listing them and opening them as a BlockDevice.
//
// The GUI never runs as root. A drive is opened directly when the user may
// read it (e.g. in the "disk" group); otherwise a DriveAccess method asks the
// system: udisks2's OpenDevice on Linux (polkit asks for a password and hands
// back a file descriptor). Windows (an elevated helper) and macOS (authopen)
// are planned behind the same interface, see docs/ROADMAP.md.

#include "core/BlockDevice.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace unnamed::core {

struct DriveInfo {
    std::string path;           // device node, e.g. /dev/sdb
    std::string model;          // "WDC WD2500BEVS-..." when known
    std::string vendor;
    std::string serial;
    std::uint64_t size = 0;     // bytes
    bool removable = false;
    bool readOnly = false;      // the kernel reports the device read-only
    bool inUse = false;         // the drive or one of its partitions is mounted
    bool readable = false;      // this process can open it without asking
};

// Where the Linux enumeration looks (tests point these at a fake tree).
struct LinuxDrivePaths {
    std::filesystem::path sysBlock = "/sys/block";
    std::filesystem::path dev = "/dev";
    std::filesystem::path mounts = "/proc/self/mounts";
    bool includeLoop = false;   // loop devices (disk images attached with losetup)
};

// Lists whole disks from sysfs (no partitions, RAM disks, zram, device-mapper,
// or loop devices unless asked). Empty on other systems.
std::vector<DriveInfo> listLinuxDrives(const LinuxDrivePaths& paths = {});
// The drives of this system (Linux only for now).
std::vector<DriveInfo> listDrives(bool includeLoop = false);
// Whether listDrives() is implemented on this platform.
bool drivesSupported();

// Opens a device node (or any file) with the process's own rights. Read-only
// unless `writable`; on Linux a writable open is exclusive (O_EXCL), which the
// kernel refuses while the drive is mounted.
Result<std::shared_ptr<BlockDevice>> openDeviceNode(const std::string& path, bool writable);
// Wraps an open file descriptor, e.g. one udisks2 handed over (an Android
// file descriptor later). Takes ownership of `fd`.
Result<std::shared_ptr<BlockDevice>> deviceFromDescriptor(int fd, std::string description, bool writable);

// One way to get at a drive.
class DriveAccess {
public:
    virtual ~DriveAccess() = default;
    virtual std::string name() const = 0;
    virtual Result<std::shared_ptr<BlockDevice>> open(const std::string& path, bool writable) = 0;
};

// The ways this platform offers, in the order to try them: the process's own
// rights first, then udisks2 on Linux (when built with it).
std::vector<std::unique_ptr<DriveAccess>> driveAccessMethods();
// Tries each method until one opens the drive. While a device opened for
// writing is in use, opening the same drive for writing again returns it (a
// second exclusive open would fail, e.g. for another partition of the drive);
// BlockDevice reads and writes at an offset, so places may share it.
Result<std::shared_ptr<BlockDevice>> openDrive(const std::string& path, bool writable);

// A drive opened for use: writable when possible (exclusive open), otherwise
// read-only with the reason (mounted, read-only device, permission refused).
struct OpenedDrive {
    std::shared_ptr<BlockDevice> device;
    bool writable = false;
    std::string readOnlyReason; // why writing was refused, when !writable
};
using DriveOpener = std::function<Result<std::shared_ptr<BlockDevice>>(const std::string& path, bool writable)>;
// Drives open read-write by default (images stay read-only): tries a writable
// open, then falls back to read-only. `open` defaults to openDrive().
Result<OpenedDrive> openDriveForUse(const std::string& path, const DriveOpener& open = {});

} // namespace unnamed::core
