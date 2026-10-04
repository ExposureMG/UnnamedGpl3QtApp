#include "core/FatxFileSystem.hpp"
#include "core/Format.hpp"

#include "volume.hpp" // fatx_core (extern/FATX/src)

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <span>
#include <system_error>

namespace unnamed::core {

namespace {

// fatx_core reads and writes the device through this adapter. It can make a
// writable device read-only for one filesystem.
class DeviceIo final : public fatx::io_backend {
public:
    DeviceIo(std::shared_ptr<BlockDevice> device, bool writable)
        : m_device(std::move(device)), m_writable(writable && m_device->writable()) {}

    std::uint64_t size() const override { return m_device->size(); }
    bool writable() const override { return m_writable; }
    bool read_at(std::uint64_t offset, std::span<std::byte> out) override {
        return m_device->readAt(offset, out.data(), out.size());
    }
    bool write_at(std::uint64_t offset, std::span<const std::byte> in) override {
        return m_writable && m_device->writeAt(offset, in.data(), in.size());
    }
    bool flush() override { return !m_writable || m_device->flush(); }

private:
    std::shared_ptr<BlockDevice> m_device;
    bool m_writable;
};

// The library's error messages, to complete our own.
struct LibraryLog {
    std::string lastError;
};

fatx::log_sink sinkFor(const std::shared_ptr<LibraryLog>& log) {
    return [log](fatx::log_level level, const std::string& line) {
        if (level == fatx::log_level::error && !line.empty())
            log->lastError = line;
    };
}

std::string errorText(int err) { return std::generic_category().message(err); }

std::string capitalized(std::string s) {
    if (!s.empty())
        s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

std::string hex(std::uint64_t v, int width = 0) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%0*llX", width, static_cast<unsigned long long>(v));
    return buf;
}

fatx::location locationOf(const FatxPartition& p) {
    fatx::location l;
    l.table = p.table;
    l.partition = p.partition;
    return l;
}

std::string attributes(const fatx::entry_info& e) {
    std::string out;
    auto add = [&](bool on, const char* name) {
        if (!on)
            return;
        if (!out.empty())
            out += ", ";
        out += name;
    };
    add(e.read_only, "Read-only");
    add(e.hidden, "Hidden");
    add(e.system, "System");
    add(e.archive, "Archive");
    add(e.label, "Volume label");
    return out.empty() ? "None" : out;
}

Entry toEntry(const fatx::entry_info& e) {
    Entry out;
    out.name = e.name;
    out.type = e.directory ? EntryType::Directory : EntryType::File;
    out.kind = e.directory ? "folder" : kindFromName(e.name);
    out.size = e.size;
    out.modified = static_cast<std::int64_t>(e.modified);
    return out;
}

class FatxFileSystem;

class FatxSource final : public ByteSource {
public:
    FatxSource(fatx::volume& volume, std::string path, std::uint64_t size)
        : m_volume(volume), m_path(std::move(path)), m_size(size) {}

    Result<std::size_t> read(void* buffer, std::size_t size) override {
        const std::uint64_t left = m_size - m_pos;
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(size, left));
        if (n == 0)
            return std::size_t{0};
        std::size_t got = 0;
        if (int err = m_volume.read(m_path, m_pos, std::span(static_cast<std::byte*>(buffer), n), &got))
            return Status::failure("Cannot read " + m_path + ": " + errorText(err));
        if (got == 0)
            return Status::failure("Cannot read " + m_path + ": unexpected end of file");
        m_pos += got;
        return got;
    }
    std::optional<std::uint64_t> size() const override { return m_size; }

private:
    fatx::volume& m_volume;
    std::string m_path;
    std::uint64_t m_size;
    std::uint64_t m_pos = 0;
};

class FatxFileSystem final : public FileSystem {
public:
    FatxFileSystem(std::shared_ptr<BlockDevice> device, std::shared_ptr<DeviceIo> io, FatxPartition partition,
                   bool writable, std::string name, std::unique_ptr<fatx::volume> volume,
                   std::shared_ptr<LibraryLog> log)
        : m_device(std::move(device)), m_io(std::move(io)), m_partition(std::move(partition)),
          m_writable(writable), m_name(std::move(name)), m_volume(std::move(volume)), m_log(std::move(log)) {}

    std::string name() const override { return m_name; }

    Capability capabilities() const override {
        return Capability::Browse | Capability::Inspect | Capability::Extract | Capability::HealthCheck;
    }

    Status list(const std::string& path, std::vector<Entry>& out) const override {
        m_log->lastError.clear();
        std::vector<fatx::entry_info> entries;
        if (int err = m_volume->list(path, entries))
            return fail(err, "Cannot list " + path);
        out.clear();
        out.reserve(entries.size());
        for (const auto& e : entries)
            out.push_back(toEntry(e));
        return Status::success();
    }

    Status stat(const std::string& path, Entry& out) const override {
        m_log->lastError.clear();
        fatx::entry_info e;
        if (int err = m_volume->stat(path, e))
            return fail(err, "Cannot open " + path);
        out = toEntry(e);
        return Status::success();
    }

    Status describe(const std::string& path, Details& out) const override {
        m_log->lastError.clear();
        fatx::entry_info e;
        if (int err = m_volume->stat(path, e))
            return fail(err, "Cannot open " + path);
        const bool root = path.empty() || path == "/";
        out = {};
        out.title = root ? m_name : e.name;
        out.kind = e.directory ? "folder" : kindFromName(e.name);
        out.subtitle = kindLabel(out.kind);

        PropertyGroup general{"General", {}};
        general.items.push_back({"Name", out.title});
        general.items.push_back({"Location", root ? "/" : path});
        if (e.directory) {
            std::vector<fatx::entry_info> children;
            if (m_volume->list(path, children) == 0)
                general.items.push_back({"Items", std::to_string(children.size())});
        } else {
            general.items.push_back({"Size", humanSize(e.size)});
        }
        if (!root) {
            general.items.push_back({"Modified", formatTimestamp(e.modified)});
            general.items.push_back({"Created", formatTimestamp(e.created)});
            general.items.push_back({"Accessed", formatTimestamp(e.accessed)});
        }
        out.groups.push_back(std::move(general));

        if (!root) {
            out.groups.push_back({"FATX entry",
                                  {{"Attributes", attributes(e)},
                                   {"First cluster", e.first_cluster ? hex(e.first_cluster, 8) : "None"}}});
        }
        return Status::success();
    }

    Status describeFileSystem(Details& out) const override {
        m_log->lastError.clear();
        const fatx::volume_info i = m_volume->info();
        out = {};
        out.title = m_name;
        out.subtitle = "FATX filesystem";
        out.kind = "filesystem";
        if (i.inconsistent)
            out.notice = "Errors were found while reading this filesystem. Run a health check.";

        const std::uint64_t total = i.cluster_count * i.cluster_size;
        const std::uint64_t free = i.free_clusters * i.cluster_size;
        out.groups.push_back({"General",
                              {{"Type", i.fat_entry_size == 4 ? "FATX (32-bit FAT)" : "FATX (16-bit FAT)"},
                               {"Label", i.label.empty() ? "None" : i.label},
                               {"Serial number", hex(i.serial, 8)},
                               {"Device", m_device->description()},
                               {"Layout", m_partition.tableName},
                               {"Partition", m_partition.name},
                               {"Access", m_writable ? "Read and write" : "Read-only"}}});
        out.groups.push_back({"Capacity",
                              {{"Total", humanSize(total)},
                               {"Free", humanSize(free)},
                               {"Used", humanSize(total - std::min(total, free))}}});
        out.groups.push_back({"Geometry",
                              {{"Partition offset", hex(i.partition_offset)},
                               {"Partition size", humanSize(i.partition_size)},
                               {"Cluster size", humanSize(i.cluster_size)},
                               {"Clusters", std::to_string(i.cluster_count)},
                               {"Free clusters", std::to_string(i.free_clusters)},
                               {"FAT offset", hex(i.fat_offset)},
                               {"FAT size", humanSize(i.fat_size)},
                               {"Data offset", hex(i.data_offset)},
                               {"Root cluster", std::to_string(i.root_cluster)}}});

        PropertyGroup caps{"Supported operations", {}};
        for (const auto& op : capabilityNames(capabilities()))
            caps.items.push_back({op, "Yes"});
        out.groups.push_back(std::move(caps));
        return Status::success();
    }

    Result<std::unique_ptr<ByteSource>> openRead(const std::string& path) const override {
        m_log->lastError.clear();
        fatx::entry_info e;
        if (int err = m_volume->stat(path, e))
            return fail(err, "Cannot open " + path);
        if (e.directory)
            return Status::failure("Is a folder: " + path);
        return std::unique_ptr<ByteSource>(std::make_unique<FatxSource>(*m_volume, path, e.size));
    }

    Status healthCheck(std::string& report) override {
        m_log->lastError.clear();
        if (int err = m_volume->flush())
            return fail(err, "Cannot write pending changes");
        fatx::check_report r;
        if (int err = fatx::check(std::make_shared<DeviceIo>(m_device, false), locationOf(m_partition), false, r))
            return fail(err, "Health check failed");
        report = describeCheck(r, false);
        return Status::success();
    }

private:
    static std::string describeCheck(const fatx::check_report& r, bool repair) {
        std::string out;
        if (r.problems == 0)
            out = "No problems found.";
        else if (repair)
            out = std::to_string(r.problems) + (r.problems == 1 ? " problem" : " problems") +
                  (r.repaired ? " found and repaired." : " found; nothing was changed.");
        else
            out = std::to_string(r.problems) + (r.problems == 1 ? " problem found." : " problems found.") +
                  " Repair needs write access.";
        // Each problem is one of fsck.fatx's questions, "<problem>. <fix> ? [Y/n] :<answer>";
        // its progress chatter is left out.
        for (const auto& m : r.messages) {
            const auto question = m.rfind(" ?");
            if (question == std::string::npos)
                continue;
            std::string text = m.substr(0, question);
            const auto dot = text.rfind(". ");
            if (dot != std::string::npos) {
                std::string fix = text.substr(dot + 2);
                if (!fix.empty())
                    fix[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(fix[0])));
                text = text.substr(0, dot + 1) + (repair ? " Fix: " : " Repair would ") + fix + ".";
            }
            out += "\n• " + text;
        }
        return out;
    }

    Status fail(int err, const std::string& what) const {
        std::string message = what + ": " + errorText(err);
        if (!m_log->lastError.empty())
            message += " (" + m_log->lastError + ")";
        m_log->lastError.clear();
        return Status::failure(message);
    }

    std::shared_ptr<BlockDevice> m_device;
    std::shared_ptr<DeviceIo> m_io;
    FatxPartition m_partition;
    bool m_writable;
    std::string m_name;
    std::unique_ptr<fatx::volume> m_volume;
    std::shared_ptr<LibraryLog> m_log;
};

} // namespace

std::vector<FatxPartition> probeFatx(const std::shared_ptr<BlockDevice>& device) {
    std::vector<FatxPartition> out;
    if (!device)
        return out;
    for (const fatx::partition_info& p : fatx::probe(std::make_shared<DeviceIo>(device, false))) {
        FatxPartition part;
        part.table = p.where.table;
        part.partition = p.where.partition;
        part.name = capitalized(p.name);
        part.tableName = capitalized(p.table_name);
        part.offset = p.offset;
        part.size = p.size;
        out.push_back(std::move(part));
    }
    return out;
}

Result<std::unique_ptr<FileSystem>> openFatx(std::shared_ptr<BlockDevice> device, const FatxPartition& partition,
                                             bool writable, std::string displayName) {
    if (!device)
        return Status::failure("No device");
    if (writable && !device->writable())
        return Status::failure(device->description() + " is open read-only");
    auto io = std::make_shared<DeviceIo>(device, writable);
    auto log = std::make_shared<LibraryLog>();
    int err = 0;
    auto volume = fatx::volume::open(io, locationOf(partition), writable, sinkFor(log), &err);
    if (!volume) {
        std::string message = "Cannot open the FATX filesystem on " + device->description() + ": " + errorText(err);
        if (!log->lastError.empty())
            message += " (" + log->lastError + ")";
        return Status::failure(message);
    }
    return std::unique_ptr<FileSystem>(std::make_unique<FatxFileSystem>(
        std::move(device), std::move(io), partition, writable, std::move(displayName), std::move(volume), log));
}

} // namespace unnamed::core
