#include "core/LocalFileSystem.hpp"
#include "core/Format.hpp"
#include "core/PathUtil.hpp"

#include <chrono>
#include <system_error>

namespace fs = std::filesystem;

namespace unnamed::core {

namespace {
Status fromError(const std::error_code& ec) {
    return ec ? Status::failure(ec.message()) : Status::success();
}

// file_clock -> Unix seconds without clock_cast (not available in every stdlib).
std::int64_t toUnixSeconds(fs::file_time_type t) {
    using namespace std::chrono;
    const auto sys = time_point_cast<system_clock::duration>(
        t - fs::file_time_type::clock::now() + system_clock::now());
    return duration_cast<seconds>(sys.time_since_epoch()).count();
}

std::string permissionString(fs::perms p) {
    auto bit = [&](fs::perms flag, char c) { return (p & flag) != fs::perms::none ? c : '-'; };
    std::string out;
    out += bit(fs::perms::owner_read, 'r');
    out += bit(fs::perms::owner_write, 'w');
    out += bit(fs::perms::owner_exec, 'x');
    out += bit(fs::perms::group_read, 'r');
    out += bit(fs::perms::group_write, 'w');
    out += bit(fs::perms::group_exec, 'x');
    out += bit(fs::perms::others_read, 'r');
    out += bit(fs::perms::others_write, 'w');
    out += bit(fs::perms::others_exec, 'x');
    return out;
}
} // namespace

LocalFileSystem::LocalFileSystem(fs::path root) : m_root(root.lexically_normal()) {}

Capability LocalFileSystem::capabilities() const {
    return Capability::Browse | Capability::Inspect | Capability::Extract |
           Capability::Inject | Capability::Replace | Capability::Remove |
           Capability::MakeDirectory | Capability::Rename;
}

std::optional<fs::path> LocalFileSystem::hostPath(const std::string& path) const {
    fs::path out;
    if (!resolve(path, out))
        return std::nullopt;
    return out;
}

bool LocalFileSystem::resolve(const std::string& path, fs::path& out) const {
    const fs::path relative = pathFromUtf8(path).relative_path().lexically_normal();
    for (const auto& part : relative) {
        if (part == "..")
            return false;
    }
    out = relative.empty() ? m_root : (m_root / relative).lexically_normal();
    return true;
}

Status LocalFileSystem::list(const std::string& path, std::vector<Entry>& out) const {
    fs::path dir;
    if (!resolve(path, dir))
        return Status::failure("Invalid path: " + path);

    std::error_code ec;
    out.clear();
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        Entry entry;
        entry.name = pathToUtf8(it->path().filename());
        std::error_code statEc;
        if (it->is_directory(statEc)) {
            entry.type = EntryType::Directory;
            entry.kind = "folder";
        } else {
            entry.size = it->is_regular_file(statEc) ? it->file_size(statEc) : 0;
            if (statEc)
                entry.size = 0;
            entry.kind = kindFromName(entry.name);
        }
        const auto mtime = it->last_write_time(statEc);
        if (!statEc)
            entry.modified = toUnixSeconds(mtime);
        out.push_back(std::move(entry));
    }
    return fromError(ec);
}

Status LocalFileSystem::describe(const std::string& path, Details& out) const {
    fs::path target;
    if (!resolve(path, target))
        return Status::failure("Invalid path: " + path);

    std::error_code ec;
    const auto status = fs::status(target, ec);
    if (ec)
        return fromError(ec);

    const bool isDir = fs::is_directory(status);
    const std::string name = pathToUtf8((path == "/" || path.empty()) ? m_root.filename()
                                                                       : target.filename());
    out = {};
    out.title = name.empty() ? pathToUtf8(m_root) : name;
    out.kind = isDir ? "folder" : kindFromName(name);
    out.subtitle = kindLabel(out.kind);

    PropertyGroup general{"General", {}};
    general.items.push_back({"Name", out.title});
    general.items.push_back({"Location", path.empty() ? "/" : path});
    if (isDir) {
        std::uint64_t count = 0;
        for (fs::directory_iterator it(target, ec), end; !ec && it != end; it.increment(ec))
            ++count;
        general.items.push_back({"Items", std::to_string(count)});
    } else {
        general.items.push_back({"Size", humanSize(fs::file_size(target, ec))});
    }
    const auto mtime = fs::last_write_time(target, ec);
    if (!ec)
        general.items.push_back({"Modified", formatTimestamp(toUnixSeconds(mtime))});
    out.groups.push_back(std::move(general));

    out.groups.push_back({"Permissions", {{"Mode", permissionString(status.permissions())}}});
    return Status::success();
}

Status LocalFileSystem::describeFileSystem(Details& out) const {
    out = {};
    out.title = pathToUtf8(m_root.filename().empty() ? m_root : m_root.filename());
    out.subtitle = "Local folder";
    out.kind = "filesystem";

    out.groups.push_back({"General", {{"Type", "Local folder"}, {"Host path", pathToUtf8(m_root)}}});

    std::error_code ec;
    const auto space = fs::space(m_root, ec);
    if (!ec) {
        out.groups.push_back({"Capacity",
                              {{"Total", humanSize(space.capacity)},
                               {"Free", humanSize(space.available)},
                               {"Used", humanSize(space.capacity - space.free)}}});
    }

    PropertyGroup caps{"Supported operations", {}};
    for (const auto& name : capabilityNames(capabilities()))
        caps.items.push_back({name, "Yes"});
    out.groups.push_back(std::move(caps));
    return Status::success();
}

Status LocalFileSystem::stat(const std::string& path, Entry& out) const {
    fs::path target;
    if (!resolve(path, target))
        return Status::failure("Invalid path: " + path);
    std::error_code ec;
    const auto status = fs::status(target, ec);
    if (ec || !fs::exists(status))
        return Status::failure("No such file or folder: " + path);

    out = {};
    out.name = pathToUtf8(target.filename());
    if (fs::is_directory(status)) {
        out.type = EntryType::Directory;
        out.kind = "folder";
    } else {
        out.size = fs::is_regular_file(status) ? fs::file_size(target, ec) : 0;
        out.kind = kindFromName(out.name);
    }
    const auto mtime = fs::last_write_time(target, ec);
    if (!ec)
        out.modified = toUnixSeconds(mtime);
    return Status::success();
}

Result<std::unique_ptr<ByteSource>> LocalFileSystem::openRead(const std::string& path) const {
    fs::path target;
    if (!resolve(path, target))
        return Status::failure("Invalid path: " + path);
    return openFileSource(target);
}

Result<std::unique_ptr<ByteSink>> LocalFileSystem::openWrite(const std::string& path,
                                                             std::optional<std::uint64_t>,
                                                             bool overwrite) {
    fs::path target;
    if (!resolve(path, target) || target == m_root)
        return Status::failure("Invalid path: " + path);
    return openFileSink(target, overwrite);
}

Status LocalFileSystem::makeDirectory(const std::string& path) {
    fs::path target;
    if (!resolve(path, target) || target == m_root)
        return Status::failure("Invalid path: " + path);
    std::error_code ec;
    if (fs::exists(target, ec))
        return Status::failure("Already exists: " + path);
    fs::create_directory(target, ec);
    return fromError(ec);
}

Status LocalFileSystem::rename(const std::string& path, const std::string& newName) {
    fs::path source;
    if (!resolve(path, source) || source == m_root)
        return Status::failure("Invalid path: " + path);
    if (newName.empty() || newName == "." || newName == ".." ||
        newName.find('/') != std::string::npos || newName.find('\\') != std::string::npos)
        return Status::failure("Invalid name: " + newName);

    const fs::path target = source.parent_path() / pathFromUtf8(newName);
    std::error_code ec;
    if (fs::exists(target, ec))
        return Status::failure("Already exists: " + newName);
    fs::rename(source, target, ec);
    return fromError(ec);
}

Status LocalFileSystem::remove(const std::string& path) {
    fs::path target;
    if (!resolve(path, target) || target == m_root)
        return Status::failure("Invalid path: " + path);
    std::error_code ec;
    fs::remove_all(target, ec);
    return fromError(ec);
}

} // namespace unnamed::core
