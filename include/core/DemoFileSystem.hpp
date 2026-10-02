#pragma once

#include "core/FileSystem.hpp"

#include <map>

namespace unnamed::core {

// Read-only in-memory filesystem with *sample data* (no real files, nothing is
// parsed). It lets the UI show the expanded views for XEX, STFS, ... before the
// real format backends exist.
class DemoFileSystem final : public FileSystem {
public:
    DemoFileSystem();

    std::string name() const override { return "Demo"; }
    Capability capabilities() const override { return Capability::Browse | Capability::Inspect; }

    Status list(const std::string& path, std::vector<Entry>& out) const override;
    Status describe(const std::string& path, Details& out) const override;
    Status describeFileSystem(Details& out) const override;

private:
    struct Node {
        Entry entry;
        std::vector<PropertyGroup> groups; // kind specific groups
    };

    void add(const std::string& dir, Entry entry, std::vector<PropertyGroup> groups = {});

    std::map<std::string, Node> m_nodes; // by full virtual path
};

} // namespace unnamed::core
