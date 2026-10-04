#include "core/Transfer.hpp"

#include <vector>

namespace unnamed::core {

namespace {

constexpr std::size_t kChunk = 1 << 20; // 1 MiB
constexpr int kMaxDepth = 64;           // guards against symlink loops

struct Copier {
    const FileSystem& source;
    FileSystem& destination;
    const TransferOptions& options;
    TransferProgress progress;
    std::vector<std::uint8_t> buffer = std::vector<std::uint8_t>(kChunk);

    bool report() { return !options.progress || options.progress(progress); }

    // Total bytes below `path`, for a determinate progress bar.
    std::uint64_t measure(const std::string& path, const Entry& entry, int depth) {
        if (entry.type == EntryType::File)
            return entry.size;
        if (depth > kMaxDepth)
            return 0;
        std::vector<Entry> children;
        if (!source.list(path, children))
            return 0;
        std::uint64_t total = 0;
        for (const Entry& child : children)
            total += measure(joinPath(path, child.name), child, depth + 1);
        return total;
    }

    Status copyFile(const std::string& from, const std::string& to) {
        auto in = source.openRead(from);
        if (!in)
            return in.status();
        auto out = destination.openWrite(to, in.value()->size(), options.overwrite);
        if (!out)
            return out.status();

        progress.current = from;
        for (;;) {
            if (!report())
                return Status::cancelledByUser(); // sink destructor discards the partial file
            auto n = in.value()->read(buffer.data(), buffer.size());
            if (!n)
                return n.status();
            if (n.value() == 0)
                break;
            if (const Status st = out.value()->write(buffer.data(), n.value()); !st)
                return st;
            progress.bytesDone += n.value();
        }
        if (const Status st = out.value()->finish(); !st)
            return st;
        return report() ? Status::success() : Status::cancelledByUser();
    }

    Status copy(const std::string& from, const std::string& to, const Entry& entry, int depth) {
        if (entry.type == EntryType::File)
            return copyFile(from, to);
        if (depth > kMaxDepth)
            return Status::failure("Folder nesting too deep: " + from);

        Entry existing;
        if (destination.stat(to, existing)) {
            if (existing.type != EntryType::Directory)
                return Status::failure("A file is in the way: " + to);
        } else if (const Status st = destination.makeDirectory(to); !st) {
            return st;
        }

        std::vector<Entry> children;
        if (const Status st = source.list(from, children); !st)
            return st;
        for (const Entry& child : children) {
            if (const Status st = copy(joinPath(from, child.name), joinPath(to, child.name), child, depth + 1); !st)
                return st;
        }
        return Status::success();
    }
};

} // namespace

Status copyTree(const FileSystem& source, const std::string& sourcePath, FileSystem& destination,
                const std::string& destinationPath, const TransferOptions& options) {
    Entry entry;
    if (const Status st = source.stat(sourcePath, entry); !st)
        return st;

    Copier copier{source, destination, options, {}};
    copier.progress.bytesTotal = copier.measure(sourcePath, entry, 0);
    if (!copier.report())
        return Status::cancelledByUser();
    return copier.copy(sourcePath, destinationPath, entry, 0);
}

} // namespace unnamed::core
