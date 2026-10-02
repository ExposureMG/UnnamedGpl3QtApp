// Minimal dependency-free tests for the GUI-free core library.
#include "core/DemoFileSystem.hpp"
#include "core/FileSystemRegistry.hpp"
#include "core/LocalFileSystem.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace unnamed::core;

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

int main() {
    const fs::path root = fs::temp_directory_path() / "unnamed_core_tests";
    fs::remove_all(root);
    fs::create_directories(root / "dir");
    std::ofstream(root / "a.txt") << "hello";
    std::ofstream(root / "src.bin") << "data";

    LocalFileSystem local(root);

    std::vector<Entry> entries;
    CHECK(local.list("/", entries));
    CHECK(entries.size() == 3);

    CHECK(!local.list("/../etc", entries));
    CHECK(!local.remove("/"));

    CHECK(local.inject("/dir", root / "src.bin"));
    CHECK(fs::exists(root / "dir" / "src.bin"));
    CHECK(local.replace("/a.txt", root / "src.bin"));
    CHECK(local.remove("/dir/src.bin"));
    CHECK(!fs::exists(root / "dir" / "src.bin"));

    CHECK(parentPath("/dir/x") == "/dir");
    CHECK(parentPath("/dir") == "/");
    CHECK(joinPath("/", "x") == "/x");
    CHECK(joinPath("/dir", "x") == "/dir/x");

    // Kinds, timestamps and the expanded (details) view of the local backend.
    CHECK(kindFromName("default.xex") == "xex");
    CHECK(kindFromName("LAUNCH.INI") == "ini");
    CHECK(kindFromName("noextension") == "file");
    CHECK(local.list("/", entries));
    for (const Entry& e : entries) {
        if (e.name == "dir")
            CHECK(e.kind == "folder");
        if (e.name == "a.txt") {
            CHECK(e.kind == "file");
            CHECK(e.modified > 0);
        }
    }
    Details details;
    CHECK(local.describe("/a.txt", details));
    CHECK(details.title == "a.txt");
    CHECK(!details.groups.empty() && details.groups[0].title == "General");
    CHECK(!local.describe("/../etc", details));
    CHECK(local.describeFileSystem(details));
    CHECK(details.kind == "filesystem");
    CHECK(hasCapability(local.capabilities(), Capability::Inspect));

    // Demo (sample data) filesystem: browsable, inspectable, read-only.
    DemoFileSystem demo;
    CHECK(!hasCapability(demo.capabilities(), Capability::Remove));
    CHECK(demo.list("/Games/Sample Title", entries));
    CHECK(entries.size() == 2);
    CHECK(demo.describe("/Games/Sample Title/default.xex", details));
    CHECK(details.kind == "xex");
    CHECK(!details.notice.empty());
    bool hasExecutable = false;
    for (const auto& g : details.groups)
        hasExecutable = hasExecutable || g.title == "Executable";
    CHECK(hasExecutable);
    CHECK(demo.describeFileSystem(details));
    CHECK(!demo.list("/nope", entries));
    CHECK(!demo.remove("/readme.txt")); // unsupported by default

    auto registry = FileSystemRegistry::withBuiltins();
    std::string error;
    CHECK(registry.open("Local", root, error) != nullptr);
    CHECK(registry.open("Nope", root, error) == nullptr);
    CHECK(registry.open("Demo", {}, error) != nullptr);
    CHECK(registry.open("Local", root / "a.txt", error) == nullptr);

    fs::remove_all(root);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
