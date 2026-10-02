#include "core/FileSystem.hpp"

namespace unnamed::core {

namespace {
Status unsupported(const FileSystem& fs, const char* op) {
    return Status::failure(std::string(op) + " is not supported by " + fs.name());
}
} // namespace

Status FileSystem::extract(const std::string&, const std::filesystem::path&) {
    return unsupported(*this, "Extract");
}
Status FileSystem::inject(const std::string&, const std::filesystem::path&) {
    return unsupported(*this, "Inject");
}
Status FileSystem::replace(const std::string&, const std::filesystem::path&) {
    return unsupported(*this, "Replace");
}
Status FileSystem::remove(const std::string&) { return unsupported(*this, "Delete"); }
Status FileSystem::clear(const std::string&) { return unsupported(*this, "Clear"); }
Status FileSystem::healthCheck(std::string&) { return unsupported(*this, "Health check"); }
Status FileSystem::repair(std::string&) { return unsupported(*this, "Repair"); }

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
