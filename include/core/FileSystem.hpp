#pragma once

#include "core/Status.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace unnamed::core {

enum class EntryType { File, Directory };

struct Entry {
    std::string name;
    EntryType type = EntryType::File;
    std::uint64_t size = 0;
};

// Operations a filesystem may support (see "FS Functions" in the README).
enum class Capability : unsigned {
    Browse      = 1u << 0,
    Extract     = 1u << 1,
    Inject      = 1u << 2,
    Replace     = 1u << 3,
    Remove      = 1u << 4,
    Clear       = 1u << 5,
    HealthCheck = 1u << 6,
    Repair      = 1u << 7,
};

constexpr Capability operator|(Capability a, Capability b) {
    return static_cast<Capability>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

constexpr bool hasCapability(Capability set, Capability flag) {
    return (static_cast<unsigned>(set) & static_cast<unsigned>(flag)) != 0;
}

// Abstract filesystem (host directory, FATX, STFS, NAND, ...).
//
// Paths are virtual, '/'-separated and rooted at "/". Only list() is required;
// every other operation defaults to "unsupported" and should be advertised via
// capabilities() when overridden, so the GUI can enable/disable actions.
class FileSystem {
public:
    virtual ~FileSystem() = default;

    virtual std::string name() const = 0;
    virtual Capability capabilities() const { return Capability::Browse; }

    virtual Status list(const std::string& path, std::vector<Entry>& out) const = 0;

    // Copy a file/directory out of the filesystem to a host path.
    virtual Status extract(const std::string& path, const std::filesystem::path& hostDest);
    // Copy a host file into the directory `dir` of the filesystem.
    virtual Status inject(const std::string& dir, const std::filesystem::path& hostSource);
    // Overwrite an existing file with the contents of a host file.
    virtual Status replace(const std::string& path, const std::filesystem::path& hostSource);
    virtual Status remove(const std::string& path);
    virtual Status clear(const std::string& path);
    virtual Status healthCheck(std::string& report);
    virtual Status repair(std::string& report);
};

// Helpers for virtual paths.
std::string joinPath(const std::string& dir, const std::string& name);
std::string parentPath(const std::string& path);

} // namespace unnamed::core
