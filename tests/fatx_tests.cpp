// FATX backend tests. Images are made and checked with the FATX fork's own
// command-line tool (mkfs.fatx, fsck.fatx, label.fatx scripts), so the
// backend is checked against an independent writer and checker.
#include "core/FatxFileSystem.hpp"
#include "core/FileSystemRegistry.hpp"
#include "core/LocalFileSystem.hpp"
#include "core/PathUtil.hpp"
#include "core/Transfer.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <sstream>

namespace fs = std::filesystem;
using namespace unnamed::core;

static int failures = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

namespace {

#ifdef FATX_TOOL
const std::string kTool = FATX_TOOL;
#endif
fs::path scratch;

std::string quoted(const std::string& s) { return "'" + s + "'"; }

// Runs the fatx tool ("--as mkfs", ...); returns its exit code and output.
int runTool(const std::string& args, std::string* output = nullptr) {
#ifdef FATX_TOOL
    const fs::path log = scratch / "tool.log";
    const std::string cmd = quoted(kTool) + " " + args + " </dev/null >" + quoted(log.string()) + " 2>&1";
    const int rc = std::system(cmd.c_str());
    std::ifstream in(log);
    std::string text((std::istreambuf_iterator<char>(in)), {});
    if (output)
        *output = text;
    return rc;
#else
    (void)args;
    (void)output;
    return -1;
#endif
}

// Number of problems fsck.fatx reports (questions it would ask), -1 on failure.
int fsckProblems(const fs::path& image, const std::string& layout = "--table file") {
    std::string out;
    if (runTool("--as fsck -n " + layout + " " + quoted(image.string()), &out) != 0)
        return -1;
    int n = 0;
    for (std::size_t pos = 0; (pos = out.find("[Y/n]", pos)) != std::string::npos; ++pos)
        ++n;
    for (std::size_t pos = 0; (pos = out.find("[y/N]", pos)) != std::string::npos; ++pos)
        ++n;
    if (n)
        std::cerr << out;
    return n;
}

fs::path sparseFile(const std::string& name, std::uint64_t size) {
    const fs::path p = scratch / name;
    std::ofstream(p, std::ios::binary | std::ios::trunc).close();
    fs::resize_file(p, size);
    return p;
}

std::vector<char> randomData(std::size_t n, unsigned seed) {
    std::mt19937 gen(seed);
    std::vector<char> v(n);
    for (char& c : v)
        c = static_cast<char>(gen() & 0xFF);
    return v;
}

void writeFile(const fs::path& p, const std::vector<char>& data) {
    std::ofstream(p, std::ios::binary).write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::vector<char> readHostFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::vector<char> readAll(FileSystem& fsys, const std::string& path) {
    std::vector<char> out;
    auto src = fsys.openRead(path);
    if (!src)
        return out;
    std::vector<char> buf(100000);
    for (;;) {
        auto n = src.value()->read(buf.data(), buf.size());
        if (!n || n.value() == 0)
            break;
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<long>(n.value()));
    }
    return out;
}

std::unique_ptr<FileSystem> openPartition(const fs::path& image, const std::string& partition) {
    auto dev = openImageFile(image, false);
    if (!dev)
        return nullptr;
    for (const auto& p : probeFatx(dev.value())) {
        if (p.partition == partition) {
            auto f = openFatx(dev.value(), p, false, p.name);
            return f ? std::move(f.value()) : nullptr;
        }
    }
    return nullptr;
}

bool hasEntry(const std::vector<Entry>& entries, const std::string& name, EntryType type) {
    for (const auto& e : entries)
        if (e.name == name && e.type == type)
            return true;
    return false;
}

std::string details(const Details& d, const std::string& label) {
    for (const auto& g : d.groups)
        for (const auto& p : g.items)
            if (p.label == label)
                return p.value;
    return {};
}

// --- tests ---------------------------------------------------------------------

// A partition image written by mkfs.fatx and label.fatx scripts, read back.
void readImageMadeByTheTools() {
    const fs::path image = sparseFile("plain.img", 64ull << 20);
    CHECK(runTool("--as mkfs -y --table file " + quoted(image.string()) + " -l TOOLS") == 0);

    const auto small = randomData(5000, 1), big = randomData(3 * 1024 * 1024 + 17, 2), empty = std::vector<char>();
    writeFile(scratch / "small.bin", small);
    writeFile(scratch / "big.bin", big);
    writeFile(scratch / "empty.bin", empty);
    const std::string script = "mkdir,/Content; mkdir,/Content/0000000000000000; mkdir,/Content/Empty;"
                               "rcp," + (scratch / "small.bin").string() + ",/Content/0000000000000000/small.bin;"
                               "rcp," + (scratch / "big.bin").string() + ",/big.bin;"
                               "rcp," + (scratch / "empty.bin").string() + ",/Content/empty.bin";
    std::string out;
    CHECK(runTool("--as label --table file " + quoted(image.string()) + " -l TOOLS --do \"" + script + "\"", &out) == 0);
    CHECK(fsckProblems(image) == 0);
    const auto hashBefore = readHostFile(image);

    // through the registry, as the GUI's "Open" does for a single filesystem
    std::string error;
    auto fsys = FileSystemRegistry::withBuiltins().open("FATX", image, error);
    CHECK(fsys);
    if (!fsys) {
        std::cerr << error << "\n" << out;
        return;
    }
    CHECK(!hasCapability(fsys->capabilities(), Capability::Inject)); // read-only in this milestone step
    CHECK(hasCapability(fsys->capabilities(), Capability::Extract));

    std::vector<Entry> root;
    CHECK(fsys->list("/", root));
    CHECK(root.size() == 3);
    CHECK(hasEntry(root, "name.txt", EntryType::File));
    CHECK(hasEntry(root, "Content", EntryType::Directory));
    CHECK(hasEntry(root, "big.bin", EntryType::File));
    std::vector<Entry> content;
    CHECK(fsys->list("/Content", content));
    CHECK(content.size() == 3);
    CHECK(!fsys->list("/missing", content));
    CHECK(!fsys->list("/big.bin", content));

    Entry e;
    CHECK(fsys->stat("/big.bin", e));
    CHECK(e.size == big.size() && e.type == EntryType::File && e.modified > 0);
    CHECK(!fsys->stat("/nope", e));

    CHECK(readAll(*fsys, "/big.bin") == big);
    CHECK(readAll(*fsys, "/Content/0000000000000000/small.bin") == small);
    CHECK(readAll(*fsys, "/Content/empty.bin").empty());
    CHECK(!fsys->openRead("/Content"));

    Details d;
    CHECK(fsys->describeFileSystem(d));
    CHECK(details(d, "Label") == "TOOLS");
    CHECK(details(d, "Access") == "Read-only");
    CHECK(details(d, "Partition") == "Data");
    CHECK(fsys->describe("/Content/0000000000000000/small.bin", d));
    CHECK(details(d, "Size").find("5000") != std::string::npos);
    CHECK(!details(d, "First cluster").empty());

    std::string report;
    CHECK(fsys->healthCheck(report));
    CHECK(report == "No problems found.");

    // extract everything (copyTree FATX -> host) and compare
    const fs::path out_dir = scratch / "extract";
    fs::remove_all(out_dir);
    fs::create_directories(out_dir);
    LocalFileSystem host(out_dir);
    CHECK(copyTree(*fsys, "/", host, "/all"));
    CHECK(readHostFile(out_dir / "all" / "big.bin") == big);
    CHECK(readHostFile(out_dir / "all" / "Content" / "0000000000000000" / "small.bin") == small);
    CHECK(fs::is_directory(out_dir / "all" / "Content" / "Empty"));
    CHECK(fs::file_size(out_dir / "all" / "Content" / "empty.bin") == 0);

    // reading never changes the image
    fsys.reset();
    CHECK(readHostFile(image) == hashBefore);
    CHECK(fsckProblems(image) == 0);
}

// An Xbox 360 retail disk layout: every FATX partition is found.
void wholeDiskImage() {
    const fs::path image = sparseFile("disk.img", 0x130EB0000ull + (64ull << 20));
    CHECK(runTool("--as mkfs -y --table hd --partition x2 " + quoted(image.string()) + " -l DATA") == 0);
    CHECK(runTool("--as mkfs -y --table hd --partition x1 " + quoted(image.string()) + " -l COMPAT") == 0);
    CHECK(runTool("--as mkfs -y --table hd --partition sc " + quoted(image.string()) + " -l CACHE") == 0);
    CHECK(runTool("--as label --table hd --partition x1 " + quoted(image.string()) + " -l COMPAT --do \"mkdir,/xbox1\"") == 0);

    auto dev = openImageFile(image, false);
    CHECK(dev);
    if (!dev)
        return;
    const auto parts = probeFatx(dev.value());
    CHECK(parts.size() == 3);
    for (const auto& p : parts)
        CHECK(p.table == "hd");
    auto data = openPartition(image, "x2");
    auto compat = openPartition(image, "x1");
    auto cache = openPartition(image, "sc");
    CHECK(data && compat && cache);
    if (!data || !compat || !cache)
        return;
    Details d;
    CHECK(data->describeFileSystem(d) && details(d, "Label") == "DATA");
    CHECK(compat->describeFileSystem(d) && details(d, "Label") == "COMPAT");
    CHECK(details(d, "Partition offset") == "0x120EB0000");
    CHECK(cache->describeFileSystem(d) && details(d, "Partition") == "System cache");
    std::vector<Entry> entries;
    CHECK(compat->list("/", entries) && hasEntry(entries, "xbox1", EntryType::Directory));
    CHECK(data->list("/", entries) && !hasEntry(entries, "xbox1", EntryType::Directory));
    CHECK(fsckProblems(image, "--table hd --partition x1") == 0);
}

// Damage is reported, not crashed on.
void damagedImages() {
    // not FATX at all
    const fs::path junk = scratch / "junk.img";
    writeFile(junk, randomData(1 << 20, 3));
    std::string error;
    CHECK(!FileSystemRegistry::withBuiltins().open("FATX", junk, error));
    CHECK(error.find("No FATX") != std::string::npos);
    CHECK(!FileSystemRegistry::withBuiltins().open("FATX", scratch / "does-not-exist.img", error));
    CHECK(!FileSystemRegistry::withBuiltins().open("FATX", scratch, error)); // a directory

    // a lost cluster chain: the health check reports it
    const fs::path image = sparseFile("lost.img", 32ull << 20);
    CHECK(runTool("--as mkfs -y --table file " + quoted(image.string())) == 0);
    CHECK(runTool("--as label --table file " + quoted(image.string()) + " -l XBOX --do \"mklost,40:44\"") == 0);
    CHECK(fsckProblems(image) == 1);
    auto fsys = FileSystemRegistry::withBuiltins().open("FATX", image, error);
    CHECK(fsys);
    std::string report;
    CHECK(fsys && fsys->healthCheck(report));
    CHECK(report.rfind("1 problem found.", 0) == 0);

    // truncated copies of a populated image
    const auto full = readHostFile(scratch / "plain.img");
    std::mt19937 gen(5);
    for (int i = 0; i < 40; ++i) {
        const fs::path cut = scratch / "cut.img";
        auto part = std::vector<char>(full.begin(), full.begin() + static_cast<long>(gen() % full.size()));
        writeFile(cut, part);
        auto t = FileSystemRegistry::withBuiltins().open("FATX", cut, error);
        if (t) {
            std::vector<Entry> entries;
            if (t->list("/", entries))
                for (const auto& e : entries)
                    if (e.type == EntryType::File)
                        readAll(*t, "/" + e.name);
        }
    }
}

} // namespace

int main() {
#ifndef FATX_TOOL
    std::cout << "SKIPPED: the fatx command-line tool was not built (needs Boost.Program_options)\n";
    return 77;
#else
    scratch = fs::temp_directory_path() / "unnamed_fatx_tests";
    fs::remove_all(scratch);
    fs::create_directories(scratch);

    readImageMadeByTheTools();
    wholeDiskImage();
    damagedImages();

    fs::remove_all(scratch);
    if (failures)
        std::cerr << failures << " check(s) failed\n";
    else
        std::cout << "All FATX tests passed\n";
    return failures ? 1 : 0;
#endif
}
