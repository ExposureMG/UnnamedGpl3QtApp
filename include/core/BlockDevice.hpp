#pragma once

#include "core/Status.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace unnamed::core {

// Random-access storage a filesystem backend (FATX, later STFS/NAND) lives on:
// a disk image now, a physical drive (Linux block device, Windows
// \\.\PhysicalDriveN, macOS /dev/rdiskN) or an Android file descriptor later.
// Like FileSystem, a device is used by one thread at a time.
class BlockDevice {
public:
    virtual ~BlockDevice() = default;

    virtual std::uint64_t size() const = 0;
    virtual bool writable() const = 0;
    // Reads/writes exactly `size` bytes; false on any error (short read included).
    virtual bool readAt(std::uint64_t offset, void* data, std::size_t size) = 0;
    virtual bool writeAt(std::uint64_t offset, const void* data, std::size_t size) = 0;
    virtual bool flush() = 0;
    // Shown to the user: the image path or the device node.
    virtual std::string description() const = 0;
};

// A disk or partition image (or any file/device node the user may open).
// Read-only unless `writable`.
Result<std::shared_ptr<BlockDevice>> openImageFile(const std::filesystem::path& path, bool writable);

} // namespace unnamed::core
