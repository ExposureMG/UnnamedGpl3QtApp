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

    // Adds the bytes below `path` to `total`, for a determinate progress bar.
    // A folder that cannot be listed counts as empty; only a cancel fails.
    // Each level's listing is let go before its folders are walked.
    Status measure(const std::string& path, const Entry& entry, int depth, std::uint64_t& total) {
        if (entry.type == EntryType::File) {
            total += entry.size;
            return Status::success();
        }
        if (depth > kMaxDepth)
            return Status::success();
        if (!report())
            return Status::cancelledByUser();
        std::vector<Entry> folders;
        {
            std::vector<Entry> children;
            if (const Status st = source.list(path, children); !st)
                return st.cancelled ? st : Status::success();
            for (Entry& child : children) {
                if (child.type == EntryType::File)
                    total += child.size;
                else
                    folders.push_back(std::move(child));
            }
        }
        for (const Entry& folder : folders) {
            if (const Status st = measure(joinPath(path, folder.name), folder, depth + 1, total); !st)
                return st;
        }
        return Status::success();
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

    // A folder's files first, then its folders, from a listing that holds the
    // folders only.
    Status copy(const std::string& from, const std::string& to, const Entry& entry, int depth) {
        if (entry.type == EntryType::File)
            return copyFile(from, to);
        if (depth > kMaxDepth)
            return Status::failure("Folder nesting too deep: " + from);
        if (!report())
            return Status::cancelledByUser();

        Entry existing;
        if (destination.stat(to, existing)) {
            if (existing.type != EntryType::Directory)
                return Status::failure("A file is in the way: " + to);
        } else if (const Status st = destination.makeDirectory(to); !st) {
            return st;
        }

        std::vector<Entry> folders;
        {
            std::vector<Entry> children;
            if (const Status st = source.list(from, children); !st)
                return st;
            for (Entry& child : children) {
                if (child.type != EntryType::File) {
                    folders.push_back(std::move(child));
                } else if (const Status st = copyFile(joinPath(from, child.name), joinPath(to, child.name)); !st) {
                    return st;
                }
            }
        }
        for (const Entry& folder : folders) {
            if (const Status st = copy(joinPath(from, folder.name), joinPath(to, folder.name), folder, depth + 1); !st)
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
    if (const Status st = copier.measure(sourcePath, entry, 0, copier.progress.bytesTotal); !st)
        return st;
    if (!copier.report())
        return Status::cancelledByUser();
    return copier.copy(sourcePath, destinationPath, entry, 0);
}

} // namespace unnamed::core
