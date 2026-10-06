#pragma once

#include "core/Status.hpp"
#include "core/Stream.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
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
    MakeDirectory = 1u << 9,
    Rename      = 1u << 10,
    Format      = 1u << 11,
};

constexpr Capability operator|(Capability a, Capability b) {
    return static_cast<Capability>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

constexpr bool hasCapability(Capability set, Capability flag) {
    return (static_cast<unsigned>(set) & static_cast<unsigned>(flag)) != 0;
}

// Abstract filesystem (host directory, FATX, STFS, console over the network, ...).
//
// Paths are virtual, UTF-8, '/'-separated and rooted at "/". Only list() is
// required; every other operation defaults to "unsupported" and must be
// advertised via capabilities() when overridden, so the GUI can enable/disable
// actions.
//
// Threading: name(), capabilities() and hostPath() must be thread-safe and
// constant. All other calls may run on a worker thread, but callers serialise
// them (one call at a time per filesystem object), so backends need no
// internal locking.
//
// File contents move as streams (see Stream.hpp) and whole trees are copied
// between any two filesystems by copyTree() (see Transfer.hpp):
//   Extract  = openRead()  | Inject = openWrite() creating | Replace = openWrite() overwriting
class FileSystem {
public:
    virtual ~FileSystem() = default;

    virtual std::string name() const = 0;
    virtual Capability capabilities() const { return Capability::Browse; }
    // The host file or folder at `path`, for a filesystem that is a host
    // folder; computed from the path alone, without touching the disk.
    virtual std::optional<std::filesystem::path> hostPath(const std::string&) const { return std::nullopt; }

    virtual Status list(const std::string& path, std::vector<Entry>& out) const = 0;
    // Information about one entry. The default looks it up in list(parent).
    virtual Status stat(const std::string& path, Entry& out) const;

    // Expanded view of a file/folder (needs Capability::Inspect).
    virtual Status describe(const std::string& path, Details& out) const;
    // Expanded view of the filesystem object itself (needs Capability::Inspect).
    virtual Status describeFileSystem(Details& out) const;

    // Stream a file's contents out (Capability::Extract).
    virtual Result<std::unique_ptr<ByteSource>> openRead(const std::string& path) const;
    // Stream a file's contents in (Capability::Inject / Replace). `size` is the
    // final length when known (some formats need it up front). Nothing may be
    // visible at `path` until the sink's finish() succeeds.
    virtual Result<std::unique_ptr<ByteSink>> openWrite(const std::string& path,
                                                        std::optional<std::uint64_t> size,
                                                        bool overwrite);
    virtual Status makeDirectory(const std::string& path);
    // Renames within the same folder; `newName` is a bare name.
    virtual Status rename(const std::string& path, const std::string& newName);
    virtual Status remove(const std::string& path);
    // Deletes everything inside the folder at `path` (Capability::Clear).
    virtual Status clear(const std::string& path);
    virtual Status healthCheck(std::string& report);
    virtual Status repair(std::string& report);
    // Erases the whole filesystem and creates an empty one (Capability::Format).
    virtual Status format(const std::string& label);
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
