#pragma once

#include "core/FileSystem.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace unnamed::core {

// Opens a host path (directory or disk/package image) as a FileSystem.
// Returns nullptr and fills `error` on failure.
using FileSystemFactory =
    std::function<std::unique_ptr<FileSystem>(const std::filesystem::path&, std::string& error)>;

// Maps a filesystem kind ("Local", later "FATX", "STFS", "NAND", ...) to its
// factory. New backends register themselves here; the GUI only talks to
// FileSystem.
class FileSystemRegistry {
public:
    // Registry pre-populated with all built-in backends.
    static FileSystemRegistry withBuiltins();

    void add(std::string kind, FileSystemFactory factory);
    bool contains(const std::string& kind) const;

    std::unique_ptr<FileSystem> open(const std::string& kind,
                                     const std::filesystem::path& path,
                                     std::string& error) const;

private:
    std::map<std::string, FileSystemFactory> m_factories;
};

} // namespace unnamed::core
