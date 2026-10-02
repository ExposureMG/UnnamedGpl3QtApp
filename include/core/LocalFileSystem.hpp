#pragma once

#include "core/FileSystem.hpp"

namespace unnamed::core {

// A directory on the host exposed as a FileSystem. Virtual paths cannot escape
// `root`.
class LocalFileSystem final : public FileSystem {
public:
    explicit LocalFileSystem(std::filesystem::path root);

    std::string name() const override { return "Local"; }
    Capability capabilities() const override;

    Status list(const std::string& path, std::vector<Entry>& out) const override;
    Status describe(const std::string& path, Details& out) const override;
    Status describeFileSystem(Details& out) const override;
    Status extract(const std::string& path, const std::filesystem::path& hostDest) override;
    Status inject(const std::string& dir, const std::filesystem::path& hostSource) override;
    Status replace(const std::string& path, const std::filesystem::path& hostSource) override;
    Status remove(const std::string& path) override;

private:
    // Maps a virtual path to a host path; false if it would escape the root.
    bool resolve(const std::string& path, std::filesystem::path& out) const;

    std::filesystem::path m_root;
};

} // namespace unnamed::core
