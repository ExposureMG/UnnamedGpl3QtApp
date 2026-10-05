// Tests the XEX format handler against the XexTool command line.
//
// The XexTool fork's golden record (tests/golden/expected.txt) holds what the
// command line printed for each step over its sample inputs and the SHA-256 of
// every file it wrote. Each handler operation with a command line equivalent
// runs here on the same inputs, through the FileSystem and OperationIo paths
// the app uses, and must write the same bytes and report the same text.
//
//   xex_tests <golden samples dir> <expected.txt>
//
// The reports print file times in local time; run with TZ=UTC, as the record was.
#include "core/FormatHandlerRegistry.hpp"
#include "core/LocalFileSystem.hpp"
#include "core/PathUtil.hpp"
#include "core/XexHandler.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <random>

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

namespace {

// --- SHA-256 (FIPS 180-4), for comparing with the golden record --------------------

class Sha256 {
public:
    void update(const void* data, std::size_t size) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        m_length += size;
        while (size > 0) {
            const std::size_t n = std::min(size, sizeof m_buffer - m_used);
            std::memcpy(m_buffer + m_used, p, n);
            m_used += n;
            p += n;
            size -= n;
            if (m_used == sizeof m_buffer) {
                block(m_buffer);
                m_used = 0;
            }
        }
    }

    std::string hexDigest() {
        const std::uint64_t bits = m_length * 8;
        const std::uint8_t pad = 0x80, zero = 0;
        update(&pad, 1);
        while (m_used != 56)
            update(&zero, 1);
        std::uint8_t length[8];
        for (int i = 0; i < 8; ++i)
            length[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        update(length, 8);
        std::string out;
        char buf[9];
        for (const std::uint32_t h : m_h) {
            std::snprintf(buf, sizeof buf, "%08x", h);
            out += buf;
        }
        return out;
    }

private:
    static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void block(const std::uint8_t* p) {
        static constexpr std::uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = std::uint32_t(p[4 * i]) << 24 | std::uint32_t(p[4 * i + 1]) << 16 |
                   std::uint32_t(p[4 * i + 2]) << 8 | std::uint32_t(p[4 * i + 3]);
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3], e = m_h[4], f = m_h[5], g = m_h[6],
                      h = m_h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        const std::uint32_t add[8] = {a, b, c, d, e, f, g, h};
        for (int i = 0; i < 8; ++i)
            m_h[i] += add[i];
    }

    std::uint32_t m_h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint8_t m_buffer[64] = {};
    std::size_t m_used = 0;
    std::uint64_t m_length = 0;
};

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

void writeFile(const fs::path& path, const std::string& data) {
    std::ofstream(path, std::ios::binary) << data;
}

std::string sha256(const std::string& data) {
    Sha256 h;
    h.update(data.data(), data.size());
    return h.hexDigest();
}

std::string fileHash(const fs::path& path) {
    return fs::exists(path) ? sha256(readFile(path)) : std::string("(missing)");
}

// --- the golden record ----------------------------------------------------------------

struct Step {
    std::string command;
    std::string output;                      // what it printed
    std::map<std::string, std::string> files; // file name -> SHA-256
};

std::map<std::string, Step> parseRecord(const std::string& text) {
    std::map<std::string, Step> steps;
    std::size_t pos = 0;
    while ((pos = text.find("== ", pos)) != std::string::npos) {
        std::size_t next = text.find("\n== ", pos);
        next = next == std::string::npos ? text.size() : next + 1;
        const std::string section = text.substr(pos, next - pos);
        pos = next;

        const std::size_t colon = section.find(": ");
        const std::size_t lineEnd = section.find('\n');
        const std::size_t exitEnd = section.find('\n', lineEnd + 1);
        if (colon == std::string::npos || lineEnd == std::string::npos || exitEnd == std::string::npos)
            continue;
        Step step;
        step.command = section.substr(colon + 2, lineEnd - colon - 2);
        std::string body = section.substr(exitEnd + 1);
        // each step ends in an empty line of the record's own
        if (!body.empty() && body.back() == '\n')
            body.pop_back();
        const std::size_t files = body.find("-- files\n");
        if (files != std::string::npos) {
            std::size_t line = files + 9;
            while (line < body.size()) {
                const std::size_t end = body.find('\n', line);
                const std::string entry = body.substr(line, end - line);
                if (entry.size() > 66)
                    step.files[entry.substr(66)] = entry.substr(0, 64);
                line = end == std::string::npos ? body.size() : end + 1;
            }
            body.erase(files);
        }
        step.output = body.substr(0, body.find("-- stderr\n"));
        steps[section.substr(3, colon - 3)] = step;
    }
    return steps;
}

std::map<std::string, Step> g_record;

// The hash the record has for `file` written by `step`.
std::string recorded(const std::string& step, const std::string& file) {
    const auto s = g_record.find(step);
    if (s == g_record.end())
        return "(no step " + step + ")";
    const auto f = s->second.files.find(file);
    return f == s->second.files.end() ? "(no file " + file + " in " + step + ")" : f->second;
}

// The output must be byte for byte the file the command line wrote in `step`.
bool matches(const fs::path& output, const std::string& step, const std::string& file, int line) {
    const std::string want = recorded(step, file);
    const std::string got = fileHash(output);
    if (got == want)
        return true;
    std::cerr << __FILE__ << ":" << line << ": " << output.filename().string() << " differs from " << step << " ("
              << g_record[step].command << "): " << got << " != " << want << "\n";
    ++failures;
    return false;
}
#define MATCHES(output, step, file) matches((output), (step), (file), __LINE__)

const std::string kBanner = "XexTool v7.0  -  xorloser 2006-2017 (Build)\nReading and parsing input xex file...\n";

// --- helpers --------------------------------------------------------------------------

struct Fixture {
    fs::path root;
    fs::path src; // the place the sources are in (a LocalFileSystem)
    fs::path out; // host folder for outputs
    std::shared_ptr<const FormatHandler> xex;
    std::unique_ptr<LocalFileSystem> place;

    std::string host(const std::string& name) const { return pathToUtf8(out / name); }

    Result<std::string> run(const std::string& op, const std::string& source, Parameters parameters,
                            const ProgressFn& progress = {}) {
        HostOperationIo io(place.get(), "/" + source);
        return runOperation(*xex, op, *place, "/" + source, std::move(parameters), io, progress);
    }

    // Runs an operation that writes `name` in the output folder.
    bool runTo(const std::string& op, const std::string& source, Parameters parameters, const std::string& name) {
        parameters["output"] = host(name);
        const auto result = run(op, source, std::move(parameters));
        if (!result)
            std::cerr << op << " " << source << ": " << result.status().message << "\n";
        return bool(result);
    }

    // Puts an output next to the sources, to be the source of a later step.
    void keep(const std::string& name) { fs::copy_file(out / name, src / name, fs::copy_options::overwrite_existing); }
};

FileProbe probeOf(std::string name, const std::string& head) {
    FileProbe p;
    p.name = std::move(name);
    p.size = head.size();
    p.head.assign(head.begin(), head.end());
    return p;
}

// A source that claims to be larger than the handler accepts.
class HugeFileSystem final : public FileSystem {
public:
    std::string name() const override { return "Huge"; }
    Capability capabilities() const override { return Capability::Browse | Capability::Extract; }
    Status list(const std::string&, std::vector<Entry>& out) const override {
        out = {{"huge.xex", EntryType::File, "xex", XexHandler::kMaxSize + 1, 0}};
        return Status::success();
    }
    Result<std::unique_ptr<ByteSource>> openRead(const std::string&) const override {
        std::string head = "XEX2";
        head.resize(4096);
        return std::unique_ptr<ByteSource>(std::make_unique<MemorySource>(head));
    }
};

// A view of a place that can be read but not changed.
class ReadOnlyView final : public FileSystem {
public:
    explicit ReadOnlyView(const LocalFileSystem& inner) : m_inner(inner) {}
    std::string name() const override { return "Read-only view"; }
    Capability capabilities() const override { return Capability::Browse | Capability::Extract; }
    Status list(const std::string& path, std::vector<Entry>& out) const override { return m_inner.list(path, out); }
    Status stat(const std::string& path, Entry& out) const override { return m_inner.stat(path, out); }
    Result<std::unique_ptr<ByteSource>> openRead(const std::string& path) const override {
        return m_inner.openRead(path);
    }

private:
    const LocalFileSystem& m_inner;
};

std::string property(const std::vector<PropertyGroup>& groups, const std::string& group, const std::string& label) {
    for (const PropertyGroup& g : groups) {
        if (g.title != group)
            continue;
        for (const Property& p : g.items) {
            if (p.label == label)
                return p.value;
        }
    }
    return "(none)";
}

Result<std::vector<PropertyGroup>> describeFile(const FormatHandler& handler, const FileSystem& fs,
                                                const std::string& path) {
    auto probe = probeFile(fs, path);
    if (!probe)
        return probe.status();
    auto in = fs.openRead(path);
    if (!in)
        return in.status();
    std::vector<PropertyGroup> groups;
    if (const Status st = handler.describe(*in.value(), probe.value(), groups); !st)
        return st;
    return groups;
}

// --- tests ------------------------------------------------------------------------------

void testDescriptors(const FormatHandler& xex) {
    std::vector<std::string> ids;
    for (const OperationDescriptor& op : xex.operations()) {
        const Status st = validateDescriptor(op);
        if (!st)
            std::cerr << st.message << "\n";
        CHECK(st);
        ids.push_back(op.id);
        // Everything that writes a xex offers a new file (the default) or in place.
        if (op.modifiesSource) {
            CHECK(!rewritesSource(op, {}));
            CHECK(rewritesSource(op, {{"target", std::string{"source"}}}));
        }
    }
    const std::vector<std::string> expected = {"info",    "basefile", "idc",    "resources",    "decrypt",
                                               "encrypt", "compression", "machine", "patch", "limits",
                                               "boundingPath", "updateFix", "exportInfo", "importInfo"};
    CHECK(ids == expected);
}

void testProbe(const FormatHandler& xex, Fixture& f) {
    const std::string titled = readFile(f.src / "titled.xex");
    CHECK(xex.probe(probeOf("titled.xex", titled.substr(0, kProbeSize))) == kMatchContent);
    CHECK(xex.probe(probeOf("default", titled.substr(0, kProbeSize))) > kMatchName); // the content decides
    CHECK(xex.probe(probeOf("patch.xexp", readFile(f.src / "patch.xexp"))) == kMatchContent);
    CHECK(xex.probe(probeOf("old.xex", "XEX1" + std::string(60, '\0'))) == kMatchContent);
    CHECK(xex.probe(probeOf("empty.xex", "")) == kMatchNone);
    CHECK(xex.probe(probeOf("short.xex", "XE")) == kMatchNone);
    CHECK(xex.probe(probeOf("text.xex", "This is not an executable")) == kMatchNone);
    CHECK(xex.probe(probeOf("sample.elf", readFile(f.src / "sample.elf"))) == kMatchNone);

    // The registry offers it first, the generic tools after it.
    const auto registry = FormatHandlerRegistry::withBuiltins();
    auto probe = probeFile(*f.place, "/titled.xex");
    CHECK(probe);
    const auto found = registry.match(probe.value());
    CHECK(found.size() == 2 && found[0].handler->id() == "xex" && found[1].handler->id() == "file");
    CHECK(applicableOperations(xex, probe.value()).size() == xex.operations().size());
    // A patch cannot be patched; an XEX1 file has no tools.
    const auto onPatch = applicableOperations(xex, probeFile(*f.place, "/patch.xexp").value());
    CHECK(std::none_of(onPatch.begin(), onPatch.end(), [](const OperationDescriptor& o) { return o.id == "patch"; }));
    CHECK(applicableOperations(xex, probeOf("old.xex", "XEX1" + std::string(60, '\0'))).empty());

    // Damaged files: clean errors from the expanded view and the tools.
    writeFile(f.src / "empty.xex", "");
    writeFile(f.src / "text.xex", "This is not an executable");
    writeFile(f.src / "truncated.xex", titled.substr(0, 100));
    writeFile(f.src / "headers.xex", titled.substr(0, 0x1000));
    writeFile(f.src / "old.xex", "XEX1" + std::string(60, '\0'));
    for (const char* name : {"empty.xex", "text.xex"}) {
        auto p = probeFile(*f.place, std::string("/") + name);
        CHECK(p && xex.probe(p.value()) == kMatchNone);
        CHECK(!f.run("info", name, {})); // does not apply
    }
    for (const char* name : {"truncated.xex", "headers.xex"}) {
        auto groups = describeFile(xex, *f.place, std::string("/") + name);
        CHECK(groups && property(groups.value(), "Executable", "Error") != "(none)");
        const auto info = f.run("info", name, {});
        CHECK(!info && !info.status().cancelled && !info.status().message.empty());
        CHECK(!f.run("decrypt", name, {{"output", f.host(std::string(name) + ".out")}}));
        CHECK(!fs::exists(f.out / (std::string(name) + ".out")));
    }
    auto old = describeFile(xex, *f.place, "/old.xex");
    CHECK(old && property(old.value(), "Executable", "Format") == "XEX1 (pre-release)");
    CHECK(!f.run("info", "old.xex", {}));

    // Too large: refused before reading.
    HugeFileSystem huge;
    HostOperationIo hugeIo(nullptr, {});
    const auto big = runOperation(xex, "info", huge, "/huge.xex", {}, hugeIo);
    CHECK(!big && big.status().message.find("too large") != std::string::npos);
    auto bigView = describeFile(xex, huge, "/huge.xex");
    CHECK(bigView && property(bigView.value(), "Executable", "Note").find("Larger than") == 0);
}

void testDescribe(const FormatHandler& xex, Fixture& f) {
    auto groups = describeFile(xex, *f.place, "/titled.xex");
    CHECK(groups);
    const auto& g = groups.value();
    CHECK(property(g, "Executable", "Format") == "XEX2");
    CHECK(property(g, "Executable", "Title") == "Golden <Sample> & Co");
    CHECK(property(g, "Executable", "Basefile") == "PE (executable)");
    CHECK(property(g, "Executable", "Flags") == "Title Module");
    CHECK(property(g, "Executable", "Base address") == "0x82000000");
    CHECK(property(g, "Executable", "Entry point") == "0x82001000");
    CHECK(property(g, "Executable", "Image size") == "0x00040000 (256 KiB)");
    CHECK(property(g, "Executable", "PE timestamp") == "0x4A2A6A4F (2009-06-06 13:08:31 UTC)");
    CHECK(property(g, "Executable", "Stack size") == "0x00040000 (256 KiB)");
    CHECK(property(g, "Execution ID", "Title ID") == "4E4D07D0 (NM-2000)");
    CHECK(property(g, "Execution ID", "Media ID") == "0x11223344");
    CHECK(property(g, "Execution ID", "Version") == "v2.0.48.1");
    CHECK(property(g, "Execution ID", "Base version") == "v2.0.0.0");
    CHECK(property(g, "Execution ID", "Disc") == "1 of 2");
    CHECK(property(g, "Execution ID", "Alternate title ID") == "4E4D07D1 (NM-2001)");
    CHECK(property(g, "Security", "Machine") == "Retail (signature cleared)");
    CHECK(property(g, "Security", "Encryption") == "Not encrypted");
    CHECK(property(g, "Security", "Compression") == "Normal (LZX compressed)");
    CHECK(property(g, "Security", "Page size") == "0x00010000 (64 KiB)");
    CHECK(property(g, "Security", "Regions").find("North America") == 0);
    CHECK(property(g, "Security", "Allowed media").find("Hard Disk, DVD-X2 (Xbox1 Original Disc)") == 0);
    CHECK(property(g, "Security", "Media ID") == "00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF");
    CHECK(property(g, "Security", "Encryption key") == "72 C0 FE 97 DA 43 7A C1 6D 78 4F CC 3F B4 87 0E");
    CHECK(property(g, "Security", "LAN key") == "A0 A1 A2 A3 A4 A5 A6 A7 A8 A9 AA AB AC AD AE AF");
    CHECK(property(g, "Security", "Bounding path") == "\\Device\\Harddisk0\\Partition1\\Games\\Sample");
    CHECK(property(g, "Security", "Section 2") == "0x82020000 – 0x82040000 Data");
    CHECK(property(g, "Ratings", "ESRB") == "06");
    // the block's own byte: XexTool's report prints the FPB byte for Taiwan and Singapore
    CHECK(property(g, "Ratings", "FPB") == "20");
    CHECK(property(g, "Ratings", "Singapore") == "01");
    CHECK(property(g, "Libraries", "xboxkrnl.exe (imports)") == "v0.0.0.0, at least v0.0.0.0, 2 records");
    CHECK(property(g, "Resources", "4E4D07D0") == "0x82010010, 155 bytes");

    groups = describeFile(xex, *f.place, "/dev.xex");
    CHECK(groups && property(groups.value(), "Security", "Machine") == "Devkit");
    CHECK(groups && property(groups.value(), "Security", "Encryption") == "Encrypted");
    groups = describeFile(xex, *f.place, "/patch.xexp");
    CHECK(groups && property(groups.value(), "Security", "Compression") == "Delta patch");
}

// The report of Info, against what XexTool -l (or XexTool alone) printed.
void checkReport(Fixture& f, const std::string& source, const std::string& step, bool full) {
    auto report = f.run("info", source, {{"detail", std::string{full ? "full" : "summary"}}});
    CHECK(report);
    const std::string want = kBanner + "\n" + (report ? report.value() : std::string()) + "\n";
    if (!report || g_record[step].output != want) {
        std::cerr << "info " << source << " differs from " << step << "\n";
        ++failures;
    }
}

void testGolden(Fixture& f) {
    // -z s: the title's facts set from info.xml make titled.xex, the source of
    // most other steps.
    CHECK(f.runTo("importInfo", "sample.xex", {{"info", pathToUtf8(f.src / "info.xml")}}, "titled.xex"));
    if (!MATCHES(f.out / "titled.xex", "set_info", "titled.xex"))
        return;
    f.keep("titled.xex");

    // -l, and the short list
    checkReport(f, "titled.xex", "list_titled", true);
    checkReport(f, "sample.xex", "list", false);
    checkReport(f, "patch.xexp", "list_patch", true);
    auto saved = f.run("info", "titled.xex", {{"save", true}, {"output", f.host("titled.txt")}});
    CHECK(saved && readFile(f.out / "titled.txt") == saved.value());

    // -b, -i, -d, -z g
    CHECK(f.runTo("basefile", "titled.xex", {}, "basefile.bin"));
    MATCHES(f.out / "basefile.bin", "basefile", "basefile.bin");
    CHECK(f.runTo("idc", "titled.xex", {}, "sample.idc"));
    MATCHES(f.out / "sample.idc", "idc", "sample.idc");
    fs::create_directories(f.out / "res");
    auto res = f.run("resources", "titled.xex", {{"folder", f.host("res")}});
    CHECK(res && res.value().find("4E4D07D0") != std::string::npos);
    MATCHES(f.out / "res" / "4E4D07D0", "resources", "res\\4E4D07D0");
    CHECK(f.runTo("exportInfo", "titled.xex", {}, "got.xml"));
    MATCHES(f.out / "got.xml", "get_info", "got.xml");

    // -c u, -c b, -c c
    CHECK(f.runTo("compression", "titled.xex", {{"format", std::string{"basic"}}}, "basic.xex"));
    MATCHES(f.out / "basic.xex", "to_basic", "basic.xex");
    f.keep("basic.xex");
    CHECK(f.runTo("compression", "titled.xex", {{"format", std::string{"uncompressed"}}}, "none.xex"));
    MATCHES(f.out / "none.xex", "to_none", "none.xex");
    CHECK(f.runTo("compression", "basic.xex", {{"format", std::string{"normal"}}}, "normal.xex"));
    MATCHES(f.out / "normal.xex", "to_normal", "normal.xex");

    // -e e, -e u
    CHECK(f.runTo("encrypt", "titled.xex", {}, "encrypted.xex"));
    MATCHES(f.out / "encrypted.xex", "encrypt", "encrypted.xex");
    f.keep("encrypted.xex");
    CHECK(f.runTo("decrypt", "encrypted.xex", {}, "decrypted.xex"));
    MATCHES(f.out / "decrypted.xex", "decrypt", "decrypted.xex");
    checkReport(f, "encrypted.xex", "list_encrypted", true);

    // -m d: the record signs titled.xex with -m d -c c -e e in one run; here
    // the same comes from signing its encrypted form (it is compressed already).
    auto signedDev = f.run("machine", "encrypted.xex", {{"machine", std::string{"devkit"}}, {"output", f.host("dev.xex")}});
    CHECK(signedDev && signedDev.value().find("devkit key") != std::string::npos);
    if (!MATCHES(f.out / "dev.xex", "sign_devkit", "dev.xex"))
        return;
    f.keep("dev.xex");
    checkReport(f, "dev.xex", "list_devkit", true);

    // -m r: a cleared signature, which XexTool's retail signing reports as a
    // failure by design; it must not be one here.
    auto retail = f.run("machine", "dev.xex", {{"machine", std::string{"retail"}}, {"output", f.host("retail.xex")}});
    CHECK(retail && retail.value().find("no retail private key") != std::string::npos);
    MATCHES(f.out / "retail.xex", "to_retail", "retail.xex");
    f.keep("retail.xex");
    checkReport(f, "retail.xex", "list_retail", true);

    // -c b on a devkit file; -c u -e u in two steps
    CHECK(f.runTo("compression", "dev.xex", {{"format", std::string{"uncompressed"}}}, "dev_none.xex"));
    MATCHES(f.out / "dev_none.xex", "devkit_none", "dev_none.xex");
    CHECK(f.runTo("compression", "dev.xex", {{"format", std::string{"basic"}}}, "dev_basic.xex"));
    f.keep("dev_basic.xex");
    CHECK(f.runTo("decrypt", "dev_basic.xex", {}, "dev_plain.xex"));
    MATCHES(f.out / "dev_plain.xex", "devkit_plain", "dev_plain.xex");

    // -p, and the patches XexTool refuses
    const std::string patch = pathToUtf8(f.src / "patch.xexp");
    CHECK(f.runTo("patch", "dev.xex", {{"patch", patch}}, "patched.xex"));
    MATCHES(f.out / "patched.xex", "patch", "patched.xex");
    f.keep("patched.xex");
    checkReport(f, "patched.xex", "list_patched", true);
    auto wrong = f.run("patch", "titled.xex", {{"patch", patch}, {"output", f.host("wrong.xex")}});
    CHECK(!wrong && wrong.status().message.find("is not the correct patch file") != std::string::npos);
    wrong = f.run("patch", "titled.xex", {{"patch", pathToUtf8(f.src / "dev.xex")}, {"output", f.host("wrong.xex")}});
    CHECK(!wrong && wrong.status().message.find("is not a patch file") != std::string::npos);
    CHECK(!f.run("patch", "patch.xexp", {{"patch", patch}, {"output", f.host("wrong.xex")}})); // does not apply
    CHECK(!fs::exists(f.out / "wrong.xex"));

    // -r a, -r mrzby, -a, -u, -z s with a partial document
    auto unlocked = f.run("limits", "dev.xex", {{"output", f.host("unlocked.xex")}});
    CHECK(unlocked && unlocked.value().find("  removed region limit\n") != std::string::npos);
    MATCHES(f.out / "unlocked.xex", "remove_all_limits", "unlocked.xex");
    f.keep("unlocked.xex");
    checkReport(f, "unlocked.xex", "list_unlocked", true);
    CHECK(f.runTo("limits", "titled.xex",
                  {{"all", false}, {"media", true}, {"regions", true}, {"mediaId", true}, {"boundingPath", true},
                   {"dates", true}},
                  "partial.xex"));
    MATCHES(f.out / "partial.xex", "remove_some_limits", "partial.xex");
    CHECK(f.runTo("boundingPath", "dev.xex", {{"path", std::string{"\\Device\\Cdrom0\\Games"}}}, "bound.xex"));
    MATCHES(f.out / "bound.xex", "bounding_path", "bound.xex");
    CHECK(f.runTo("updateFix", "dev.xex", {}, "fixed.xex"));
    MATCHES(f.out / "fixed.xex", "update_fix", "fixed.xex");
    CHECK(f.runTo("importInfo", "titled.xex", {{"info", pathToUtf8(f.src / "info_partial.xml")}}, "partial_info.xex"));
    MATCHES(f.out / "partial_info.xex", "partial_info", "partial_info.xex");
    auto badInfo = f.run("importInfo", "titled.xex",
                         {{"info", pathToUtf8(f.src / "info_bad_hex.xml")}, {"output", f.host("bad.xex")}});
    CHECK(!badInfo && badInfo.status().message.find("Invalid MediaId length") != std::string::npos);
    CHECK(!fs::exists(f.out / "bad.xex"));

    // In place: XexTool -c u -e e inplace.xex rewrites the file it read.
    fs::copy_file(f.src / "titled.xex", f.src / "inplace.xex");
    const auto outputs = std::distance(fs::directory_iterator(f.out), fs::directory_iterator());
    CHECK(f.run("compression", "inplace.xex", {{"format", std::string{"basic"}}, {"target", std::string{"source"}}}));
    CHECK(f.run("encrypt", "inplace.xex", {{"target", std::string{"source"}}}));
    MATCHES(f.src / "inplace.xex", "in_place", "inplace.xex");
    CHECK(std::distance(fs::directory_iterator(f.out), fs::directory_iterator()) == outputs);
}

void testParameters(Fixture& f) {
    auto refused = [&](const std::string& op, Parameters p) {
        const auto r = f.run(op, "titled.xex", std::move(p));
        return !r && !r.status().cancelled;
    };
    CHECK(refused("compression", {{"format", std::string{"zip"}}, {"output", f.host("x.xex")}}));
    CHECK(refused("compression", {{"format", std::string{"basic"}}})); // no output file
    CHECK(refused("machine", {{"machine", std::string{"freeboot"}}, {"output", f.host("x.xex")}}));
    CHECK(refused("decrypt", {{"target", std::string{"elsewhere"}}}));
    CHECK(refused("patch", {{"output", f.host("x.xex")}})); // no patch file
    CHECK(refused("patch", {{"patch", pathToUtf8(f.root / "missing.xexp")}, {"output", f.host("x.xex")}}));
    CHECK(refused("boundingPath", {{"output", f.host("x.xex")}})); // no path
    CHECK(refused("importInfo", {{"output", f.host("x.xex")}}));   // no document
    CHECK(refused("info", {{"save", true}}));                       // no file to save to
    CHECK(refused("info", {{"detail", true}}));                     // wrong type
    CHECK(refused("decrypt", {{"output", f.host("x.xex")}, {"nope", true}}));
    CHECK(refused("nope", {}));
    const auto none = f.run("limits", "titled.xex", {{"all", false}, {"output", f.host("x.xex")}});
    CHECK(!none && none.status().message == "Choose at least one limit to remove");
    CHECK(!fs::exists(f.out / "x.xex"));

    // An output file the user chose is replaced (the save dialog asked); a
    // resource file in a folder is not.
    writeFile(f.out / "replace.xex", "old");
    CHECK(f.runTo("decrypt", "titled.xex", {}, "replace.xex"));
    MATCHES(f.out / "replace.xex", "set_info", "titled.xex");
    fs::create_directories(f.out / "res2");
    writeFile(f.out / "res2" / "4E4D07D0", "mine");
    CHECK(!f.run("resources", "titled.xex", {{"folder", f.host("res2")}}));
    CHECK(readFile(f.out / "res2" / "4E4D07D0") == "mine");
    auto none2 = f.run("resources", "patch.xexp", {{"folder", f.host("res2")}});
    CHECK(none2 && none2.value() == "The file contains no resources.\n");
}

void testCancel(Fixture& f) {
    // At the start, after reading, before writing: nothing is left behind.
    const ProgressFn never = [](const TransferProgress&) { return false; };
    auto r = f.run("decrypt", "encrypted.xex", {{"output", f.host("c1.xex")}}, never);
    CHECK(!r && r.status().cancelled);
    for (const char* step : {"processing", "writing"}) {
        int calls = 0;
        const ProgressFn stopAt = [&](const TransferProgress& p) {
            ++calls;
            return p.current.find(step) == std::string::npos;
        };
        r = f.run("decrypt", "encrypted.xex", {{"output", f.host("c2.xex")}}, stopAt);
        CHECK(!r && r.status().cancelled && calls > 2);
        r = f.run("decrypt", "encrypted.xex", {{"target", std::string{"source"}}}, stopAt);
        CHECK(!r && r.status().cancelled);
    }
    CHECK(!fs::exists(f.out / "c1.xex") && !fs::exists(f.out / "c2.xex"));
    MATCHES(f.src / "encrypted.xex", "encrypt", "encrypted.xex"); // the source is untouched
    const ProgressFn stopWriting = [](const TransferProgress& p) {
        return p.current.find("writing") == std::string::npos;
    };
    r = f.run("info", "titled.xex", {{"save", true}, {"output", f.host("c3.txt")}}, stopWriting);
    CHECK(!r && r.status().cancelled && !fs::exists(f.out / "c3.txt"));
    // Resources: checked before each one.
    fs::create_directories(f.out / "res3");
    const ProgressFn stopAtResource = [](const TransferProgress& p) {
        return p.current.find("4E4D07D0") == std::string::npos;
    };
    r = f.run("resources", "titled.xex", {{"folder", f.host("res3")}}, stopAtResource);
    CHECK(!r && r.status().cancelled && fs::is_empty(f.out / "res3"));
    // The whole run reports progress up to the file's size.
    std::uint64_t last = 0;
    r = f.run("decrypt", "encrypted.xex", {{"output", f.host("c4.xex")}}, [&](const TransferProgress& p) {
        last = p.bytesDone;
        return true;
    });
    CHECK(r && last == fs::file_size(f.src / "encrypted.xex"));
}

void testPlaces(Fixture& f) {
    // In place needs a writable place; a new file does not.
    ReadOnlyView view(*f.place);
    HostOperationIo viewIo(&view, "/titled.xex");
    auto r = runOperation(*f.xex, "decrypt", view, "/titled.xex", {{"target", std::string{"source"}}}, viewIo);
    CHECK(!r);
    r = runOperation(*f.xex, "encrypt", view, "/titled.xex", {{"output", f.host("from_view.xex")}}, viewIo);
    CHECK(r);
    MATCHES(f.out / "from_view.xex", "encrypt", "encrypted.xex");
    MATCHES(f.src / "titled.xex", "set_info", "titled.xex");

    // Inputs and outputs on another filesystem: the patch is read from it,
    // the results are written into it.
    const fs::path otherRoot = f.root / "other";
    fs::create_directories(otherRoot / "res");
    fs::copy_file(f.src / "patch.xexp", otherRoot / "update.xexp");
    LocalFileSystem other(otherRoot);
    FileSystemOperationIo io(other, f.place.get(), "/dev.xex");
    r = runOperation(*f.xex, "patch", *f.place, "/dev.xex",
                     {{"patch", std::string{"/update.xexp"}}, {"output", std::string{"/patched.xex"}}}, io);
    CHECK(r);
    MATCHES(otherRoot / "patched.xex", "patch", "patched.xex");
    FileSystemOperationIo titledIo(other, f.place.get(), "/titled.xex");
    r = runOperation(*f.xex, "resources", *f.place, "/titled.xex", {{"folder", std::string{"/res"}}}, titledIo);
    CHECK(r);
    MATCHES(otherRoot / "res" / "4E4D07D0", "resources", "res\\4E4D07D0");
    r = runOperation(*f.xex, "basefile", *f.place, "/titled.xex", {{"output", std::string{"/basefile.bin"}}},
                     titledIo);
    CHECK(r && r.value().find("Load address:    0x82000000") != std::string::npos);
    MATCHES(otherRoot / "basefile.bin", "basefile", "basefile.bin");
    // In place still rewrites the source, not a file on the other filesystem.
    fs::copy_file(f.src / "dev.xex", f.src / "dev2.xex");
    FileSystemOperationIo dev2Io(other, f.place.get(), "/dev2.xex");
    r = runOperation(*f.xex, "machine", *f.place, "/dev2.xex",
                     {{"machine", std::string{"retail"}}, {"target", std::string{"source"}}}, dev2Io);
    CHECK(r);
    MATCHES(f.src / "dev2.xex", "to_retail", "retail.xex");
    CHECK(!fs::exists(otherRoot / "dev2.xex"));
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "usage: xex_tests <golden samples dir> <expected.txt>\n";
        return 2;
    }
    const fs::path samples = argv[1];
    g_record = parseRecord(readFile(argv[2]));
    if (g_record.size() < 40) {
        std::cerr << "cannot read the golden record " << argv[2] << "\n";
        return 2;
    }

    Fixture f;
    f.root = fs::temp_directory_path() / ("unnamed_xex_tests-" + std::to_string(std::random_device{}()));
    f.src = f.root / "src";
    f.out = f.root / "out";
    fs::create_directories(f.src);
    fs::create_directories(f.out);
    for (const char* name : {"sample.xex", "sample.elf", "patch.xexp", "info.xml", "info_partial.xml",
                             "info_bad_hex.xml"})
        fs::copy_file(samples / name, f.src / name);
    f.place = std::make_unique<LocalFileSystem>(f.src);
    f.xex = FormatHandlerRegistry::withBuiltins().find("xex");
    CHECK(f.xex != nullptr);
    if (f.xex) {
        testDescriptors(*f.xex);
        testGolden(f);
        if (fs::exists(f.src / "dev.xex")) {
            testDescribe(*f.xex, f);
            testProbe(*f.xex, f);
            testParameters(f);
            testCancel(f);
            testPlaces(f);
        }
    }

    fs::remove_all(f.root);
    if (failures)
        std::cerr << failures << " check(s) failed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
