#pragma once

#include "core/FileSystem.hpp"

namespace unnamed::core {

// A directory on the host exposed as a FileSystem. Virtual paths cannot escape
// `root`. On Windows, paths with a name Windows would change (a trailing dot
// or space) or open as a device (CON, NUL.txt, COM1, ...) are refused.
class LocalFileSystem final : public FileSystem {
public:
    explicit LocalFileSystem(std::filesystem::path root);

    std::string name() const override { return "Local"; }
    Capability capabilities() const override;
    std::optional<std::filesystem::path> hostPath(const std::string& path) const override;

    Status list(const std::string& path, std::vector<Entry>& out) const override;
    Status describe(const std::string& path, Details& out) const override;
    Status describeFileSystem(Details& out) const override;
    Status stat(const std::string& path, Entry& out) const override;
    Result<std::unique_ptr<ByteSource>> openRead(const std::string& path) const override;
    Result<std::unique_ptr<ByteSink>> openWrite(const std::string& path,
                                                std::optional<std::uint64_t> size,
                                                bool overwrite) override;
    Status makeDirectory(const std::string& path) override;
    Status rename(const std::string& path, const std::string& newName) override;
    Status remove(const std::string& path) override;

private:
    // Maps a virtual path to a host path; fails if it would escape the root.
    Status resolve(const std::string& path, std::filesystem::path& out) const;

    std::filesystem::path m_root;
};

} // namespace unnamed::core
