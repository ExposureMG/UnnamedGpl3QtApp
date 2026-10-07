// The XBDM backend against UpdClient's mock console
// (extern/UpdClient/tests/support/xbdm_mock_server.*), which serves an
// in-memory console over in-memory pipes and loopback TCP. Every test that
// talks to a console runs over both. Nothing here has seen a real console.
#include "core/FileSystemRegistry.hpp"
#include "core/Format.hpp"
#include "core/LocalFileSystem.hpp"
#include "core/PathUtil.hpp"
#include "core/Transfer.hpp"
#include "core/XbdmFileSystem.hpp"

#include "support/xbdm_mock_server.hpp"

#include <net/tcp_transport.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <thread>

namespace fs = std::filesystem;
namespace xbdm = updclient::xbdm;
using namespace std::chrono_literals;
using namespace unnamed::core;
using ut::Bytes;
using ut::XbdmFault;

static int failures = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n";             \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
#define CHECK_OK(expr)                                                                             \
    do {                                                                                           \
        const Status st_ = (expr);                                                                 \
        if (!st_) {                                                                                \
            std::cerr << __FILE__ << ":" << __LINE__ << ": " #expr " failed: " << st_.message << "\n"; \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
// Fails, with `text` in its message.
#define CHECK_FAILS(expr, text)                                                                    \
    do {                                                                                           \
        const Status st_ = (expr);                                                                 \
        if (st_ || st_.message.find(text) == std::string::npos) {                                  \
            std::cerr << __FILE__ << ":" << __LINE__ << ": " #expr " should fail with \"" << (text) \
                      << "\", got: " << (st_ ? "success" : st_.message) << "\n";                   \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

namespace {

fs::path scratch;

enum class Link { Memory, Tcp };

const char* linkName(Link link) { return link == Link::Memory ? "memory" : "tcp"; }

xbdm::ClientOptions quickOptions() {
    xbdm::ClientOptions o;
    o.greetingTimeout = 3000ms;
    o.idleTimeout = 3000ms;
    o.slowIdleTimeout = 3000ms;
    o.commandTimeout = 20000ms;
    o.byeTimeout = 1000ms;
    return o;
}

ut::XbdmMockOptions mockOptions() {
    ut::XbdmMockOptions o;
    o.maxUploadBytes = 256ull << 20;
    return o;
}

// A mock console and the place connected to it.
struct Rig {
    explicit Rig(Link l, ut::XbdmMockOptions options = mockOptions()) : mock(options), link(l) {
        if (link == Link::Tcp) {
            auto p = mock.listenTcp();
            if (!p) {
                std::cout << "SKIPPED (loopback TCP refused): " << updclient::formatError(p.error()) << "\n";
                skipped = true;
                return;
            }
            port = *p;
        }
    }

    xbdm::XbdmClient::Connector rawConnector() {
        if (link == Link::Memory)
            return mock.connector();
        const std::uint16_t p = port;
        return [p] { return updclient::net::TcpTransport::connect("127.0.0.1", p, 3000ms); };
    }

    // Over TCP as the GUI would connect (host and port); in memory through the
    // mock's connector.
    XbdmConnectOptions options() {
        XbdmConnectOptions o;
        o.client = quickOptions();
        if (link == Link::Memory) {
            o.connector = mock.connector();
        } else {
            o.host = "127.0.0.1";
            o.port = port;
        }
        return o;
    }

    std::unique_ptr<XbdmFileSystem> connect(XbdmConnectOptions o) {
        auto r = XbdmFileSystem::connect(std::move(o));
        if (!r) {
            std::cerr << "connect failed: " << r.status().message << "\n";
            ++failures;
            return nullptr;
        }
        return std::move(r.value());
    }
    std::unique_ptr<XbdmFileSystem> connect() { return connect(options()); }

    size_t commandsNamed(const std::string& name) const {
        size_t n = 0;
        for (const auto& c : mock.commands())
            n += c.name == name;
        return n;
    }

    ut::XbdmMockServer mock;
    Link link;
    std::uint16_t port = 0;
    bool skipped = false;
};

fs::path freshDir(const std::string& name) {
    const fs::path dir = scratch / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string consolePathOf(const std::string& drive, const std::string& relative) {
    std::string out = drive + ":\\" + relative;
    for (char& c : out) {
        if (c == '/')
            c = '\\';
    }
    return out;
}

// Names below a console folder that look like temporary uploads.
std::vector<std::string> partFilesBelow(const ut::XbdmMockServer& mock, const std::string& folder) {
    std::vector<std::string> out;
    for (const auto& name : mock.listNames(folder).value_or(std::vector<std::string>{})) {
        const std::string path = folder.back() == '\\' ? folder + name : folder + "\\" + name;
        if (name.size() > 5 && name.ends_with(".part"))
            out.push_back(path);
        const auto entry = mock.entry(path);
        if (entry && entry->directory) {
            const auto deeper = partFilesBelow(mock, path);
            out.insert(out.end(), deeper.begin(), deeper.end());
        }
    }
    return out;
}

// Host staging files a sink left in `dir` (".<name>.<random>.part").
int stagingFiles(const fs::path& dir) {
    int n = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        const std::string name = pathToUtf8(entry.path().filename());
        n += name.ends_with(".part");
    }
    return n;
}

int xbdmStagingFiles() {
    int n = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs::temp_directory_path(), ec))
        n += pathToUtf8(entry.path().filename()).starts_with("unnamed-xbdm-upload-");
    return n;
}

bool waitFor(const std::function<bool()>& condition, std::chrono::milliseconds limit = 5s) {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (!condition()) {
        if (std::chrono::steady_clock::now() > end)
            return false;
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

bool hasEntry(const std::vector<Entry>& entries, const std::string& name) {
    return std::any_of(entries.begin(), entries.end(), [&](const Entry& e) { return e.name == name; });
}

std::string property(const Details& d, const std::string& group, const std::string& label) {
    for (const auto& g : d.groups) {
        if (g.title != group)
            continue;
        for (const auto& item : g.items) {
            if (item.label == label)
                return item.value;
        }
    }
    return "<missing " + group + "/" + label + ">";
}

void writeBytes(const fs::path& path, const Bytes& data) {
    fs::create_directories(path.parent_path());
    ut::writeFile(path, data);
}

// --- tests -----------------------------------------------------------------------

void pathMapping() {
    const std::string names[] = {"a b.txt", " lead", "trail.", "trail ", "...", "x~y#z{}", "!#$%&'()-@[]^_`{}~",
                                 "UPPER lower", std::string(42, 'n'), "default.xex"};
    for (const auto& name : names) {
        const std::string virtualPath = "/HDD/dir/" + name;
        auto console = xbdmConsolePath(virtualPath);
        CHECK(console && console.value() == "HDD:\\dir\\" + name);
        if (!console)
            continue;
        auto back = xbdmVirtualPath(console.value());
        CHECK(back && back.value() == virtualPath);
        CHECK_OK(checkXboxName(name));
    }
    CHECK(xbdmConsolePath("/HDD").value() == "HDD:\\");
    CHECK(xbdmConsolePath("/HDD/").value() == "HDD:\\");
    CHECK(xbdmConsolePath("/DEVKIT/a/b").value() == "DEVKIT:\\a\\b");
    CHECK(xbdmVirtualPath("HDD:\\").value() == "/HDD");
    CHECK(xbdmVirtualPath("USB0:\\x").value() == "/USB0/x");

    const std::string refusedVirtual[] = {"/", "", "HDD/a", "/HDD//a", "/HDD/a\\b", "/HDD/a:b", "/HDD/..",
                                          "/HDD/.", "/C:/x", "/HDD/a*b", "/HDD/a\"b", "/HD D/x", "/HDD/\xc3\xa9",
                                          std::string("/HDD/a\0b", 8), "/HDD/a\nb", "/../HDD"};
    for (const auto& path : refusedVirtual)
        CHECK(!xbdmConsolePath(path));
    const std::string refusedConsole[] = {"HDD:/a", "\\Device\\Harddisk0\\x", "HDD:\\a\\\\b", "HDD", "HDD:\\..",
                                          "HDD:x"};
    for (const auto& path : refusedConsole)
        CHECK(!xbdmVirtualPath(path));

    CHECK_FAILS(checkXboxName(""), "not a valid name");
    CHECK_FAILS(checkXboxName(".."), "not a valid name");
    CHECK_FAILS(checkXboxName(std::string(43, 'n')), "at most 42");
    CHECK_FAILS(checkXboxName("a+b"), "not a valid Xbox name");
    CHECK_FAILS(checkXboxName("a/b"), "not a valid Xbox name");
    CHECK_FAILS(checkXboxName("a\\b"), "not a valid Xbox name");
    CHECK_FAILS(checkXboxName("a:b"), "not a valid Xbox name");
    CHECK_FAILS(checkXboxName("a\"b"), "not a valid Xbox name");
    CHECK_FAILS(checkXboxName("\xc3\xa9t\xc3\xa9"), "printable ASCII");
    CHECK_FAILS(checkXboxName(std::string("a\0b", 3)), "printable ASCII");
    CHECK_FAILS(checkXboxName("a\x01"), "\\x01");
}

void capabilityFlags(Link link) {
    Rig rig(link);
    if (rig.skipped)
        return;
    auto fs = rig.connect();
    if (!fs)
        return;
    const Capability caps = fs->capabilities();
    for (Capability c : {Capability::Browse, Capability::Inspect, Capability::Extract, Capability::Inject,
                         Capability::Replace, Capability::Remove, Capability::MakeDirectory, Capability::Rename,
                         Capability::Clear})
        CHECK(hasCapability(caps, c));
    for (Capability c : {Capability::Format, Capability::HealthCheck, Capability::Repair})
        CHECK(!hasCapability(caps, c));
    CHECK(fs->name() == "MockDevkit");
    CHECK(fs->consoleType() == "devkit");
    CHECK(fs->connectionState() == XbdmConnectionState::Connected);
    CHECK(fs->connectionError().empty());

    XbdmConnectOptions named = rig.options();
    named.displayName = "Test kit";
    auto other = rig.connect(named);
    CHECK(other && other->name() == "Test kit");
}

void browseAndDescribe(Link link) {
    Rig rig(link);
    if (rig.skipped)
        return;
    auto fs = rig.connect();
    if (!fs)
        return;
    std::vector<Entry> root;
    CHECK_OK(fs->list("/", root));
    for (const char* drive : {"HDD", "DEVKIT", "FLASH", "USB0"})
        CHECK(hasEntry(root, drive));
    CHECK(!hasEntry(root, "USB1"));
    for (const auto& e : root)
        CHECK(e.type == EntryType::Directory);

    std::vector<Entry> hdd;
    CHECK_OK(fs->list("/HDD", hdd));
    CHECK(hasEntry(hdd, "Content") && hasEntry(hdd, "default.xex") && hasEntry(hdd, "Games"));
    for (const auto& e : hdd) {
        if (e.name == "default.xex") {
            CHECK(e.type == EntryType::File && e.size == 20000 && e.kind == "xex");
            CHECK(e.modified == 1710000000);
        }
        if (e.name == "Content")
            CHECK(e.type == EntryType::Directory);
    }

    Entry entry;
    CHECK_OK(fs->stat("/HDD/default.xex", entry));
    CHECK(entry.size == 20000 && entry.type == EntryType::File && entry.modified == 1710000000);
    CHECK_OK(fs->stat("/HDD", entry));
    CHECK(entry.type == EntryType::Directory && entry.name == "HDD");
    CHECK_OK(fs->stat("/hdd/Content", entry));
    CHECK(entry.type == EntryType::Directory);
    CHECK_FAILS(fs->stat("/USB1", entry), "not mounted");
    CHECK_FAILS(fs->stat("/HDD/nothing.bin", entry), "No such file or folder");

    Details d;
    CHECK_OK(fs->describe("/HDD/default.xex", d));
    CHECK(property(d, "General", "Size") == "19.53 KiB (20000 bytes)");
    CHECK(property(d, "General", "Console path") == "HDD:\\default.xex");
    CHECK(property(d, "General", "Modified") == "2024-03-09 16:00 UTC");
    CHECK(property(d, "General", "Created") == "2023-11-14 22:13 UTC");
    CHECK(property(d, "XBDM entry", "Changed (FILETIME)") == "0x01da723ad6e74000");
    CHECK(property(d, "XBDM entry", "Attributes") == "None");
    CHECK_OK(fs->describe("/HDD/Attrs/ro.txt", d));
    CHECK(property(d, "XBDM entry", "Attributes") == "Read-only");
    CHECK_OK(fs->describe("/HDD/Attrs/hidden.txt", d));
    CHECK(property(d, "XBDM entry", "Attributes") == "Hidden");
    CHECK_OK(fs->describe("/HDD/Content", d));
    CHECK(d.kind == "folder" && property(d, "General", "Items") == "1");
    CHECK_OK(fs->describe("/HDD", d));
    CHECK(property(d, "Capacity", "Total") == humanSize(250000000000ull));
    CHECK(property(d, "Capacity", "Free") == humanSize(100000000000ull));
    CHECK_OK(fs->describe("/", d));
    CHECK(property(d, "General", "Drives") == "4");

    // a console that sends no size: unknown, not 0
    rig.mock.inject(XbdmFault::statusLine("200- createhi=0x01d11fb5 createlo=0x59683c00").on("getfileattributes"));
    CHECK_OK(fs->describe("/HDD/default.xex", d));
    CHECK(property(d, "General", "Size") == "Unknown (the console did not send it)");
    CHECK(property(d, "XBDM entry", "Changed (FILETIME)") == "Not sent");

    CHECK_OK(fs->describeFileSystem(d));
    CHECK(d.title == "MockDevkit" && d.notice.empty());
    CHECK(property(d, "Console", "Name") == "MockDevkit");
    CHECK(property(d, "Console", "Type") == "devkit");
    CHECK(property(d, "Console", "Console ID") == "0123456789ab");
    CHECK(property(d, "Console", "Execution state") == "start");
    CHECK(property(d, "Console", "Running title") == "\\Device\\Harddisk0\\Partition1\\DEVKIT\\Mock\\default.xex");
    CHECK(property(d, "Console", "Title address").find("0xc0a80102") != std::string::npos);
    CHECK(property(d, "Drives", "HDD:") == humanSize(100000000000ull) + " free of " + humanSize(250000000000ull));
    CHECK(property(d, "Drives", "FLASH:") == humanSize(4ull << 20) + " free of " + humanSize(16ull << 20));
    CHECK(property(d, "Connection", "State") == "Connected");
    CHECK(property(d, "Supported operations", "Delete") == "Yes");
}

void copyMatrix(Link link) {
    Rig rig(link);
    if (rig.skipped)
        return;
    auto console = rig.connect();
    if (!console)
        return;
    const fs::path dir = freshDir(std::string("matrix-") + linkName(link));

    std::map<std::string, Bytes> files;
    auto add = [&](const std::string& relative, Bytes data) {
        writeBytes(dir / "src" / pathFromUtf8(relative), data);
        files[relative] = std::move(data);
    };
    add("tree/a.bin", ut::patternBytes(100 * 1024, 1));
    add("tree/empty.bin", {});
    add("tree/sub/deeper/c.txt", ut::bytesOf("deep"));
    for (const std::string& odd : std::vector<std::string>{"a b.txt", " lead", "trail.", "...", "x~y#z{}!",
                                                         std::string(42, 'n'), "UP lo"})
        add("tree/odd/" + odd, ut::bytesOf("odd name: " + odd));
    for (int i = 0; i < 300; ++i) {
        char name[32];
        std::snprintf(name, sizeof name, "tree/many/f%03d", i);
        add(name, ut::patternBytes(static_cast<size_t>(i) * 7, static_cast<uint32_t>(i)));
    }
    fs::create_directories(dir / "src/tree/emptydir");
    LocalFileSystem src(dir / "src");

    // host -> console
    CHECK_OK(copyTree(src, "/tree", *console, "/HDD/tree"));
    for (const auto& [relative, data] : files)
        CHECK(rig.mock.fileData(consolePathOf("HDD", relative)) == data);
    const auto emptyDir = rig.mock.entry("HDD:\\tree\\emptydir");
    CHECK(emptyDir && emptyDir->directory);
    CHECK(rig.mock.listNames("HDD:\\tree\\many").value_or(std::vector<std::string>{}).size() == 300);
    CHECK(partFilesBelow(rig.mock, "HDD:\\").empty());

    // console -> host
    fs::create_directories(dir / "back");
    LocalFileSystem back(dir / "back");
    CHECK_OK(copyTree(*console, "/HDD/tree", back, "/tree"));
    for (const auto& [relative, data] : files)
        CHECK(ut::readFile(dir / "back" / pathFromUtf8(relative)) == data);
    CHECK(fs::is_directory(dir / "back/tree/emptydir"));
    CHECK(stagingFiles(dir / "back") == 0);

    // into an existing folder, merged; an existing file needs overwrite
    CHECK_OK(copyTree(src, "/tree/a.bin", *console, "/HDD/tree/sub/a.bin"));
    CHECK_FAILS(copyTree(src, "/tree/a.bin", *console, "/HDD/tree/sub/a.bin"), "Already exists");
    writeBytes(dir / "src/new-a.bin", ut::patternBytes(5000, 99));
    TransferOptions overwrite;
    overwrite.overwrite = true;
    CHECK_OK(copyTree(src, "/new-a.bin", *console, "/HDD/tree/sub/a.bin", overwrite));
    CHECK(rig.mock.fileData("HDD:\\tree\\sub\\a.bin") == ut::patternBytes(5000, 99));
    CHECK(partFilesBelow(rig.mock, "HDD:\\").empty());

    // 64 MiB each way
    const Bytes big = ut::patternBytes(64u << 20, 77);
    writeBytes(dir / "src/big.bin", big);
    CHECK_OK(copyTree(src, "/big.bin", *console, "/USB0/big.bin"));
    CHECK(rig.mock.fileSize("USB0:\\big.bin") == big.size());
    CHECK(rig.mock.fileData("USB0:\\big.bin") == big);
    CHECK_OK(copyTree(*console, "/USB0/big.bin", back, "/big.bin"));
    CHECK(ut::readFile(dir / "back/big.bin") == big);
    fs::remove(dir / "src/big.bin");
    fs::remove(dir / "back/big.bin");
    CHECK_OK(console->remove("/USB0/big.bin"));

    // console -> console: the reader keeps the command connection, the writer
    // opens a second one
    CHECK_OK(copyTree(*console, "/HDD/default.xex", *console, "/DEVKIT/copy.xex"));
    CHECK(rig.mock.fileData("DEVKIT:\\copy.xex") == rig.mock.fileData("HDD:\\default.xex"));
    CHECK_OK(copyTree(*console, "/HDD/tree/odd", *console, "/DEVKIT/odd"));
    CHECK(rig.mock.listNames("DEVKIT:\\odd").value_or(std::vector<std::string>{}).size() == 7);

    // a host name the console cannot store is refused before any byte is sent
    writeBytes(dir / "src/bad+name.txt", ut::bytesOf("x"));
    const size_t sends = rig.commandsNamed("sendfile");
    CHECK_FAILS(copyTree(src, "/bad+name.txt", *console, "/HDD/bad+name.txt"), "not a valid Xbox name");
    CHECK(rig.commandsNamed("sendfile") == sends);

    for (const auto& record : rig.mock.commands())
        CHECK(!record.pipelined && !record.overLong);
}

void unknownSizeWrites(Link link) {
    Rig rig(link);
    if (rig.skipped)
        return;
    auto console = rig.connect();
    if (!console)
        return;
    const int stagingBefore = xbdmStagingFiles();

    // in memory
    {
        auto sink = console->openWrite("/HDD/small.bin", std::nullopt, false);
        CHECK_OK(sink.status());
        const Bytes data = ut::patternBytes(1000, 5);
        CHECK_OK(sink.value()->write(data.data(), data.size()));
        CHECK(!rig.mock.entry("HDD:\\small.bin"));
        CHECK_OK(sink.value()->finish());
        CHECK(rig.mock.fileData("HDD:\\small.bin") == data);
    }
    // beyond 16 MiB: staged in a host file
    {
        auto sink = console->openWrite("/HDD/large.bin", std::nullopt, false);
        CHECK_OK(sink.status());
        const Bytes data = ut::patternBytes(20u << 20, 6);
        for (size_t at = 0; at < data.size(); at += 1u << 20)
            CHECK_OK(sink.value()->write(data.data() + at, std::min<size_t>(1u << 20, data.size() - at)));
        CHECK(xbdmStagingFiles() == stagingBefore + 1);
        CHECK_OK(sink.value()->finish());
        CHECK(rig.mock.fileData("HDD:\\large.bin") == data);
    }
    CHECK(xbdmStagingFiles() == stagingBefore);
    // dropped before finish: nothing sent, nothing left
    {
        const size_t sends = rig.commandsNamed("sendfile");
        auto sink = console->openWrite("/HDD/dropped.bin", std::nullopt, false);
        CHECK_OK(sink.status());
        CHECK_OK(sink.value()->write("abc", 3));
        sink.value().reset();
        CHECK(rig.commandsNamed("sendfile") == sends);
        CHECK(!rig.mock.entry("HDD:\\dropped.bin"));
    }
    // a name that appeared meanwhile is not replaced
    {
        auto sink = console->openWrite("/HDD/race.bin", std::nullopt, false);
        CHECK_OK(sink.status());
        CHECK_OK(sink.value()->write("new", 3));
        CHECK(static_cast<bool>(rig.mock.addFile("HDD:\\race.bin", ut::bytesOf("theirs"))));
        CHECK_FAILS(sink.value()->finish(), "Already exists");
        CHECK(rig.mock.fileData("HDD:\\race.bin") == ut::bytesOf("theirs"));
    }
    // more than the console can take
    {
        XbdmConnectOptions o = rig.options();
        o.client.maxUploadBytes = 1u << 20;
        auto small = rig.connect(o);
        if (small) {
            auto sink = small->openWrite("/HDD/toolarge.bin", std::nullopt, false);
            CHECK_OK(sink.status());
            const Bytes data = ut::patternBytes((1u << 20) + 1, 7);
            CHECK_FAILS(sink.value()->write(data.data(), data.size()), "the largest file XBDM can send");
            CHECK_FAILS(small->openWrite("/HDD/toolarge.bin", data.size(), false).status(), "at most");
        }
    }
    CHECK(xbdmStagingFiles() == stagingBefore);
    CHECK(partFilesBelow(rig.mock, "HDD:\\").empty());
}

void cancelLeavesNothing(Link link) {
    Rig rig(link);
    if (rig.skipped)
        return;
    XbdmConnectOptions o = rig.options();
    o.client.idleTimeout = 10s; // a cancel must not wait for it
    auto console = rig.connect(o);
    if (!console)
        return;
    const fs::path dir = freshDir(std::string("cancel-") + linkName(link));
    LocalFileSystem host(dir);
    writeBytes(dir / "up.bin", ut::patternBytes(8u << 20, 3));
    CHECK(static_cast<bool>(rig.mock.addFile("HDD:\\down.bin", ut::patternBytes(8u << 20, 4))));

    TransferOptions stopEarly;
    stopEarly.progress = [](const TransferProgress& p) { return p.bytesDone < (2u << 20); };

    // from the progress callback, both ways
    Status st = copyTree(host, "/up.bin", *console, "/HDD/up.bin", stopEarly);
    CHECK(!st && st.cancelled);
    CHECK(!rig.mock.entry("HDD:\\up.bin"));
    CHECK(partFilesBelow(rig.mock, "HDD:\\").empty());
    CHECK(console->leftoverFiles().empty());

    st = copyTree(*console, "/HDD/down.bin", host, "/down.bin", stopEarly);
    CHECK(!st && st.cancelled);
    CHECK(!fs::exists(dir / "down.bin"));
    CHECK(stagingFiles(dir) == 0);

    // cancel() from another thread while the console stalls a download
    rig.mock.inject(XbdmFault::stall(1000).on("getfile"));
    const size_t gets = rig.commandsNamed("getfile");
    auto start = std::chrono::steady_clock::now();
    std::thread download([&] { st = copyTree(*console, "/HDD/down.bin", host, "/down.bin"); });
    CHECK(waitFor([&] { return rig.commandsNamed("getfile") > gets; }));
    std::this_thread::sleep_for(100ms);
    console->cancel();
    download.join();
    CHECK(!st && st.cancelled);
    CHECK(std::chrono::steady_clock::now() - start < 5s);
    CHECK(!fs::exists(dir / "down.bin"));
    CHECK(stagingFiles(dir) == 0);

    // and while the console stops reading an upload
    rig.mock.inject(XbdmFault::stallUploadAfterBytes(64 * 1024).on("sendfile"));
    writeBytes(dir / "up32.bin", ut::patternBytes(32u << 20, 5));
    const size_t sends = rig.commandsNamed("sendfile");
    start = std::chrono::steady_clock::now();
    std::thread upload([&] { st = copyTree(host, "/up32.bin", *console, "/HDD/up32.bin"); });
    CHECK(waitFor([&] { return rig.commandsNamed("sendfile") > sends; }));
    std::this_thread::sleep_for(200ms);
    console->cancel();
    upload.join();
    CHECK(!st && st.cancelled);
    CHECK(std::chrono::steady_clock::now() - start < 8s);
    CHECK(!rig.mock.entry("HDD:\\up32.bin"));
    CHECK(partFilesBelow(rig.mock, "HDD:\\").empty());
    CHECK(console->leftoverFiles().empty());

    // the place is usable afterwards
    std::vector<Entry> entries;
    CHECK_OK(console->list("/HDD", entries));
    CHECK(console->connectionState() == XbdmConnectionState::Connected);
}

void dropMidUpload(Link link) {
    Rig rig(link);
    if (rig.skipped)
        return;
    auto console = rig.connect();
    if (!console)
        return;
    const fs::path dir = freshDir(std::string("drop-") + linkName(link));
    LocalFileSystem host(dir);
    writeBytes(dir / "up.bin", ut::patternBytes(4u << 20, 8));

    // in memory the client's next write fails; over TCP the mock's half-closed
    // socket stops taking data, and the write times out, or, when the
    // system reports the closed socket first (broken pipe), fails as lost
    rig.mock.inject(XbdmFault::dropUploadAfterBytes(100000).on("sendfile"));
    const Status st = copyTree(host, "/up.bin", *console, "/HDD/Content/up.bin");
    CHECK(!st && !st.cancelled);
    CHECK(st.message.find("was lost") != std::string::npos ||
          (link == Link::Tcp && st.message.find("stopped responding") != std::string::npos));
    CHECK(!rig.mock.entry("HDD:\\Content\\up.bin"));
    CHECK(partFilesBelow(rig.mock, "HDD:\\").empty());
    CHECK(console->leftoverFiles().empty());

    // a drop after the last byte, while the console's answer is awaited
    // (dropped after the 23 bytes of "204- send binary data")
    rig.mock.inject(XbdmFault::dropAfterBytes(23).on("sendfile"));
    CHECK_FAILS(copyTree(host, "/up.bin", *console, "/HDD/Content/up2.bin"), "connection");
    CHECK(!rig.mock.entry("HDD:\\Content\\up2.bin"));
    CHECK(partFilesBelow(rig.mock, "HDD:\\").empty());
    CHECK(console->leftoverFiles().empty());
    CHECK_OK(copyTree(host, "/up.bin", *console, "/HDD/Content/up.bin"));
    CHECK(rig.mock.fileData("HDD:\\Content\\up.bin") == ut::patternBytes(4u << 20, 8));
}

void replaceKeepsTheOnlyCopy(Link link) {
    Rig rig(link);
    if (rig.skipped)
        return;
    auto console = rig.connect();
    if (!console)
        return;
    const fs::path dir = freshDir(std::string("kept-") + linkName(link));
    LocalFileSystem host(dir);
    const Bytes data = ut::patternBytes(3000, 11);
    writeBytes(dir / "new.xex", data);

    // the old file is deleted but the answer to the delete is lost: the
    // upload may be the only copy left
    rig.mock.inject(XbdmFault::dropAfterBytes(0).on("delete"));
    TransferOptions overwrite;
    overwrite.overwrite = true;
    const Status st = copyTree(host, "/new.xex", *console, "/HDD/default.xex", overwrite);
    CHECK(!st);
    CHECK(st.message.find("kept on the console as /HDD/default.xex.") != std::string::npos);
    const auto left = console->leftoverFiles();
    CHECK(left.size() == 1);
    if (left.size() == 1) {
        CHECK(left[0].starts_with("HDD:\\default.xex.") && left[0].ends_with(".part"));
        CHECK(rig.mock.fileData(left[0]) == data);
    }
    Details d;
    CHECK_OK(console->describeFileSystem(d));
    CHECK(d.notice.find("/HDD/default.xex.") != std::string::npos);
    // never deleted, not even by a reconnect
    CHECK_OK(console->reconnect());
    CHECK(console->leftoverFiles().size() == 1);
    CHECK(partFilesBelow(rig.mock, "HDD:\\").size() == 1);
}

void errorMapping(Link link) {
    Rig rig(link);
    if (rig.skipped)
        return;
    auto console = rig.connect();
    if (!console)
        return;
    std::vector<Entry> entries;
    Entry entry;

    // not found
    CHECK_FAILS(console->list("/HDD/nope", entries), "not found");
    CHECK_FAILS(console->openRead("/HDD/nope.bin").status(), "No such file");
    CHECK_FAILS(console->remove("/HDD/nope.bin"), "No such file or folder");
    CHECK_FAILS(console->rename("/HDD/nope.bin", "x.bin"), "not found");
    // exists
    CHECK_FAILS(console->makeDirectory("/HDD/Content"), "Already exists");
    CHECK_FAILS(console->openWrite("/HDD/default.xex", 5, false).status(), "Already exists");
    CHECK_FAILS(console->openWrite("/HDD/Content", 5, true).status(), "a folder is in the way");
    CHECK_FAILS(console->rename("/HDD/default.xex", "Content"), "Already exists");
    // access denied
    CHECK_FAILS(console->list("/HDD/Protected", entries), "access denied");
    CHECK_FAILS(console->openRead("/HDD/Protected/secret.bin").status(), "access denied");
    CHECK_FAILS(console->makeDirectory("/FLASH/new"), "access denied");
    CHECK_FAILS(console->openWrite("/FLASH/new.bin", 5, false).status(), "access denied");
    // drive not mounted
    CHECK_FAILS(console->list("/USB1", entries), "not mounted");
    CHECK_FAILS(console->list("/USB1/x", entries), "not mounted");
    CHECK_FAILS(console->openWrite("/USB1/a.bin", 5, false).status(), "not mounted");
    CHECK_FAILS(console->makeDirectory("/USB1/x"), "not mounted");
    CHECK_FAILS(console->openRead("/NODRIVE/a.bin").status(), "not mounted");
    // no space: seen by the free-space check before anything is sent, and as
    // the console's refusal
    CHECK(static_cast<bool>(rig.mock.addDrive({"TINY", 1u << 20, 1000})));
    const size_t sends = rig.commandsNamed("sendfile");
    CHECK_FAILS(console->openWrite("/TINY/a.bin", 5000, false).status(), "not enough free space on TINY:");
    CHECK(rig.commandsNamed("sendfile") == sends);
    auto mockOpts = rig.mock.options();
    mockOpts.maxUploadBytes = 4096;
    rig.mock.setOptions(mockOpts);
    CHECK_FAILS(console->openWrite("/HDD/b.bin", 5000, false).status(), "not enough free space on the drive");
    CHECK(!rig.mock.entry("HDD:\\b.bin"));
    mockOpts.maxUploadBytes = 256ull << 20;
    rig.mock.setOptions(mockOpts);
    // missing parent folder
    CHECK_FAILS(console->openWrite("/HDD/nofolder/a.bin", 5, false).status(), "cannot create it");
    CHECK_FAILS(console->makeDirectory("/HDD/nofolder/a"), "Cannot create the folder");
    // names and paths refused before anything is sent
    const size_t before = rig.mock.commands().size();
    CHECK_FAILS(console->makeDirectory("/HDD/a+b"), "not a valid Xbox name");
    CHECK_FAILS(console->makeDirectory("/HDD/" + std::string(43, 'n')), "at most 42");
    CHECK_FAILS(console->openWrite("/HDD/a\"b", 1, false).status(), "Invalid path");
    CHECK_FAILS(console->openWrite("/HDD/x?.bin", 1, false).status(), "Invalid path");
    CHECK_FAILS(console->rename("/HDD/default.xex", "a/b"), "not a valid Xbox name");
    CHECK_FAILS(console->rename("/HDD/default.xex", ".."), "not a valid name");
    CHECK_FAILS(console->list("/HDD/a\\b", entries), "Invalid path");
    CHECK_FAILS(console->stat("/HDD/../FLASH", entry), "Invalid path");
    CHECK_FAILS(console->remove("/HDD"), "Cannot delete a drive");
    CHECK_FAILS(console->remove("/"), "root");
    CHECK_FAILS(console->clear("/HDD"), "whole drive");
    CHECK_FAILS(console->clear("/"), "root");
    CHECK_FAILS(console->makeDirectory("/NEWDRIVE"), "Cannot create a drive");
    CHECK_FAILS(console->rename("/HDD", "X"), "Cannot rename a drive");
    // 4 GiB: XBDM's lengths have 32 bits
    CHECK_FAILS(console->openWrite("/HDD/huge.bin", 4ull << 30, false).status(), "4 GiB");
    CHECK(rig.mock.commands().size() == before);
    CHECK(static_cast<bool>(rig.mock.addVirtualFile("HDD:\\huge.bin", (4ull << 30) + 1, 3)));
    CHECK_FAILS(console->openRead("/HDD/huge.bin").status(), "4 GiB or more");
    CHECK(rig.commandsNamed("getfile") == 0);
    // a length the console announces beyond the listed size
    rig.mock.inject(XbdmFault::claimLength(1u << 30).on("getfile"));
    const fs::path dir = freshDir(std::string("errors-") + linkName(link));
    LocalFileSystem host(dir);
    CHECK_FAILS(copyTree(*console, "/HDD/default.xex", host, "/default.xex"), "Cannot read /HDD/default.xex");
    CHECK(!fs::exists(dir / "default.xex"));
    CHECK(stagingFiles(dir) == 0);
    CHECK_OK(copyTree(*console, "/HDD/default.xex", host, "/default.xex"));
    CHECK(console->connectionState() == XbdmConnectionState::Connected);
}

void connectionLimits(Link link) {
    ut::XbdmMockOptions limited = mockOptions();
    limited.connectionLimit = 1;
    Rig rig(link, limited);
    if (rig.skipped)
        return;
    auto console = rig.connect();
    if (!console)
        return;
    std::vector<Entry> entries;
    {
        auto source = console->openRead("/HDD/default.xex");
        CHECK_OK(source.status());
        CHECK_FAILS(console->list("/HDD", entries), "connection limit is reached");
        CHECK(console->connectionState() == XbdmConnectionState::Connected);
        Bytes buffer(30000);
        auto n = source.value()->read(buffer.data(), buffer.size());
        CHECK(n && n.value() == 20000);
    }
    CHECK_OK(console->list("/HDD", entries));

    // the place's own limit
    Rig second(link);
    if (second.skipped)
        return;
    XbdmConnectOptions o = second.options();
    o.maxConnections = 1;
    auto single = second.connect(o);
    if (!single)
        return;
    auto source = single->openRead("/HDD/default.xex");
    CHECK_OK(source.status());
    CHECK_FAILS(single->list("/HDD", entries), "connections this place may open");
    CHECK_FAILS(single->reconnect(), "a transfer is using the connection");
    source.value().reset();
    CHECK_OK(single->list("/HDD", entries));
}

void listingDuringTransfer(Link link) {
    Rig rig(link);
    if (rig.skipped)
        return;
    auto console = rig.connect();
    if (!console)
        return;
    const fs::path dir = freshDir(std::string("parallel-") + linkName(link));
    LocalFileSystem host(dir);

    rig.mock.inject(XbdmFault::stall(100, 1500ms).on("getfile"));
    Status st;
    std::thread transfer([&] { st = copyTree(*console, "/HDD/default.xex", host, "/default.xex"); });
    CHECK(waitFor([&] { return rig.commandsNamed("getfile") > 0; }));
    std::vector<Entry> entries;
    CHECK_OK(console->list("/HDD/Content", entries));
    Details d;
    CHECK_OK(console->describe("/HDD/Games", d));
    transfer.join();
    CHECK_OK(st);
    CHECK(ut::readFile(dir / "default.xex") == rig.mock.fileData("HDD:\\default.xex"));

    size_t getConnection = 0;
    std::vector<size_t> listConnections;
    bool afterGet = false;
    for (const auto& record : rig.mock.commands()) {
        if (record.name == "getfile") {
            getConnection = record.connection;
            afterGet = true;
        } else if (afterGet && record.name == "dirlist") {
            listConnections.push_back(record.connection);
        }
    }
    CHECK(!listConnections.empty());
    for (size_t c : listConnections)
        CHECK(c != getConnection);
    // the extra connections were closed again
    CHECK(rig.mock.waitForActiveConnections(1));
    CHECK(console->openConnections() == 1);
}

void reconnects(Link link) {
    Rig rig(link);
    if (rig.skipped)
        return;
    auto reachable = std::make_shared<std::atomic_bool>(true);
    auto attempts = std::make_shared<std::atomic_int>(0);
    auto states = std::make_shared<std::vector<XbdmConnectionState>>();
    auto statesMutex = std::make_shared<std::mutex>();
    XbdmConnectOptions o = rig.options();
    o.client.idleTimeout = 300ms;
    o.client.commandTimeout = 1000ms;
    o.connector = [base = rig.rawConnector(), reachable, attempts]() -> updclient::Result<updclient::net::TransportPtr> {
        ++*attempts;
        if (!*reachable)
            return updclient::fail(updclient::ErrorCode::ConnectFailed, "connection refused");
        return base();
    };
    o.onStateChanged = [states, statesMutex](XbdmConnectionState s) {
        std::lock_guard<std::mutex> lock(*statesMutex);
        states->push_back(s);
    };
    auto console = rig.connect(o);
    if (!console)
        return;
    std::vector<Entry> entries;

    // dropped while idle: opened again on the next command, transparently
    rig.mock.dropAllConnections();
    CHECK(rig.mock.waitForActiveConnections(0));
    CHECK_OK(console->list("/HDD", entries));
    rig.mock.dropAllConnections();
    CHECK(rig.mock.waitForActiveConnections(0));
    CHECK_OK(console->makeDirectory("/HDD/afterdrop"));
    CHECK(rig.mock.entry("HDD:\\afterdrop").has_value());
    CHECK(console->connectionState() == XbdmConnectionState::Connected);

    // unreachable: disconnected, and calls fail at once until reconnect()
    *reachable = false;
    rig.mock.dropAllConnections();
    CHECK(rig.mock.waitForActiveConnections(0));
    CHECK_FAILS(console->list("/HDD", entries), "cannot reach");
    CHECK(console->connectionState() == XbdmConnectionState::Disconnected);
    CHECK(console->connectionError().find("cannot reach") != std::string::npos);
    const int tried = *attempts;
    CHECK_FAILS(console->list("/HDD", entries), "Reconnect to continue");
    CHECK_FAILS(console->makeDirectory("/HDD/x"), "Reconnect to continue");
    CHECK(*attempts == tried);
    Details d;
    CHECK_OK(console->describeFileSystem(d));
    CHECK(d.notice.find("Not connected") != std::string::npos);
    CHECK_FAILS(console->reconnect(), "cannot reach");
    *reachable = true;
    CHECK_OK(console->reconnect());
    CHECK(console->connectionState() == XbdmConnectionState::Connected);
    CHECK_OK(console->list("/HDD", entries));

    // a console that stops answering: disconnected after one timeout
    rig.mock.inject(XbdmFault::silence().on("dirlist"));
    CHECK_FAILS(console->list("/HDD", entries), "stopped responding");
    CHECK(console->connectionState() == XbdmConnectionState::Disconnected);
    const auto start = std::chrono::steady_clock::now();
    CHECK_FAILS(console->list("/HDD", entries), "Reconnect to continue");
    CHECK(std::chrono::steady_clock::now() - start < 200ms);
    rig.mock.clearFaults();
    CHECK_OK(console->reconnect());
    CHECK_OK(console->list("/HDD", entries));
    {
        std::lock_guard<std::mutex> lock(*statesMutex);
        CHECK((*states == std::vector<XbdmConnectionState>{XbdmConnectionState::Disconnected,
                                                            XbdmConnectionState::Connected,
                                                            XbdmConnectionState::Disconnected,
                                                            XbdmConnectionState::Connected}));
    }

    // the raw client for other tools is a connection of its own
    auto raw = console->openClient();
    CHECK_OK(raw.status());
    if (raw) {
        auto name = raw.value()->debugName();
        CHECK(name && *name == "MockDevkit");
        auto memory = raw.value()->getMemory(0x82000000u, 16);
        CHECK(memory && memory->readableBytes() == 16);
    }
}

void hostileNames(Link link) {
    Rig rig(link);
    if (rig.skipped)
        return;
    auto console = rig.connect();
    if (!console)
        return;
    CHECK(static_cast<bool>(rig.mock.addFile("HDD:\\Hostile\\ok.txt", ut::bytesOf("fine"))));
    CHECK(static_cast<bool>(rig.mock.addFile("HDD:\\Hostile\\...", ut::bytesOf("dots"))));

    Bytes body = ut::bytesOf("202- multiline response follows\r\n");
    const std::string hostile[] = {"..", ".", "..\\..\\evil", "C:\\evil", "HDD:", "/etc/passwd", "a\\b",
                                   "x/y", "..\\", "\\\\server\\share", "a*b", "\x7f"};
    for (const auto& name : hostile)
        ut::append(body, "name=\"" + name + "\" sizehi=0x0 sizelo=0x4\r\n");
    static const char nulLine[] = "name=\"nul\0inside\" sizehi=0x0 sizelo=0x4\r\n";
    ut::append(body, std::string_view(nulLine, sizeof nulLine - 1));
    ut::append(body, std::string_view("name=\"ok.txt\" sizehi=0x0 sizelo=0x4\r\n"));
    ut::append(body, std::string_view("name=\"...\" sizehi=0x0 sizelo=0x4\r\n"));
    ut::append(body, std::string_view(".\r\n"));
    rig.mock.inject(XbdmFault::reply(body).on("dirlist").always());

    std::vector<Entry> entries;
    CHECK_OK(console->list("/HDD/Hostile", entries));
    CHECK(entries.size() == 2 && hasEntry(entries, "ok.txt") && hasEntry(entries, "..."));

    const fs::path dir = freshDir(std::string("hostile-") + linkName(link));
    fs::create_directories(dir / "target");
    LocalFileSystem host(dir / "target");
    CHECK_OK(copyTree(*console, "/HDD/Hostile", host, "/out"));
    std::vector<std::string> written;
    for (const auto& e : fs::recursive_directory_iterator(dir))
        written.push_back(pathToUtf8(fs::relative(e.path(), dir)));
    std::sort(written.begin(), written.end());
    CHECK((written == std::vector<std::string>{"target", "target/out", "target/out/...", "target/out/ok.txt"}));
    CHECK(ut::readFile(dir / "target/out/ok.txt") == ut::bytesOf("fine"));

    Details d;
    CHECK_OK(console->describe("/HDD/Hostile", d));
    CHECK(property(d, "General", "Not shown").starts_with("11 "));
    // nothing of a folder with entries that cannot be named is deleted
    CHECK_FAILS(console->remove("/HDD/Hostile"), "nothing was deleted");
    CHECK(rig.mock.entry("HDD:\\Hostile\\ok.txt").has_value());
    rig.mock.clearFaults();

    rig.mock.inject(XbdmFault::reply(std::string_view("202- multiline response follows\r\n"
                                                      "drivename=\"HDD\"\r\ndrivename=\"..\"\r\n"
                                                      "drivename=\"C:\\\"\r\ndrivename=\"HDD\\..\"\r\n"
                                                      "drivename=\"a/b\"\r\ndrivename=\"\"\r\n.\r\n"))
                        .on("drivelist"));
    CHECK_OK(console->list("/", entries));
    CHECK(entries.size() == 1 && entries[0].name == "HDD");
}

void removeAndRename(Link link) {
    Rig rig(link);
    if (rig.skipped)
        return;
    auto console = rig.connect();
    if (!console)
        return;
    for (const char* path : {"HDD:\\Tree\\a.bin", "HDD:\\Tree\\sub\\b.bin", "HDD:\\Tree\\sub\\deep\\c.bin"})
        CHECK(static_cast<bool>(rig.mock.addFile(path, ut::bytesOf("x"))));
    CHECK(static_cast<bool>(rig.mock.addDirectory("HDD:\\Tree\\empty")));
    CHECK_OK(console->remove("/HDD/Tree"));
    CHECK(!rig.mock.entry("HDD:\\Tree"));

    CHECK_OK(console->remove("/HDD/default.xex"));
    CHECK(!rig.mock.entry("HDD:\\default.xex"));

    // too deep: refused before anything is deleted
    std::string deep = "HDD:\\Deep";
    for (int i = 0; i < 33; ++i)
        deep += "\\d";
    CHECK(static_cast<bool>(rig.mock.addFile(deep + "\\leaf.bin", ut::bytesOf("x"))));
    CHECK(static_cast<bool>(rig.mock.addFile("HDD:\\Deep\\top.bin", ut::bytesOf("x"))));
    CHECK_FAILS(console->remove("/HDD/Deep"), "more than 32 levels");
    CHECK(rig.mock.entry("HDD:\\Deep\\top.bin").has_value());
    CHECK(rig.mock.entry(deep + "\\leaf.bin").has_value());

    // clear keeps the folder
    CHECK_OK(console->clear("/HDD/Games"));
    CHECK(rig.mock.entry("HDD:\\Games").has_value());
    CHECK(rig.mock.listNames("HDD:\\Games").value_or(std::vector<std::string>{"?"}).empty());
    CHECK_FAILS(console->clear("/HDD/Attrs/ro.txt"), "not a folder");

    // rename, and a change of case only
    CHECK_OK(console->rename("/HDD/Attrs/ro.txt", "renamed.txt"));
    CHECK(rig.mock.entry("HDD:\\Attrs\\renamed.txt").has_value() && !rig.mock.entry("HDD:\\Attrs\\ro.txt"));
    CHECK_OK(console->rename("/HDD/Empty", "EMPTY"));
    const auto names = rig.mock.listNames("HDD:\\").value_or(std::vector<std::string>{});
    CHECK(std::find(names.begin(), names.end(), "EMPTY") != names.end());
    CHECK(std::find(names.begin(), names.end(), "Empty") == names.end());
    CHECK_OK(console->rename("/HDD/EMPTY", "EMPTY"));
    CHECK_OK(console->makeDirectory("/HDD/New Folder"));
    CHECK(rig.mock.entry("HDD:\\New Folder").has_value());
}

void discovery() {
    ut::XbdmMockServer mock;
    XbdmDiscoveryOptions o;
    o.timeout = 300ms;
    o.sockets = mock.datagramFactory("10.0.0.7");
    o.connector = [&mock](const updclient::net::Endpoint&) { return mock.connect(); };
    auto found = discoverConsoles(o);
    CHECK_OK(found.status());
    CHECK(found && found->size() == 1);
    if (found && found->size() == 1) {
        CHECK(found.value()[0].name == "MockDevkit");
        CHECK(found.value()[0].address == "10.0.0.7");
        CHECK(found.value()[0].port == 730);
    }

    // cancelled before, and during a search that hears nothing
    o.cancel = std::make_shared<std::atomic_bool>(true);
    auto cancelled = discoverConsoles(o);
    CHECK(!cancelled && cancelled.status().cancelled);
    mock.setUdpMode(ut::XbdmUdpMode::Silent);
    o.cancel = std::make_shared<std::atomic_bool>(false);
    o.timeout = 10s;
    const auto start = std::chrono::steady_clock::now();
    std::thread stopper([flag = o.cancel] {
        std::this_thread::sleep_for(100ms);
        *flag = true;
    });
    cancelled = discoverConsoles(o);
    stopper.join();
    CHECK(!cancelled && cancelled.status().cancelled);
    CHECK(std::chrono::steady_clock::now() - start < 2s);

    // a real UDP socket against the mock's UDP responder on 127.0.0.1
    mock.setUdpMode(ut::XbdmUdpMode::Answer);
    auto port = mock.listenUdp();
    if (!port) {
        std::cout << "SKIPPED (UDP refused): discovery over a real socket\n";
        return;
    }
    XbdmDiscoveryOptions real;
    real.timeout = 500ms;
    real.discovery.broadcastAddress = "127.0.0.1";
    real.discovery.port = *port;
    real.connector = o.connector;
    auto heard = discoverConsoles(real);
    CHECK_OK(heard.status());
    CHECK(heard && heard->size() == 1 && heard.value()[0].address == "127.0.0.1" &&
          heard.value()[0].name == "MockDevkit");
}

void registryKind() {
    ut::XbdmMockServer mock;
    auto port = mock.listenTcp();
    if (!port) {
        std::cout << "SKIPPED (loopback TCP refused): the XBDM registry kind\n";
        return;
    }
    const auto registry = FileSystemRegistry::withBuiltins();
    CHECK(registry.contains("XBDM"));
    std::string error;
    auto fs = registry.open("XBDM", "127.0.0.1:" + std::to_string(*port), error);
    CHECK(fs && fs->name() == "MockDevkit");
    CHECK(error.empty());
    CHECK(!registry.open("XBDM", "127.0.0.1:port", error) && error.find("Invalid console address") == 0);
    auto direct = connectXbdm("127.0.0.1", *port);
    CHECK_OK(direct.status());
    mock.stop();
    auto refused = connectXbdm("127.0.0.1", *port);
    CHECK_FAILS(refused.status(), "Cannot connect: cannot reach 127.0.0.1:");
}

// 4 GiB - 1 each way, the most a getfile length can carry; slow, so only
// with UNNAMED_XBDM_HUGE=1. The mock serves and counts the data without
// storing it.
void hugeFiles(Link link) {
    ut::XbdmMockOptions o = mockOptions();
    o.maxUploadBytes = 0xFFFFFFFFull;
    o.storeUploads = false;
    Rig rig(link, o);
    if (rig.skipped)
        return;
    CHECK(static_cast<bool>(rig.mock.updateDrive({"HDD", 1ull << 40, 1ull << 39})));
    auto console = rig.connect();
    if (!console)
        return;
    constexpr std::uint64_t size = 0xFFFFFFFFull;
    CHECK(static_cast<bool>(rig.mock.addVirtualFile("HDD:\\big.bin", size, 21)));
    auto source = console->openRead("/HDD/big.bin");
    CHECK_OK(source.status());
    if (source) {
        CHECK(source.value()->size() == size);
        Bytes buffer(1u << 20);
        std::uint64_t total = 0;
        bool same = true;
        for (;;) {
            auto n = source.value()->read(buffer.data(), buffer.size());
            CHECK_OK(n.status());
            if (!n || n.value() == 0)
                break;
            same = same && buffer[0] == ut::virtualFileByte(21, total) &&
                   buffer[n.value() - 1] == ut::virtualFileByte(21, total + n.value() - 1);
            total += n.value();
        }
        CHECK(total == size && same);
    }
    auto sink = console->openWrite("/HDD/up.bin", size, false);
    CHECK_OK(sink.status());
    if (sink) {
        Bytes buffer(1u << 20);
        ut::Fnv1a digest;
        for (std::uint64_t at = 0; at < size;) {
            const auto n = static_cast<size_t>(std::min<std::uint64_t>(buffer.size(), size - at));
            ut::virtualFileBytes(22, at, std::span<std::uint8_t>(buffer.data(), n));
            digest.add(std::span<const std::uint8_t>(buffer.data(), n));
            const Status st = sink.value()->write(buffer.data(), n);
            CHECK_OK(st);
            if (!st)
                break;
            at += n;
        }
        CHECK_OK(sink.value()->finish());
        CHECK(rig.mock.fileSize("HDD:\\up.bin") == size);
        CHECK(rig.mock.fileDigest("HDD:\\up.bin") == digest.value);
    }
}

void forEachLink(void (*test)(Link)) {
    test(Link::Memory);
    test(Link::Tcp);
}

} // namespace

int main() {
    // one folder per run: several builds' tests may run at the same time
    scratch = fs::temp_directory_path() / ("unnamed_xbdm_tests-" + std::to_string(std::random_device{}()));
    fs::remove_all(scratch);
    fs::create_directories(scratch);

    pathMapping();
    forEachLink(capabilityFlags);
    forEachLink(browseAndDescribe);
    forEachLink(copyMatrix);
    forEachLink(unknownSizeWrites);
    forEachLink(cancelLeavesNothing);
    forEachLink(dropMidUpload);
    forEachLink(replaceKeepsTheOnlyCopy);
    forEachLink(errorMapping);
    forEachLink(connectionLimits);
    forEachLink(listingDuringTransfer);
    forEachLink(reconnects);
    forEachLink(hostileNames);
    forEachLink(removeAndRename);
    discovery();
    registryKind();
    if (const char* huge = std::getenv("UNNAMED_XBDM_HUGE"); huge && std::string(huge) == "1")
        forEachLink(hugeFiles);
    else
        std::cout << "SKIPPED (set UNNAMED_XBDM_HUGE=1): 4 GiB - 1 in each direction\n";

    fs::remove_all(scratch);
    if (failures)
        std::cerr << failures << " check(s) failed\n";
    else
        std::cout << "All XBDM tests passed\n";
    return failures ? 1 : 0;
}
