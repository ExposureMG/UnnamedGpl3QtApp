#include "core/LocalFileSystem.hpp"

#include <system_error>

namespace fs = std::filesystem;

namespace unnamed::core {

namespace {
Status fromError(const std::error_code& ec) {
    return ec ? Status::failure(ec.message()) : Status::success();
}
} // namespace

LocalFileSystem::LocalFileSystem(fs::path root) : m_root(root.lexically_normal()) {}

Capability LocalFileSystem::capabilities() const {
    return Capability::Browse | Capability::Extract | Capability::Inject |
           Capability::Replace | Capability::Remove;
}

bool LocalFileSystem::resolve(const std::string& path, fs::path& out) const {
    const fs::path relative = fs::path(path).relative_path().lexically_normal();
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
        entry.name = it->path().filename().string();
        std::error_code statEc;
        if (it->is_directory(statEc)) {
            entry.type = EntryType::Directory;
        } else {
            entry.size = it->is_regular_file(statEc) ? it->file_size(statEc) : 0;
            if (statEc)
                entry.size = 0;
        }
        out.push_back(std::move(entry));
    }
    return fromError(ec);
}

Status LocalFileSystem::extract(const std::string& path, const fs::path& hostDest) {
    fs::path src;
    if (!resolve(path, src))
        return Status::failure("Invalid path: " + path);
    std::error_code ec;
    fs::copy(src, hostDest, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    return fromError(ec);
}

Status LocalFileSystem::inject(const std::string& dir, const fs::path& hostSource) {
    fs::path dest;
    if (!resolve(dir, dest))
        return Status::failure("Invalid path: " + dir);
    std::error_code ec;
    fs::copy(hostSource, dest / hostSource.filename(),
             fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
    return fromError(ec);
}

Status LocalFileSystem::replace(const std::string& path, const fs::path& hostSource) {
    fs::path dest;
    if (!resolve(path, dest))
        return Status::failure("Invalid path: " + path);
    std::error_code ec;
    if (!fs::is_regular_file(dest, ec))
        return Status::failure("Not a file: " + path);
    fs::copy_file(hostSource, dest, fs::copy_options::overwrite_existing, ec);
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
