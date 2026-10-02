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
    // "folder" for directories, otherwise a lowercase format id such as "xex",
    // "stfs", "ini" or "file". Drives icons and the expanded view in the UI.
    std::string kind = "file";
    std::uint64_t size = 0;
    std::int64_t modified = 0; // seconds since the Unix epoch, 0 = unknown
};

// One labelled value in the expanded (details) view.
struct Property {
    std::string label;
    std::string value;
};

struct PropertyGroup {
    std::string title;
    std::vector<Property> items;
};

// Everything the UI shows in the expanded view of a file/folder, or of the
// filesystem object itself.
struct Details {
    std::string title;
    std::string subtitle;
    std::string kind;
    std::string notice; // optional banner, e.g. "Sample data"
    std::vector<PropertyGroup> groups;
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
    Inspect     = 1u << 8,
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

    // Expanded view of a file/folder (needs Capability::Inspect).
    virtual Status describe(const std::string& path, Details& out) const;
    // Expanded view of the filesystem object itself (needs Capability::Inspect).
    virtual Status describeFileSystem(Details& out) const;

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

// Human readable names of the operations set in `caps` (for the details view).
std::vector<std::string> capabilityNames(Capability caps);

// Maps a file name to a kind id ("xex", "ini", ...); "file" if unknown.
std::string kindFromName(const std::string& name);
// Human readable label for a kind id ("Xbox 360 executable", ...).
std::string kindLabel(const std::string& kind);

// Helpers for virtual paths.
std::string joinPath(const std::string& dir, const std::string& name);
std::string parentPath(const std::string& path);

} // namespace unnamed::core
