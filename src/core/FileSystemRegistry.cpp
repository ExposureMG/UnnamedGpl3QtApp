#include "core/FileSystemRegistry.hpp"
#include "core/DemoFileSystem.hpp"
#include "core/LocalFileSystem.hpp"
#include "core/PathUtil.hpp"
#ifdef UNNAMED_WITH_FATX
#include "core/FatxFileSystem.hpp"
#endif
#ifdef UNNAMED_WITH_XBDM
#include "core/XbdmFileSystem.hpp"
#endif

#include <algorithm>
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
#ifdef UNNAMED_WITH_FATX
    // A FATX image, opened read-only: the data partition of an Xbox 360 disk
    // image, otherwise the first partition found. (The GUI offers every
    // partition of a disk as its own place, see FileBrowser::openFatxImage.)
    registry.add("FATX", [](const std::filesystem::path& path, std::string& error) -> std::unique_ptr<FileSystem> {
        auto device = openImageFile(path, false);
        if (!device) {
            error = device.status().message;
            return nullptr;
        }
        const auto partitions = probeFatx(device.value());
        if (partitions.empty()) {
            error = "No FATX filesystem found in " + pathToUtf8(path);
            return nullptr;
        }
        auto chosen = std::find_if(partitions.begin(), partitions.end(),
                                   [](const FatxPartition& p) { return p.partition == "x2"; });
        if (chosen == partitions.end())
            chosen = partitions.begin();
        auto fs = openFatx(device.value(), *chosen, false, pathToUtf8(path.filename()));
        if (!fs) {
            error = fs.status().message;
            return nullptr;
        }
        return std::move(fs.value());
    });
#endif
#ifdef UNNAMED_WITH_XBDM
    // A console over XBDM; the path is its address, "host" or "host:port".
    registry.add("XBDM", [](const std::filesystem::path& path, std::string& error) -> std::unique_ptr<FileSystem> {
        std::string host = pathToUtf8(path);
        std::uint16_t port = updclient::xbdm::kXbdmPort;
        if (const auto colon = host.rfind(':'); colon != std::string::npos) {
            const std::string digits = host.substr(colon + 1);
            unsigned long value = 0;
            const bool ok = !digits.empty() && digits.size() <= 5 &&
                            std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; }) &&
                            (value = std::stoul(digits)) > 0 && value <= 0xFFFF;
            if (!ok) {
                error = "Invalid console address: " + host;
                return nullptr;
            }
            port = static_cast<std::uint16_t>(value);
            host.resize(colon);
        }
        auto fs = connectXbdm(host, port);
        if (!fs) {
            error = fs.status().message;
            return nullptr;
        }
        return std::move(fs.value());
    });
#endif
    // Future backends (STFS, NAND, ...) register here.
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
