#include "core/DemoFileSystem.hpp"
#include "core/Format.hpp"

namespace unnamed::core {

namespace {
constexpr std::int64_t kSampleTime = 1735689600; // 2025-01-01 00:00 UTC

Entry folder(std::string name) {
    return {std::move(name), EntryType::Directory, "folder", 0, kSampleTime};
}

Entry file(std::string name, std::string kind, std::uint64_t size) {
    return {std::move(name), EntryType::File, std::move(kind), size, kSampleTime};
}

const char* const kNotice = "Sample data: this demo filesystem is not read from a real file.";
} // namespace

DemoFileSystem::DemoFileSystem() {
    m_nodes["/"] = {folder(""), {}};

    add("/", folder("Content"));
    add("/Content", folder("0000000000000000"));
    add("/Content/0000000000000000", folder("AAAA0001"));
    add("/Content/0000000000000000/AAAA0001", folder("00000001"));
    add("/Content/0000000000000000/AAAA0001/00000001",
        file("SAMPLEPKG0000001", "stfs", 25165824),
        {{"Package", {{"Package type", "LIVE"},
                      {"Content type", "Arcade title"},
                      {"Title ID", "AAAA0001"},
                      {"Display name", "Sample Content"}}},
         {"Integrity", {{"Header hash", "Not checked (sample)"}, {"Signature", "Not checked (sample)"}}}});

    add("/", folder("Games"));
    add("/Games", folder("Sample Title"));
    add("/Games/Sample Title", file("default.xex", "xex", 5452800),
        {{"Executable", {{"Title ID", "AAAA0001"},
                         {"Media ID", "00000000"},
                         {"Version", "1.0.0.0"},
                         {"Base version", "1.0.0.0"},
                         {"Region", "All regions"}}},
         {"Image", {{"Format", "XEX2"},
                    {"Encryption", "Encrypted"},
                    {"Compression", "Normal (LZX)"}}},
         {"Integrity", {{"Header hash", "Not checked (sample)"}, {"Signature", "Not checked (sample)"}}}});
    add("/Games/Sample Title", file("config.ini", "ini", 1480),
        {{"Format", {{"Type", "Configuration file"}, {"Sections", "3"}}}});

    add("/", folder("System"));
    add("/System", file("xboxupd.bin", "updatebin", 31457280),
        {{"Update", {{"Contents", "Sample update bundle"}, {"Files", "12"}}}});
    add("/System", file("smc.bin", "smc", 16384),
        {{"SMC", {{"Image type", "Sample SMC image"}}}});

    add("/", file("launch.ini", "ini", 2210),
        {{"Format", {{"Type", "Launch configuration"}, {"Sections", "5"}}}});
    add("/", file("readme.txt", "file", 312));
}

void DemoFileSystem::add(const std::string& dir, Entry entry, std::vector<PropertyGroup> groups) {
    const std::string full = joinPath(dir, entry.name);
    m_nodes[full] = {std::move(entry), std::move(groups)};
}

Status DemoFileSystem::list(const std::string& path, std::vector<Entry>& out) const {
    const std::string dir = path.empty() ? "/" : path;
    const auto self = m_nodes.find(dir);
    if (self == m_nodes.end() || self->second.entry.type != EntryType::Directory)
        return Status::failure("No such folder: " + path);

    out.clear();
    for (const auto& [full, node] : m_nodes) {
        if (full != "/" && parentPath(full) == dir)
            out.push_back(node.entry);
    }
    return Status::success();
}

Status DemoFileSystem::describe(const std::string& path, Details& out) const {
    const auto it = m_nodes.find(path.empty() ? "/" : path);
    if (it == m_nodes.end())
        return Status::failure("No such item: " + path);

    const Node& node = it->second;
    const bool isDir = node.entry.type == EntryType::Directory;
    out = {};
    out.title = path == "/" ? "Demo (sample data)" : node.entry.name;
    out.kind = node.entry.kind;
    out.subtitle = kindLabel(node.entry.kind);
    out.notice = kNotice;

    PropertyGroup general{"General", {{"Name", out.title}, {"Location", path}}};
    if (isDir) {
        std::vector<Entry> children;
        list(path, children);
        general.items.push_back({"Items", std::to_string(children.size())});
    } else {
        general.items.push_back({"Size", humanSize(node.entry.size)});
    }
    general.items.push_back({"Modified", formatTimestamp(node.entry.modified)});
    out.groups.push_back(std::move(general));
    for (const auto& group : node.groups)
        out.groups.push_back(group);
    return Status::success();
}

Status DemoFileSystem::describeFileSystem(Details& out) const {
    out = {};
    out.title = "Demo (sample data)";
    out.subtitle = "Sample Xbox 360 filesystem";
    out.kind = "filesystem";
    out.notice = kNotice;

    out.groups.push_back({"General", {{"Type", "Demo"}, {"Layout", "Sample hard drive"}}});
    out.groups.push_back({"Capacity",
                          {{"Total", humanSize(20ull << 30)}, {"Free", humanSize(14ull << 30)}}});
    out.groups.push_back({"Health", {{"Status", "Not checked (sample)"}}});

    PropertyGroup caps{"Supported operations", {}};
    for (const auto& name : capabilityNames(capabilities()))
        caps.items.push_back({name, "Yes"});
    out.groups.push_back(std::move(caps));
    return Status::success();
}

} // namespace unnamed::core
