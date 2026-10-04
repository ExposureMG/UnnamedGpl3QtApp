#pragma once

// Xbox 360 FATX filesystems (images now, drives through BlockDevice), backed
// by fatx_core from the FATX fork (extern/FATX). Only built with
// -DUNNAMED_WITH_FATX=ON, which also defines UNNAMED_WITH_FATX.

#include "core/BlockDevice.hpp"
#include "core/FileSystem.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace unnamed::core {

// A FATX partition on a device.
struct FatxPartition {
    std::string table = "file";   // layout: "file" (partition image), "mu", "hd", "kit"
    std::string partition = "x2"; // "sc", "gc", "se1", "se2", "xdv", "x1", "x2"
    std::string name;             // human readable: "Data", "System cache", ...
    std::string tableName;        // "Xbox 360 retail hard disk", ...
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

// The FATX partitions found on a device (an Xbox 360 hard disk gives several,
// a partition image one). Empty if there is none.
std::vector<FatxPartition> probeFatx(const std::shared_ptr<BlockDevice>& device);

// Opens one partition. Writing needs `writable` and a writable device.
Result<std::unique_ptr<FileSystem>> openFatx(std::shared_ptr<BlockDevice> device, const FatxPartition& partition,
                                             bool writable, std::string displayName);

// Creates an empty FATX filesystem on a partition (mkfs.fatx): everything on it
// is lost. The device must be writable and nothing may have it open.
Status formatFatx(const std::shared_ptr<BlockDevice>& device, const FatxPartition& partition, const std::string& label);

} // namespace unnamed::core
