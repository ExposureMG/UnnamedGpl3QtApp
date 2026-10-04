// Minimal dependency-free tests for the GUI-free core library.
#include "core/DemoFileSystem.hpp"
#include "core/FileSystemRegistry.hpp"
#include "core/LocalFileSystem.hpp"
#include "core/Transfer.hpp"

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

    // Streams, make folder, rename, remove.
    {
        auto sink = local.openWrite("/dir/new.bin", 3, false);
        CHECK(sink);
        CHECK(!fs::exists(root / "dir" / "new.bin")); // invisible until finish()
        CHECK(sink.value()->write("abc", 3));
        CHECK(sink.value()->finish());
        CHECK(fs::file_size(root / "dir" / "new.bin") == 3);
        CHECK(!local.openWrite("/dir/new.bin", 3, false));  // exists, no overwrite
        CHECK(local.openWrite("/dir/new.bin", 3, true));   // overwrite allowed (dropped = discarded)
        CHECK(fs::file_size(root / "dir" / "new.bin") == 3);
        CHECK(!fs::exists(root / "dir" / "new.bin.part"));  // discarded sink cleans up
    }
    CHECK(local.makeDirectory("/dir/sub"));
    CHECK(!local.makeDirectory("/dir/sub"));
    CHECK(local.rename("/dir/new.bin", "renamed.bin"));
    CHECK(fs::exists(root / "dir" / "renamed.bin"));
    CHECK(!local.rename("/dir/renamed.bin", "../escape"));
    CHECK(!local.rename("/dir/renamed.bin", "sub")); // target exists
    {
        auto in = local.openRead("/dir/renamed.bin");
        CHECK(in);
        char buf[8];
        auto n = in.value()->read(buf, sizeof buf);
        CHECK(n && n.value() == 3 && std::string(buf, 3) == "abc");
        CHECK(in.value()->size() == 3u);
    }
    Entry st;
    CHECK(local.stat("/dir/renamed.bin", st) && st.size == 3 && st.kind == "file");
    CHECK(local.stat("/dir", st) && st.type == EntryType::Directory);
    CHECK(!local.stat("/nope", st));
    CHECK(local.remove("/dir/renamed.bin"));
    CHECK(!fs::exists(root / "dir" / "renamed.bin"));
    CHECK(local.remove("/dir/sub"));

    // copyTree: host -> host tree copy with progress, merge and cancel.
    {
        fs::create_directories(root / "tree" / "a" / "b");
        std::ofstream(root / "tree" / "one.txt") << "1111";
        std::ofstream(root / "tree" / "a" / "two.txt") << "22";
        std::ofstream(root / "tree" / "a" / "b" / "three.txt") << "333";
        fs::create_directories(root / "out");
        LocalFileSystem dest(root / "out");

        std::uint64_t lastDone = 0, total = 0;
        TransferOptions opts;
        opts.progress = [&](const TransferProgress& p) {
            lastDone = p.bytesDone;
            total = p.bytesTotal;
            return true;
        };
        CHECK(copyTree(local, "/tree", dest, "/tree", opts));
        CHECK(total == 9 && lastDone == 9);
        CHECK(fs::file_size(root / "out" / "tree" / "a" / "b" / "three.txt") == 3);

        // Existing files fail unless overwrite is set; folders merge.
        CHECK(!copyTree(local, "/tree", dest, "/tree"));
        TransferOptions over;
        over.overwrite = true;
        CHECK(copyTree(local, "/tree", dest, "/tree", over));

        // Cancel leaves no partial file behind.
        TransferOptions cancel;
        cancel.overwrite = true;
        cancel.progress = [](const TransferProgress& p) { return p.bytesDone == 0; };
        const Status st2 = copyTree(local, "/tree/one.txt", dest, "/cancelled.txt", cancel);
        CHECK(!st2 && st2.cancelled);
        CHECK(!fs::exists(root / "out" / "cancelled.txt"));
        CHECK(!fs::exists(root / "out" / "cancelled.txt.part"));

        // Single file into a fresh name, and a missing source.
        CHECK(copyTree(local, "/tree/one.txt", dest, "/renamed-copy.txt"));
        CHECK(!copyTree(local, "/missing", dest, "/x"));
    }

    // Memory streams.
    {
        MemorySource src(std::string("hello"));
        MemorySink sink;
        char buf[3];
        for (;;) {
            auto n = src.read(buf, sizeof buf);
            CHECK(n);
            if (n.value() == 0)
                break;
            sink.write(buf, n.value());
        }
        CHECK(sink.finish() && sink.data().size() == 5);
    }

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
    CHECK(!demo.openRead("/readme.txt"));
    CHECK(!demo.makeDirectory("/new"));
    CHECK(demo.stat("/Games/Sample Title/default.xex", st) && st.kind == "xex");

    auto registry = FileSystemRegistry::withBuiltins();
    std::string error;
    CHECK(registry.open("Local", root, error) != nullptr);
    CHECK(registry.open("Nope", root, error) == nullptr);
    CHECK(registry.open("Demo", {}, error) != nullptr);
    CHECK(registry.open("Local", root / "a.txt", error) == nullptr);

    fs::remove_all(root);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
