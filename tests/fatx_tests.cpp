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

bool haveTool() {
#ifdef FATX_TOOL
    return true;
#else
    return false;
#endif
}

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
    if (!haveTool())
        return 0; // the library's own fsck is used too (see clean())
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
    CHECK(!hasCapability(fsys->capabilities(), Capability::Inject)); // the registry opens read-only
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

// --- writing (library only, plus fsck.fatx when the tool is there) -------------

std::shared_ptr<BlockDevice> newImage(const std::string& name, std::uint64_t size, std::string label = "TEST") {
    const fs::path p = sparseFile(name, size);
    auto dev = openImageFile(p, true);
    if (!dev)
        return nullptr;
    FatxPartition plain; // "file" table, x2
    if (!formatFatx(dev.value(), plain, label))
        return nullptr;
    return dev.value();
}

std::unique_ptr<FileSystem> openOn(const std::shared_ptr<BlockDevice>& dev, bool writable) {
    const auto parts = probeFatx(dev);
    if (parts.empty())
        return nullptr;
    auto f = openFatx(dev, parts.front(), writable, "test");
    return f ? std::move(f.value()) : nullptr;
}

// Health check by the library (fsck dry run) and, when built, by fsck.fatx.
bool clean(const std::shared_ptr<BlockDevice>& dev, const fs::path& image) {
    auto ro = openOn(dev, false);
    std::string report;
    if (!ro || !ro->healthCheck(report))
        return false;
    if (report != "No problems found.") {
        std::cerr << report << "\n";
        return false;
    }
    return fsckProblems(image) == 0;
}

void makeHostTree(const fs::path& root) {
    fs::remove_all(root);
    fs::create_directories(root / "Content" / "0000000000000000" / "FFFE07D1");
    fs::create_directories(root / "Empty folder");
    const std::size_t sizes[] = {0, 1, 2047, 2048, 2049, 16384, 100000, 3 * 1024 * 1024 + 5};
    unsigned seed = 10;
    for (std::size_t n : sizes)
        writeFile(root / ("file" + std::to_string(n) + ".bin"), randomData(n, seed++));
    writeFile(root / "Content" / "0000000000000000" / "FFFE07D1" / "{braces} & spaces.dat", randomData(70000, 99));
}

bool sameTree(const fs::path& a, const fs::path& b) {
    std::size_t count = 0;
    for (const auto& e : fs::recursive_directory_iterator(a)) {
        ++count;
        const fs::path other = b / fs::relative(e.path(), a);
        if (e.is_directory() ? !fs::is_directory(other) : readHostFile(e.path()) != readHostFile(other)) {
            std::cerr << "differs: " << other << "\n";
            return false;
        }
    }
    std::size_t countB = 0;
    for ([[maybe_unused]] const auto& e : fs::recursive_directory_iterator(b))
        ++countB;
    return count == countB;
}

// Inject a host tree (copyTree host -> FATX), read it back after reopening.
void writeRoundTrip() {
    const fs::path image = scratch / "rw.img";
    auto dev = newImage("rw.img", 64ull << 20);
    CHECK(dev);
    if (!dev)
        return;
    auto fsys = openOn(dev, true);
    CHECK(fsys);
    if (!fsys)
        return;
    for (auto cap : {Capability::Inject, Capability::Replace, Capability::Remove, Capability::MakeDirectory,
                     Capability::Rename, Capability::Clear, Capability::Repair, Capability::Format})
        CHECK(hasCapability(fsys->capabilities(), cap));

    makeHostTree(scratch / "tree");
    LocalFileSystem host(scratch);
    CHECK(copyTree(host, "/tree", *fsys, "/tree"));
    CHECK(fsys->makeDirectory("/tree/New Folder"));
    CHECK(!fsys->makeDirectory("/tree/new folder")); // case-insensitive duplicate
    CHECK(fsys->rename("/tree/file1.bin", "one.bin"));
    CHECK(fsys->rename("/tree/one.bin", "One.bin")); // case-only rename
    fs::rename(scratch / "tree" / "file1.bin", scratch / "tree" / "One.bin");
    fs::create_directories(scratch / "tree" / "New Folder");

    // a stream of unknown size, written in odd chunks (the file grows as it goes)
    {
        auto sink = fsys->openWrite("/tree/stream.bin", std::nullopt, false);
        CHECK(sink);
        const auto data = randomData(123457, 7);
        for (std::size_t off = 0; sink && off < data.size(); off += 1000)
            CHECK(sink.value()->write(data.data() + off, std::min<std::size_t>(1000, data.size() - off)));
        CHECK(sink && sink.value()->finish());
        writeFile(scratch / "tree" / "stream.bin", data);
    }
    // a size hint larger than the data (truncated in finish) and smaller (grows)
    {
        const auto data = randomData(5000, 8);
        auto sink = fsys->openWrite("/tree/hint-large.bin", 100000, false);
        CHECK(sink && sink.value()->write(data.data(), data.size()) && sink.value()->finish());
        writeFile(scratch / "tree" / "hint-large.bin", data);
        auto sink2 = fsys->openWrite("/tree/hint-small.bin", 10, false);
        CHECK(sink2 && sink2.value()->write(data.data(), data.size()) && sink2.value()->finish());
        writeFile(scratch / "tree" / "hint-small.bin", data);
    }
    // replace with a different size
    {
        const auto data = randomData(40000, 9);
        writeFile(scratch / "src.bin", data);
        CHECK(!copyTree(host, "/src.bin", *fsys, "/tree/file100000.bin")); // exists
        TransferOptions replace;
        replace.overwrite = true;
        CHECK(copyTree(host, "/src.bin", *fsys, "/tree/file100000.bin", replace));
        writeFile(scratch / "tree" / "file100000.bin", data);
    }
    fsys.reset();
    CHECK(clean(dev, image));

    // read everything back from a fresh read-only open
    auto ro = openOn(dev, false);
    CHECK(ro);
    if (!ro)
        return;
    fs::remove_all(scratch / "back");
    fs::create_directories(scratch / "back");
    LocalFileSystem back(scratch / "back");
    CHECK(copyTree(*ro, "/tree", back, "/tree"));
    CHECK(sameTree(scratch / "tree", scratch / "back" / "tree"));

    // delete a whole tree, clear a folder
    ro.reset();
    fsys = openOn(dev, true);
    CHECK(fsys->remove("/tree/Content"));
    CHECK(fsys->clear("/tree"));
    std::vector<Entry> entries;
    CHECK(fsys->list("/tree", entries) && entries.empty());
    CHECK(fsys->list("/", entries) && entries.size() == 2); // name.txt + tree
    CHECK(!fsys->remove("/name.txt"));
    fsys.reset();
    CHECK(clean(dev, image));
}

// Nothing is visible before finish(); a dropped sink leaves nothing behind.
void atomicWrites() {
    const fs::path image = scratch / "atomic.img";
    auto dev = newImage("atomic.img", 16ull << 20);
    auto fsys = dev ? openOn(dev, true) : nullptr;
    CHECK(fsys);
    if (!fsys)
        return;
    const auto data = randomData(50000, 1);
    Entry e;
    std::vector<Entry> entries;
    {
        auto sink = fsys->openWrite("/game.xex", data.size(), false);
        CHECK(sink && sink.value()->write(data.data(), 20000));
        CHECK(!fsys->stat("/game.xex", e)); // not there yet
    } // dropped: discarded
    CHECK(!fsys->stat("/game.xex", e));
    CHECK(fsys->list("/", entries) && entries.size() == 1); // only name.txt: no temporary file left
    {
        auto keep = fsys->openWrite("/game.xex", data.size(), false);
        CHECK(keep && keep.value()->write(data.data(), data.size()) && keep.value()->finish());
        // replacing: the old contents stay until finish()
        auto sink = fsys->openWrite("/game.xex", 10, true);
        CHECK(sink && sink.value()->write("0123456789", 10));
        CHECK(fsys->stat("/game.xex", e) && e.size == data.size());
    }
    CHECK(fsys->stat("/game.xex", e) && e.size == data.size());
    CHECK(readAll(*fsys, "/game.xex") == data);
    fsys.reset();
    CHECK(clean(dev, image));

    // read-only filesystems refuse writes
    auto ro = openOn(dev, false);
    CHECK(ro && !hasCapability(ro->capabilities(), Capability::Inject));
    auto refused = ro->openWrite("/x", 1, false);
    CHECK(!refused && refused.status().message.find("read-only") != std::string::npos);
    CHECK(!ro->makeDirectory("/x"));
}

// FATX name rules and other errors come back as clear messages.
void namesAndErrors() {
    auto dev = newImage("names.img", 16ull << 20);
    auto fsys = dev ? openOn(dev, true) : nullptr;
    CHECK(fsys);
    if (!fsys)
        return;
    auto message = [&](const std::string& name) {
        const Status st = fsys->makeDirectory("/" + name);
        return st ? std::string() : st.message;
    };
    CHECK(message("ok name (1) [a] {b} ~!#$%&'-.@^_`").empty());
    CHECK(message("a:b").find("not a valid FATX name") != std::string::npos);
    CHECK(message("a*b").find("not a valid FATX name") != std::string::npos);
    CHECK(message("caf\xc3\xa9").find("only ASCII") != std::string::npos);
    CHECK(message(std::string(43, 'x')).find("at most 42") != std::string::npos);
    CHECK(message(std::string(42, 'x')).empty());
    CHECK(message("OK NAME (1) [A] {B} ~!#$%&'-.@^_`").find("upper and lower case") != std::string::npos);
    CHECK(!fsys->rename("/" + std::string(42, 'x'), "bad/name"));
    CHECK(!fsys->rename("/", "x"));
    CHECK(!fsys->remove("/"));
    CHECK(!fsys->makeDirectory("/missing/child"));
    auto big = fsys->openWrite("/huge.bin", 5ull << 30, false);
    CHECK(!big && big.status().message.find("4 GiB") != std::string::npos);
    auto file = fsys->openWrite("/afile", 1, false);
    CHECK(file && file.value()->write("x", 1) && file.value()->finish());
    auto inFile = fsys->openWrite("/afile/sub", 1, false); // a file is not a folder
    CHECK(!inFile);
}

// A full partition is reported and leaves the filesystem consistent.
void noSpaceLeft() {
    const fs::path image = scratch / "small.img";
    auto dev = newImage("small.img", 4ull << 20);
    auto fsys = dev ? openOn(dev, true) : nullptr;
    CHECK(fsys);
    if (!fsys)
        return;
    const auto data = randomData(6 << 20, 3);
    writeFile(scratch / "toobig.bin", data);
    LocalFileSystem host(scratch);
    const Status st = copyTree(host, "/toobig.bin", *fsys, "/toobig.bin");
    CHECK(!st && st.message.find("not enough free space") != std::string::npos);
    // unknown size: fails while writing, the partial file is removed
    {
        auto sink = fsys->openWrite("/stream.bin", std::nullopt, false);
        CHECK(sink);
        bool failed = false;
        for (std::size_t off = 0; sink && off < data.size() && !failed; off += 65536)
            failed = !sink.value()->write(data.data() + off, 65536);
        CHECK(failed);
    }
    std::vector<Entry> entries;
    CHECK(fsys->list("/", entries) && entries.size() == 1);
    fsys.reset();
    CHECK(clean(dev, image));
}

// Format and repair through the FileSystem interface.
void formatAndRepair() {
    const fs::path image = scratch / "fmt.img";
    auto dev = newImage("fmt.img", 32ull << 20, "OLD");
    auto fsys = dev ? openOn(dev, true) : nullptr;
    CHECK(fsys);
    if (!fsys)
        return;
    CHECK(fsys->makeDirectory("/dir"));
    CHECK(fsys->format("NEW LABEL"));
    std::vector<Entry> entries;
    CHECK(fsys->list("/", entries) && entries.size() == 1 && entries[0].name == "name.txt");
    Details d;
    CHECK(fsys->describeFileSystem(d) && details(d, "Label") == "NEW LABEL");
    CHECK(!fsys->format(std::string(43, 'x')));
    CHECK(fsys->makeDirectory("/after"));
    fsys.reset();
    CHECK(clean(dev, image));

    // a lost cluster chain, written behind the library's back
    {
        auto ro = openOn(dev, false);
        CHECK(ro->describeFileSystem(d));
    }
    const std::uint64_t fatOffset = std::stoull(details(d, "FAT offset"), nullptr, 16);
    const unsigned char eoc[2] = {0xFF, 0xFF}; // 16-bit FAT on a 32 MiB partition
    CHECK(dev->writeAt(fatOffset + 300 * 2, eoc, 2) && dev->flush());
    fsys = openOn(dev, true);
    std::string report;
    CHECK(fsys->healthCheck(report) && report.rfind("1 problem found.", 0) == 0);
    CHECK(fsys->repair(report) && report.rfind("1 problem found and repaired.", 0) == 0);
    CHECK(fsys->healthCheck(report) && report == "No problems found.");
    CHECK(fsys->makeDirectory("/still works"));
    fsys.reset();
    CHECK(clean(dev, image));

    // a read-only filesystem neither repairs nor formats
    auto ro = openOn(dev, false);
    CHECK(!ro->repair(report) && !ro->format("X"));
}

// Damaged images: errors, no crash (run under ASan/UBSan in CI).
void corruptedImagesDoNotCrash() {
    auto dev = newImage("fuzz-src.img", 4ull << 20);
    auto fsys = dev ? openOn(dev, true) : nullptr;
    CHECK(fsys);
    if (!fsys)
        return;
    CHECK(fsys->makeDirectory("/a"));
    for (int i = 0; i < 6; ++i) {
        auto sink = fsys->openWrite("/a/f" + std::to_string(i), 5000u * unsigned(i), false);
        const auto data = randomData(5000u * unsigned(i), unsigned(i));
        CHECK(sink && (data.empty() || sink.value()->write(data.data(), data.size())) && sink.value()->finish());
    }
    fsys.reset();
    const auto pristine = readHostFile(scratch / "fuzz-src.img");
    std::mt19937 gen(77);
    for (int round = 0; round < 60; ++round) {
        auto bytes = pristine;
        for (int i = 0; i < 8; ++i)
            bytes[gen() % (128 * 1024)] = static_cast<char>(gen());
        writeFile(scratch / "fuzz.img", bytes);
        auto d = openImageFile(scratch / "fuzz.img", true);
        if (!d)
            continue;
        auto f = openOn(d.value(), true);
        if (!f)
            continue;
        std::vector<Entry> entries;
        if (f->list("/a", entries))
            for (const auto& e : entries)
                readAll(*f, "/a/" + e.name);
        std::string report;
        void(f->healthCheck(report));
        void(f->makeDirectory("/new"));
        void(f->remove("/a/f1"));
    }
}

// The library writes into an image made by mkfs.fatx; fsck.fatx agrees.
void writeIntoImageMadeByTheTools() {
    const fs::path image = sparseFile("tool-rw.img", 48ull << 20);
    CHECK(runTool("--as mkfs -y --table file " + quoted(image.string()) + " -l TOOLS") == 0);
    auto dev = openImageFile(image, true);
    CHECK(dev);
    auto fsys = dev ? openOn(dev.value(), true) : nullptr;
    CHECK(fsys);
    if (!fsys)
        return;
    makeHostTree(scratch / "tree2");
    LocalFileSystem host(scratch);
    CHECK(copyTree(host, "/tree2", *fsys, "/tree2"));
    fsys.reset();
    CHECK(fsckProblems(image) == 0);
    // the tool reads back what the library wrote
    std::string out;
    CHECK(runTool("--as label --table file " + quoted(image.string()) + " --do \"lcp,/tree2/file100000.bin," +
                  (scratch / "lcp.bin").string() + "\"", &out) == 0);
    CHECK(readHostFile(scratch / "lcp.bin") == readHostFile(scratch / "tree2" / "file100000.bin"));
}

} // namespace

int main() {
    scratch = fs::temp_directory_path() / "unnamed_fatx_tests";
    fs::remove_all(scratch);
    fs::create_directories(scratch);

    // need only the library
    writeRoundTrip();
    atomicWrites();
    namesAndErrors();
    noSpaceLeft();
    formatAndRepair();
    corruptedImagesDoNotCrash();

    // need the fatx command-line tool (mkfs.fatx, fsck.fatx, label.fatx)
    if (haveTool()) {
        readImageMadeByTheTools();
        wholeDiskImage();
        damagedImages();
        writeIntoImageMadeByTheTools();
    } else {
        std::cout << "SKIPPED (fatx tool not built, needs Boost.Program_options): tests against mkfs.fatx/fsck.fatx\n";
    }

    fs::remove_all(scratch);
    if (failures)
        std::cerr << failures << " check(s) failed\n";
    else
        std::cout << "All FATX tests passed\n";
    return failures ? 1 : 0;
}
