#include "core/FileSystem.hpp"

#include <cctype>

namespace unnamed::core {

namespace {
Status unsupported(const FileSystem& fs, const char* op) {
    return Status::failure(std::string(op) + " is not supported by " + fs.name());
}
} // namespace

Status FileSystem::stat(const std::string& path, Entry& out) const {
    if (path.empty() || path == "/") {
        out = {};
        out.type = EntryType::Directory;
        out.kind = "folder";
        return Status::success();
    }
    std::vector<Entry> siblings;
    if (const Status st = list(parentPath(path), siblings); !st)
        return st;
    const std::string name = path.substr(path.find_last_of('/') + 1);
    for (const Entry& e : siblings) {
        if (e.name == name) {
            out = e;
            return Status::success();
        }
    }
    return Status::failure("No such file or folder: " + path);
}

Result<std::unique_ptr<ByteSource>> FileSystem::openRead(const std::string&) const {
    return unsupported(*this, "Extract");
}
Result<std::unique_ptr<ByteSink>> FileSystem::openWrite(const std::string&,
                                                        std::optional<std::uint64_t>, bool) {
    return unsupported(*this, "Writing files");
}
Status FileSystem::makeDirectory(const std::string&) { return unsupported(*this, "Make folder"); }
Status FileSystem::rename(const std::string&, const std::string&) {
    return unsupported(*this, "Rename");
}
Status FileSystem::remove(const std::string&) { return unsupported(*this, "Delete"); }
Status FileSystem::clear(const std::string&) { return unsupported(*this, "Clear"); }
Status FileSystem::healthCheck(std::string&) { return unsupported(*this, "Health check"); }
Status FileSystem::repair(std::string&) { return unsupported(*this, "Repair"); }

Status FileSystem::describe(const std::string&, Details&) const {
    return unsupported(*this, "Inspect");
}
Status FileSystem::describeFileSystem(Details&) const { return unsupported(*this, "Inspect"); }

std::vector<std::string> capabilityNames(Capability caps) {
    static const std::pair<Capability, const char*> names[] = {
        {Capability::Browse, "Browse"},        {Capability::Inspect, "Inspect"},
        {Capability::Extract, "Extract"},      {Capability::Inject, "Inject"},
        {Capability::Replace, "Replace"},      {Capability::Remove, "Delete"},
        {Capability::MakeDirectory, "Make folder"}, {Capability::Rename, "Rename"},
        {Capability::Clear, "Clear"},          {Capability::HealthCheck, "Health check"},
        {Capability::Repair, "Repair"},
    };
    std::vector<std::string> out;
    for (const auto& [flag, name] : names) {
        if (hasCapability(caps, flag))
            out.emplace_back(name);
    }
    return out;
}

std::string kindFromName(const std::string& name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos)
        return "file";
    std::string ext = name.substr(dot + 1);
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == "xex" || ext == "ini")
        return ext;
    return "file";
}

std::string kindLabel(const std::string& kind) {
    static const std::pair<const char*, const char*> labels[] = {
        {"folder", "Folder"},
        {"file", "File"},
        {"xex", "Xbox 360 executable (XEX)"},
        {"stfs", "STFS package"},
        {"ini", "Configuration (INI)"},
        {"smc", "SMC image"},
        {"updatebin", "System update (xboxupd.bin)"},
    };
    for (const auto& [id, label] : labels) {
        if (kind == id)
            return label;
    }
    return kind;
}

std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == "/")
        return "/" + name;
    return dir + "/" + name;
}

std::string parentPath(const std::string& path) {
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0)
        return "/";
    return path.substr(0, slash);
}

} // namespace unnamed::core
