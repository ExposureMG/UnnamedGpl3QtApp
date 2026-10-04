#pragma once

#include "core/FileSystem.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace unnamed::core {

struct TransferProgress {
    std::uint64_t bytesDone = 0;
    std::uint64_t bytesTotal = 0; // 0 if unknown
    std::string current;          // file being copied
};

// Called regularly during a transfer; return false to cancel.
using ProgressFn = std::function<bool(const TransferProgress&)>;

struct TransferOptions {
    bool overwrite = false; // replace existing files instead of failing
    ProgressFn progress;
};

// Copies a file or a whole folder tree between any two filesystems using only
// the FileSystem interface (list/stat/openRead/openWrite/makeDirectory). This is
// extract (fs -> host), inject (host -> fs) and fs -> fs copy in one place.
// Existing folders are merged; a cancelled transfer leaves no partial file.
Status copyTree(const FileSystem& source, const std::string& sourcePath, FileSystem& destination,
                const std::string& destinationPath, const TransferOptions& options = {});

} // namespace unnamed::core
