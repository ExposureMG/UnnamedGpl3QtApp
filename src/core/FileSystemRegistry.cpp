#include "core/FileSystemRegistry.hpp"
#include "core/DemoFileSystem.hpp"
#include "core/LocalFileSystem.hpp"
#include "core/PathUtil.hpp"

#include <system_error>

namespace unnamed::core {

FileSystemRegistry FileSystemRegistry::withBuiltins() {
    FileSystemRegistry registry;
    registry.add("Local", [](const std::filesystem::path& path, std::string& error)
                              -> std::unique_ptr<FileSystem> {
        std::error_code ec;
        if (!std::filesystem::is_directory(path, ec)) {
            error = "Not a directory: " + pathToUtf8(path);
            return nullptr;
        }
        return std::make_unique<LocalFileSystem>(path);
    });
    registry.add("Demo", [](const std::filesystem::path&, std::string&) -> std::unique_ptr<FileSystem> {
        return std::make_unique<DemoFileSystem>();
    });
    // Future backends (FATX, STFS, NAND, ...) register here.
    return registry;
}

void FileSystemRegistry::add(std::string kind, FileSystemFactory factory) {
    m_factories[std::move(kind)] = std::move(factory);
}

bool FileSystemRegistry::contains(const std::string& kind) const {
    return m_factories.count(kind) != 0;
}

std::unique_ptr<FileSystem> FileSystemRegistry::open(const std::string& kind,
                                                     const std::filesystem::path& path,
                                                     std::string& error) const {
    const auto it = m_factories.find(kind);
    if (it == m_factories.end()) {
        error = "Unknown filesystem kind: " + kind;
        return nullptr;
    }
    return it->second(path, error);
}

} // namespace unnamed::core
