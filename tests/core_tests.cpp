// Minimal dependency-free tests for the GUI-free core library.
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

    auto registry = FileSystemRegistry::withBuiltins();
    std::string error;
    CHECK(registry.open("Local", root, error) != nullptr);
    CHECK(registry.open("Nope", root, error) == nullptr);
    CHECK(registry.open("Local", root / "a.txt", error) == nullptr);

    fs::remove_all(root);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
