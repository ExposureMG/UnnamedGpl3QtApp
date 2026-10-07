#include "core/XbdmFileSystem.hpp"
#include "core/Format.hpp"
#include "core/PathUtil.hpp"

#include <net/tcp_transport.hpp>
#include <net/udp_socket.hpp>
#include <protocols/xbdm/path.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

namespace unnamed::core {

namespace xbdm = updclient::xbdm;
namespace net = updclient::net;
using updclient::ErrorCode;

namespace {

constexpr std::size_t kMaxNameLength = 42;
// Uploads of unknown size are held in memory up to this, then in a host file.
constexpr std::size_t kMemoryStagingBytes = 16u << 20;
constexpr std::size_t kChunkBytes = 1u << 20;
constexpr int kMaxRemoveDepth = 32;
constexpr std::size_t kMaxRemoveEntries = 100000;
constexpr std::uint64_t kMaxGetfileBytes = 0xFFFFFFFFull;
constexpr std::chrono::milliseconds kDiscoverySlice{50};
constexpr std::string_view kAllowedPunctuation = " !#$%&'()-.@[]^_`{}~";

bool sameName(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// Text from the console or the user, safe to show: control characters and
// bytes above 0x7E as \xNN.
std::string printable(std::string_view text) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u > 0x7E) {
            out += "\\x";
            out += digits[u >> 4];
            out += digits[u & 0xF];
        } else {
            out += c;
        }
    }
    return out;
}

std::string capitalized(std::string s) {
    if (!s.empty())
        s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

std::string hex(std::uint64_t v, int width) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%0*llx", width, static_cast<unsigned long long>(v));
    return buf;
}

bool isRoot(const std::string& path) { return path.empty() || path == "/"; }

// FILETIME (100 ns ticks since 1601, read as UTC) to Unix seconds; 0 when
// the console sent none.
std::int64_t unixSeconds(std::optional<std::uint64_t> fileTime) {
    if (!fileTime || *fileTime == 0)
        return 0;
    return static_cast<std::int64_t>(*fileTime / 10'000'000u) - 11'644'473'600LL;
}

std::string attributeText(const xbdm::FileAttributes& a) {
    std::string out;
    if (a.isReadOnly)
        out = "Read-only";
    if (a.isHidden)
        out += out.empty() ? "Hidden" : ", Hidden";
    return out.empty() ? "None" : out;
}

Entry toEntry(const std::string& name, const xbdm::FileAttributes& a) {
    Entry e;
    e.name = name;
    e.type = a.isDirectory ? EntryType::Directory : EntryType::File;
    e.kind = a.isDirectory ? "folder" : kindFromName(name);
    e.size = a.isDirectory ? 0 : a.size;
    e.modified = unixSeconds(a.changedFileTime);
    return e;
}

Entry driveEntry(const std::string& drive) {
    Entry e;
    e.name = drive;
    e.type = EntryType::Directory;
    e.kind = "folder";
    return e;
}

std::string virtualOf(const std::string& consolePath) {
    auto v = xbdm::fromConsolePath(consolePath);
    return v ? *v : printable(consolePath);
}

// What follows "console answered " in a refusal: "410- file already exists".
std::string consoleLine(const updclient::Error& e) {
    constexpr std::string_view marker = "console answered ";
    const auto at = e.message.find(marker);
    return at == std::string::npos ? std::string{} : e.message.substr(at + marker.size());
}

// Why a connect failed ("Connection refused", "timed out after 5000 ms"),
// without the address the caller names anyway.
std::string connectReason(const updclient::Error& e) {
    if (!e.message.starts_with("connect to "))
        return e.message;
    if (const auto at = e.message.find(" failed: "); at != std::string::npos)
        return e.message.substr(at + 9);
    if (const auto at = e.message.find(" timed out"); at != std::string::npos)
        return e.message.substr(at + 1);
    return e.message;
}

bool lostWhileIdle(const updclient::Error& e) {
    return e.code == ErrorCode::Disconnected || e.code == ErrorCode::NotConnected;
}

template <class T>
updclient::Result<T> failed(const updclient::Error& e) {
    return updclient::unexpected<updclient::Error>(e);
}

} // namespace

// --- connections ---------------------------------------------------------------

struct XbdmFileSystem::Impl : std::enable_shared_from_this<XbdmFileSystem::Impl> {
    struct Slot {
        std::unique_ptr<xbdm::XbdmClient> client;
        bool primary = false;
        bool busy = false;
    };
    class Lease;

    XbdmConnectOptions options;
    xbdm::XbdmClient::Connector connector;
    std::string name;
    std::string address;
    std::string type;

    mutable std::mutex mutex;
    // slots[0] is the command connection; the others live for one call or
    // transfer each.
    std::vector<std::unique_ptr<Slot>> slots;
    XbdmConnectionState state = XbdmConnectionState::Connected;
    std::string stateReason;
    std::vector<std::string> leftovers; // temporary uploads still to delete
    std::vector<std::string> kept;      // uploads kept as the only copy

    Result<Lease> acquire(const std::string& what);
    Result<Lease> acquirePrimary(const std::string& what);
    void release(Slot* slot) noexcept;
    updclient::Result<void> open(Slot& slot);

    Status failure(const updclient::Error& e, const std::string& what);
    Status pathFailure(Lease& lease, const updclient::Error& e, const std::string& what,
                       const std::string& consolePath);
    Status missing(Lease& lease, const std::string& message, const std::string& consolePath);
    Result<std::string> mountedDrive(Lease& lease, const std::string& drive, const std::string& what);
    void setState(XbdmConnectionState next, std::string reason);
    std::string notConnected() const;

    template <class Call>
    auto query(Lease& lease, Call&& call) -> decltype(call(std::declval<xbdm::XbdmClient&>()));
    updclient::Result<std::optional<xbdm::FileAttributes>> lookUp(Lease& lease, const std::string& consolePath,
                                                                  bool needSize);
    Status prepareUpload(Lease& lease, const std::string& consolePath, const std::string& path, bool overwrite,
                         std::optional<std::uint64_t> size);
    Status removeTree(Lease& lease, const std::string& consolePath, const std::string& path, bool self);

    void cleanUp(Lease& lease, const std::string& temp) noexcept;
    void sweepLeftovers(Lease& lease) noexcept;
    void addLeftover(const std::string& consolePath, bool keep);
};

class XbdmFileSystem::Impl::Lease {
public:
    Lease() = default;
    Lease(std::shared_ptr<Impl> impl, Slot* slot) : m_impl(std::move(impl)), m_slot(slot) {}
    Lease(Lease&& other) noexcept : m_impl(std::move(other.m_impl)), m_slot(std::exchange(other.m_slot, nullptr)) {}
    Lease& operator=(Lease&& other) noexcept {
        if (this != &other) {
            release();
            m_impl = std::move(other.m_impl);
            m_slot = std::exchange(other.m_slot, nullptr);
        }
        return *this;
    }
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
    ~Lease() { release(); }

    explicit operator bool() const { return m_slot != nullptr; }
    Slot& slot() { return *m_slot; }
    xbdm::XbdmClient& client() { return *m_slot->client; }

    void release() noexcept {
        if (m_slot && m_impl)
            m_impl->release(m_slot);
        m_slot = nullptr;
    }

private:
    std::shared_ptr<Impl> m_impl;
    Slot* m_slot = nullptr;
};

namespace {
using Lease = XbdmFileSystem::Impl::Lease;
using Slot = XbdmFileSystem::Impl::Slot;
} // namespace

std::string XbdmFileSystem::Impl::notConnected() const {
    return "Not connected to " + name + " (" + stateReason + "). Reconnect to continue.";
}

void XbdmFileSystem::Impl::setState(XbdmConnectionState next, std::string reason) {
    std::function<void(XbdmConnectionState)> notify;
    {
        std::lock_guard<std::mutex> lock(mutex);
        stateReason = std::move(reason);
        if (state == next)
            return;
        state = next;
        notify = options.onStateChanged;
    }
    if (notify)
        notify(next);
}

Result<Lease> XbdmFileSystem::Impl::acquire(const std::string& what) {
    Slot* slot = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (state == XbdmConnectionState::Disconnected)
            return Status::failure(what + ": " + notConnected());
        for (auto& s : slots) {
            if (!s->busy) {
                slot = s.get();
                break;
            }
        }
        if (!slot) {
            if (slots.size() >= options.maxConnections)
                return Status::failure(what + ": all " + std::to_string(options.maxConnections) +
                                       " connections this place may open to " + name +
                                       " are in use. Wait for a transfer to end.");
            slots.push_back(std::make_unique<Slot>());
            slot = slots.back().get();
        }
        slot->busy = true;
    }
    Lease lease(shared_from_this(), slot);
    if (auto r = open(*slot); !r)
        return failure(r.error(), what);
    return lease;
}

Result<Lease> XbdmFileSystem::Impl::acquirePrimary(const std::string& what) {
    Slot* slot = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex);
        slot = slots.front().get();
        if (slot->busy)
            return Status::failure(what + ": a transfer is using the connection to " + name +
                                   "; cancel it or wait for it to end");
        slot->busy = true;
    }
    return Lease(shared_from_this(), slot);
}

void XbdmFileSystem::Impl::release(Slot* slot) noexcept {
    std::unique_ptr<xbdm::XbdmClient> closing;
    {
        std::lock_guard<std::mutex> lock(mutex);
        slot->busy = false;
        if (!slot->primary) {
            closing = std::move(slot->client);
            slots.erase(std::remove_if(slots.begin(), slots.end(),
                                       [slot](const std::unique_ptr<Slot>& s) { return s.get() == slot; }),
                        slots.end());
        }
    }
    // says bye, outside the lock
    closing.reset();
}

// A connection the place closed (or lost while idle) is opened again here;
// reconnect() also deletes the temporary uploads queued on it.
updclient::Result<void> XbdmFileSystem::Impl::open(Slot& slot) {
    if (slot.client && slot.client->isConnected())
        return {};
    if (slot.client)
        return slot.client->reconnect();
    auto client = xbdm::XbdmClient::open(connector, options.client);
    if (!client)
        return failed<void>(client.error());
    auto made = std::make_unique<xbdm::XbdmClient>(std::move(*client));
    std::lock_guard<std::mutex> lock(mutex);
    slot.client = std::move(made);
    return {};
}

Status XbdmFileSystem::Impl::failure(const updclient::Error& e, const std::string& what) {
    if (e.code == ErrorCode::Cancelled)
        return Status::cancelledByUser();
    if (const auto code = xbdm::consoleStatusCode(e)) {
        std::string why;
        switch (*code) {
        case xbdm::status::kMaxConnections:
            why = name + " refuses another connection: its connection limit is reached. Wait for a transfer "
                         "to end, or close other programs connected to it";
            break;
        case xbdm::status::kNoSuchFile: why = "not found"; break;
        case xbdm::status::kMustCopy: why = "the console cannot move it to another drive"; break;
        case xbdm::status::kAlreadyExists: why = "already exists"; break;
        case xbdm::status::kDirectoryNotEmpty: why = "the folder is not empty"; break;
        case xbdm::status::kBadFileName: why = "the console refuses the name"; break;
        case xbdm::status::kCannotCreate: why = "the console cannot create it (is its folder still there?)"; break;
        case xbdm::status::kCannotAccess: why = "access denied"; break;
        case xbdm::status::kDeviceFull: why = "not enough free space on the drive"; break;
        case xbdm::status::kInvalidCommand: why = "the console does not support this command"; break;
        default: why = "the console refused it"; break;
        }
        const std::string line = consoleLine(e);
        return Status::failure(what + ": " + why + (line.empty() ? "" : " (console: " + printable(line) + ")"));
    }
    switch (e.code) {
    case ErrorCode::ConnectFailed: {
        const std::string reason = "cannot reach " + address + ": " + connectReason(e);
        setState(XbdmConnectionState::Disconnected, reason);
        return Status::failure(what + ": " + reason);
    }
    case ErrorCode::Timeout: {
        const std::string reason = e.message.starts_with("connect to ")
                                       ? "cannot reach " + address + ": " + connectReason(e)
                                       : name + " stopped responding (" + e.message + ")";
        setState(XbdmConnectionState::Disconnected, reason);
        return Status::failure(what + ": " + reason);
    }
    case ErrorCode::Disconnected:
    case ErrorCode::NotConnected:
        return Status::failure(what + ": the connection to " + name + " was lost (" + e.message + ")");
    case ErrorCode::Protocol:
        return Status::failure(what + ": " + name + " sent an answer that could not be understood (" + e.message +
                               ")");
    default:
        return Status::failure(what + ": " + e.message);
    }
}

// A path the console does not know, on a drive it does not list, is on a
// drive that is not mounted.
Status XbdmFileSystem::Impl::pathFailure(Lease& lease, const updclient::Error& e, const std::string& what,
                                         const std::string& consolePath) {
    const auto code = xbdm::consoleStatusCode(e);
    if (code && (*code == xbdm::status::kNoSuchFile || *code == xbdm::status::kCannotCreate)) {
        auto drive = xbdm::driveOf(consolePath);
        auto drives = lease.client().drives();
        if (drive && drives &&
            std::none_of(drives->begin(), drives->end(), [&](const std::string& d) { return sameName(d, *drive); }))
            return Status::failure(what + ": the drive " + *drive + ": is not mounted on " + name);
    }
    return failure(e, what);
}

Status XbdmFileSystem::Impl::missing(Lease& lease, const std::string& message, const std::string& consolePath) {
    auto drive = xbdm::driveOf(consolePath);
    auto drives = lease.client().drives();
    if (drive && drives &&
        std::none_of(drives->begin(), drives->end(), [&](const std::string& d) { return sameName(d, *drive); }))
        return Status::failure(message + ": the drive " + *drive + ": is not mounted on " + name);
    return Status::failure(message);
}

// The drive's name as the console lists it.
Result<std::string> XbdmFileSystem::Impl::mountedDrive(Lease& lease, const std::string& drive, const std::string& what) {
    auto drives = query(lease, [](xbdm::XbdmClient& c) { return c.drives(); });
    if (!drives)
        return failure(drives.error(), what);
    for (const std::string& d : *drives) {
        if (sameName(d, drive))
            return d;
    }
    return Status::failure(what + ": the drive " + drive + ": is not mounted on " + name);
}

// A query that finds its connection dropped while idle is sent again once,
// on a new connection.
template <class Call>
auto XbdmFileSystem::Impl::query(Lease& lease, Call&& call) -> decltype(call(std::declval<xbdm::XbdmClient&>())) {
    auto r = call(lease.client());
    if (r || !lostWhileIdle(r.error()))
        return r;
    if (auto again = open(lease.slot()); !again)
        return updclient::unexpected<updclient::Error>(again.error());
    return call(lease.client());
}

// getfileattributes, or the parent's listing when the console does not know
// that command, or (needSize) sent no size. nullopt: no such path. Other
// refusals (access denied) are errors.
updclient::Result<std::optional<xbdm::FileAttributes>>
XbdmFileSystem::Impl::lookUp(Lease& lease, const std::string& consolePath, bool needSize) {
    std::optional<xbdm::FileAttributes> known;
    auto direct = query(lease, [&](xbdm::XbdmClient& c) { return c.attributes(consolePath); });
    if (direct) {
        if (!needSize || direct->sizeKnown)
            return std::optional<xbdm::FileAttributes>(*direct);
        known = *direct;
    } else {
        const auto code = xbdm::consoleStatusCode(direct.error());
        if (!code || *code == xbdm::status::kCannotAccess)
            return failed<std::optional<xbdm::FileAttributes>>(direct.error());
        if (*code != xbdm::status::kInvalidCommand)
            return std::optional<xbdm::FileAttributes>{};
    }
    auto parent = xbdm::parentOf(consolePath);
    auto leaf = xbdm::nameOf(consolePath);
    if (!parent || !leaf)
        return known;
    auto listing = lease.client().list(*parent);
    if (!listing) {
        if (xbdm::consoleStatusCode(listing.error()))
            return known;
        return failed<std::optional<xbdm::FileAttributes>>(listing.error());
    }
    for (const xbdm::DirEntry& entry : listing->entries) {
        if (sameName(entry.name, *leaf))
            return std::optional<xbdm::FileAttributes>(static_cast<const xbdm::FileAttributes&>(entry));
    }
    return known;
}

Status XbdmFileSystem::Impl::prepareUpload(Lease& lease, const std::string& consolePath, const std::string& path,
                                           bool overwrite, std::optional<std::uint64_t> size) {
    const std::string what = "Cannot write " + path;
    auto existing = lookUp(lease, consolePath, false);
    if (!existing)
        return pathFailure(lease, existing.error(), what, consolePath);
    if (*existing) {
        if ((*existing)->isDirectory)
            return Status::failure(what + ": a folder is in the way");
        if (!overwrite)
            return Status::failure("Already exists: " + path);
    }
    if (size && *size > 0) {
        if (auto drive = xbdm::driveOf(consolePath)) {
            auto space = lease.client().driveSpace(*drive);
            if (space && *size > space->freeToCaller)
                return Status::failure(what + ": not enough free space on " + *drive + ": (" + humanSize(*size) +
                                       " needed, " + humanSize(space->freeToCaller) + " free)");
            if (!space && !xbdm::consoleStatusCode(space.error()))
                return failure(space.error(), what);
        }
    }
    return Status::success();
}

void XbdmFileSystem::Impl::addLeftover(const std::string& consolePath, bool keep) {
    std::lock_guard<std::mutex> lock(mutex);
    auto& list = keep ? kept : leftovers;
    if (std::find(list.begin(), list.end(), consolePath) == list.end())
        list.push_back(consolePath);
}

// After an upload ended without its rename: the temporary file the library
// queued for deletion is deleted over a new connection, then looked for.
void XbdmFileSystem::Impl::cleanUp(Lease& lease, const std::string& temp) noexcept {
    try {
        xbdm::XbdmClient& c = lease.client();
        const auto pending = c.pendingCleanup();
        if (std::find(pending.begin(), pending.end(), temp) == pending.end())
            return;
        if (auto r = c.reconnect(); !r) {
            (void)failure(r.error(), "Cannot delete the temporary upload " + temp);
            addLeftover(temp, false);
            return;
        }
        // it answered a new connection, so it is responding again
        setState(XbdmConnectionState::Connected, {});
        if (c.attributes(temp) && !c.removeFile(temp))
            addLeftover(temp, false);
    } catch (...) {
        addLeftover(temp, false);
    }
}

void XbdmFileSystem::Impl::sweepLeftovers(Lease& lease) noexcept {
    try {
        std::vector<std::string> todo;
        {
            std::lock_guard<std::mutex> lock(mutex);
            todo = leftovers;
        }
        for (const std::string& temp : todo) {
            auto present = lease.client().attributes(temp);
            const bool gone = !present ? xbdm::consoleStatusCode(present.error()) == xbdm::status::kNoSuchFile
                                       : static_cast<bool>(lease.client().removeFile(temp));
            if (!gone)
                continue;
            std::lock_guard<std::mutex> lock(mutex);
            leftovers.erase(std::remove(leftovers.begin(), leftovers.end(), temp), leftovers.end());
        }
    } catch (...) {
    }
}

Status XbdmFileSystem::Impl::removeTree(Lease& lease, const std::string& consolePath, const std::string& path,
                                        bool self) {
    struct Item {
        std::string path;
        bool directory;
    };
    std::vector<Item> order; // children before their folder
    const std::string what = "Cannot delete " + path;

    // The whole tree is listed first, so a limit or an odd entry stops the
    // delete before anything is gone.
    std::function<Status(const std::string&, int)> plan = [&](const std::string& folder, int depth) -> Status {
        if (depth > kMaxRemoveDepth)
            return Status::failure(what + ": folders are nested more than " + std::to_string(kMaxRemoveDepth) +
                                   " levels deep; nothing was deleted");
        auto listing = query(lease, [&](xbdm::XbdmClient& c) { return c.list(folder); });
        if (!listing)
            return pathFailure(lease, listing.error(), what, folder);
        if (listing->skipped > 0)
            return Status::failure(what + ": " + virtualOf(folder) + " holds " + std::to_string(listing->skipped) +
                                   (listing->skipped == 1 ? " entry" : " entries") +
                                   " whose name cannot be sent back to the console; nothing was deleted");
        for (const xbdm::DirEntry& entry : listing->entries) {
            auto child = xbdm::joinPath(folder, entry.name);
            if (!child)
                return Status::failure(what + ": " + child.error().message + "; nothing was deleted");
            if (entry.isDirectory) {
                if (Status st = plan(*child, depth + 1); !st)
                    return st;
            }
            order.push_back({*child, entry.isDirectory});
            if (order.size() > kMaxRemoveEntries)
                return Status::failure(what + ": it holds more than " + std::to_string(kMaxRemoveEntries) +
                                       " entries; nothing was deleted");
        }
        return Status::success();
    };
    if (Status st = plan(consolePath, 1); !st)
        return st;
    if (self)
        order.push_back({consolePath, true});

    std::size_t done = 0;
    for (const Item& item : order) {
        auto r = query(lease, [&](xbdm::XbdmClient& c) {
            return item.directory ? c.removeDirectory(item.path) : c.removeFile(item.path);
        });
        if (!r) {
            Status st = failure(r.error(), "Cannot delete " + virtualOf(item.path));
            if (!st.cancelled && done > 0)
                st.message += " (" + std::to_string(done) + " of " + std::to_string(order.size()) +
                              " items were deleted before)";
            return st;
        }
        ++done;
    }
    return Status::success();
}

// --- streams -------------------------------------------------------------------

namespace {

using Impl = XbdmFileSystem::Impl;

// A getfile: owns its connection until the last byte was read.
class XbdmSource final : public ByteSource {
public:
    XbdmSource(std::shared_ptr<Impl> impl, Lease lease, xbdm::FileReader reader, std::string path)
        : m_impl(std::move(impl)), m_lease(std::move(lease)), m_reader(std::move(reader)), m_path(std::move(path)) {
        if (!m_reader.isOpen())
            m_lease.release();
    }

    ~XbdmSource() override {
        // closes the connection: the protocol cannot skip the rest of a download
        m_reader.abort();
        m_lease.release();
    }

    Result<std::size_t> read(void* buffer, std::size_t size) override {
        if (!m_reader.isOpen()) {
            if (m_reader.position() < m_reader.size())
                return Status::failure("Cannot read " + m_path + ": the download ended early");
            return std::size_t{0};
        }
        auto* out = static_cast<std::uint8_t*>(buffer);
        std::size_t total = 0;
        while (total < size && m_reader.isOpen()) {
            auto n = m_reader.read(std::span<std::uint8_t>(out + total, size - total));
            if (!n) {
                Status st = m_impl->failure(n.error(), "Cannot read " + m_path);
                m_lease.release();
                return st;
            }
            if (*n == 0)
                break;
            total += *n;
        }
        if (!m_reader.isOpen())
            m_lease.release();
        return total;
    }

    std::optional<std::uint64_t> size() const override { return m_reader.size(); }

private:
    std::shared_ptr<Impl> m_impl;
    Lease m_lease;
    xbdm::FileReader m_reader;
    std::string m_path;
};

// A sendfile of a known size: owns its connection until finish() renamed the
// upload into place, or until it is abandoned.
class UploadSink final : public ByteSink {
public:
    UploadSink(std::shared_ptr<Impl> impl, Lease lease, xbdm::FileWriter writer, std::string path)
        : m_impl(std::move(impl)), m_lease(std::move(lease)), m_writer(std::move(writer)), m_path(std::move(path)) {}

    ~UploadSink() override { abandon(); }

    Status write(const void* data, std::size_t size) override {
        if (m_ended)
            return Status::failure("Cannot write " + m_path + ": the upload has ended");
        if (size > m_writer.size() - m_writer.written()) {
            abandon();
            return Status::failure("Cannot write " + m_path + ": its source grew while it was copied; nothing was "
                                   "written under its name");
        }
        auto r = m_writer.write(std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(data), size));
        if (!r) {
            Status st = m_impl->failure(r.error(), "Cannot write " + m_path);
            abandon();
            return st;
        }
        return Status::success();
    }

    Status finish() override {
        if (m_ended)
            return Status::failure("Cannot write " + m_path + ": the upload has ended");
        if (m_writer.written() != m_writer.size()) {
            const std::string message = "Cannot write " + m_path + ": its source ended after " +
                                        std::to_string(m_writer.written()) + " of " +
                                        std::to_string(m_writer.size()) + " bytes; nothing was written under its name";
            abandon();
            return Status::failure(message);
        }
        auto r = m_writer.finish();
        if (r) {
            m_ended = true;
            m_lease.release();
            return Status::success();
        }
        // The old file was deleted (or may have been) and the rename failed:
        // the upload is the only copy, kept under its temporary name.
        if (r.error().message.find(" is kept as ") != std::string::npos) {
            const std::string temp = m_writer.temporaryPath();
            m_impl->addLeftover(temp, true);
            Status st = m_impl->failure(r.error(), "Cannot write " + m_path);
            st.message += ". Its new contents are kept on the console as " + virtualOf(temp);
            m_ended = true;
            m_lease.release();
            return st;
        }
        Status st = m_impl->failure(r.error(), "Cannot write " + m_path);
        abandon();
        return st;
    }

    // Nothing appears under the final name; the temporary file is deleted.
    void abandon() noexcept {
        if (m_ended)
            return;
        m_ended = true;
        m_writer.abort();
        if (m_lease)
            m_impl->cleanUp(m_lease, m_writer.temporaryPath());
        m_lease.release();
    }

private:
    std::shared_ptr<Impl> m_impl;
    Lease m_lease;
    xbdm::FileWriter m_writer;
    std::string m_path;
    bool m_ended = false;
};

// Data of unknown length: held on the host until finish(), which sends it.
class StagedSink final : public ByteSink {
public:
    StagedSink(std::shared_ptr<Impl> impl, std::string path, std::string consolePath, bool overwrite)
        : m_impl(std::move(impl)), m_path(std::move(path)), m_consolePath(std::move(consolePath)),
          m_overwrite(overwrite), m_limit(m_impl->options.client.maxUploadBytes) {}

    ~StagedSink() override {
        if (m_file.is_open())
            m_file.close();
        if (!m_staging.empty()) {
            std::error_code ec;
            std::filesystem::remove(m_staging, ec);
        }
    }

    Status write(const void* data, std::size_t size) override {
        if (m_done)
            return Status::failure("Cannot write " + m_path + ": already finished");
        if (size > m_limit - m_total)
            return Status::failure("Cannot write " + m_path + ": it is more than " + humanSize(m_limit) +
                                   ", the largest file XBDM can send");
        const auto* bytes = static_cast<const char*>(data);
        if (!m_file.is_open() && m_memory.size() + size <= kMemoryStagingBytes) {
            m_memory.insert(m_memory.end(), bytes, bytes + size);
        } else {
            if (!m_file.is_open()) {
                if (Status st = spill(); !st)
                    return st;
            }
            m_file.write(bytes, static_cast<std::streamsize>(size));
            if (!m_file)
                return Status::failure("Cannot write " + m_path + ": the temporary copy in " +
                                       pathToUtf8(m_staging.parent_path()) + " failed (disk full?)");
        }
        m_total += size;
        return Status::success();
    }

    Status finish() override {
        if (m_done)
            return Status::success();
        auto lease = m_impl->acquire("Cannot write " + m_path);
        if (!lease)
            return lease.status();
        if (Status st = m_impl->prepareUpload(lease.value(), m_consolePath, m_path, m_overwrite, m_total); !st)
            return st;
        auto writer = m_impl->query(lease.value(),
                                    [&](xbdm::XbdmClient& c) { return c.openWrite(m_consolePath, m_total); });
        if (!writer)
            return m_impl->pathFailure(lease.value(), writer.error(), "Cannot write " + m_path, m_consolePath);
        UploadSink upload(m_impl, std::move(lease.value()), std::move(*writer), m_path);
        if (m_file.is_open()) {
            m_file.flush();
            m_file.seekg(0);
            std::vector<char> buffer(kChunkBytes);
            for (std::uint64_t sent = 0; sent < m_total;) {
                const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), m_total - sent));
                m_file.read(buffer.data(), static_cast<std::streamsize>(n));
                if (!m_file)
                    return Status::failure("Cannot write " + m_path + ": reading the temporary copy failed");
                if (Status st = upload.write(buffer.data(), n); !st)
                    return st;
                sent += n;
            }
        } else {
            for (std::size_t sent = 0; sent < m_memory.size();) {
                const std::size_t n = std::min(kChunkBytes, m_memory.size() - sent);
                if (Status st = upload.write(m_memory.data() + sent, n); !st)
                    return st;
                sent += n;
            }
        }
        if (Status st = upload.finish(); !st)
            return st;
        m_done = true;
        return Status::success();
    }

private:
    Status spill() {
        static std::atomic<unsigned> counter{0};
        std::error_code ec;
        const auto dir = std::filesystem::temp_directory_path(ec);
        if (ec)
            return Status::failure("Cannot write " + m_path + ": no temporary folder");
        m_staging = dir / ("unnamed-xbdm-upload-" + std::to_string(std::random_device{}()) + "-" +
                           std::to_string(counter++) + ".part");
        m_file.open(m_staging, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
        if (!m_file.is_open())
            return Status::failure("Cannot write " + m_path + ": cannot create a temporary file in " +
                                   pathToUtf8(dir));
        m_file.write(m_memory.data(), static_cast<std::streamsize>(m_memory.size()));
        m_memory.clear();
        m_memory.shrink_to_fit();
        if (!m_file)
            return Status::failure("Cannot write " + m_path + ": the temporary copy failed (disk full?)");
        return Status::success();
    }

    std::shared_ptr<Impl> m_impl;
    std::string m_path;
    std::string m_consolePath;
    bool m_overwrite;
    std::uint64_t m_limit;
    std::vector<char> m_memory;
    std::filesystem::path m_staging;
    std::fstream m_file;
    std::uint64_t m_total = 0;
    bool m_done = false;
};

} // namespace

// --- names and paths -----------------------------------------------------------

Status checkXboxName(const std::string& name) {
    const std::string shown = "“" + printable(name) + "”";
    if (name.empty() || name == "." || name == "..")
        return Status::failure(shown + " is not a valid name");
    for (char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u > 0x7E)
            return Status::failure(shown + " is not a valid Xbox name: only printable ASCII characters are allowed");
    }
    if (name.size() > kMaxNameLength)
        return Status::failure(shown + " is too long: Xbox names have at most " + std::to_string(kMaxNameLength) +
                               " characters");
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && kAllowedPunctuation.find(c) == std::string_view::npos)
            return Status::failure(shown + " is not a valid Xbox name: use letters, digits, spaces and " +
                                   "! # $ % & ' ( ) - . @ [ ] ^ _ ` { } ~");
    }
    if (auto r = xbdm::validateName(name); !r)
        return Status::failure(shown + " is not a valid name: " + r.error().message);
    return Status::success();
}

Result<std::string> xbdmConsolePath(const std::string& virtualPath) {
    if (isRoot(virtualPath))
        return Status::failure("The root lists the console's drives and has no console path");
    auto r = xbdm::toConsolePath(virtualPath);
    if (!r)
        return Status::failure(capitalized(r.error().message));
    return *r;
}

Result<std::string> xbdmVirtualPath(const std::string& consolePath) {
    auto r = xbdm::fromConsolePath(consolePath);
    if (!r)
        return Status::failure(capitalized(r.error().message));
    return *r;
}

// --- the filesystem ------------------------------------------------------------

XbdmFileSystem::XbdmFileSystem(std::shared_ptr<Impl> impl) : m_impl(std::move(impl)) {}

XbdmFileSystem::~XbdmFileSystem() = default;

Result<std::unique_ptr<XbdmFileSystem>> XbdmFileSystem::connect(XbdmConnectOptions options) {
    auto impl = std::make_shared<Impl>();
    if (options.connector) {
        impl->connector = options.connector;
        impl->address = options.host.empty() ? "console" : options.host + ":" + std::to_string(options.port);
    } else {
        if (options.host.empty())
            return Status::failure("No console address given");
        if (options.port == 0)
            return Status::failure("No port given for " + options.host);
        impl->connector = [host = options.host, port = options.port, timeout = options.connectTimeout] {
            return net::TcpTransport::connect(host, port, timeout);
        };
        impl->address = options.host + ":" + std::to_string(options.port);
    }
    options.maxConnections = std::max<std::size_t>(options.maxConnections, 1);
    impl->name = impl->address;
    impl->options = std::move(options);
    auto primary = std::make_unique<Slot>();
    primary->primary = true;
    impl->slots.push_back(std::move(primary));

    auto lease = impl->acquire("Cannot connect");
    if (!lease)
        return lease.status();
    auto debugName = lease.value().client().debugName();
    if (!impl->options.displayName.empty())
        impl->name = impl->options.displayName;
    else if (debugName && !debugName->empty())
        impl->name = printable(*debugName);
    if (auto type = lease.value().client().consoleType())
        impl->type = printable(*type);
    lease.value().release();
    return std::unique_ptr<XbdmFileSystem>(new XbdmFileSystem(std::move(impl)));
}

std::string XbdmFileSystem::name() const { return m_impl->name; }

Capability XbdmFileSystem::capabilities() const {
    return Capability::Browse | Capability::Inspect | Capability::Extract | Capability::Inject |
           Capability::Replace | Capability::Remove | Capability::MakeDirectory | Capability::Rename |
           Capability::Clear;
}

Status XbdmFileSystem::list(const std::string& path, std::vector<Entry>& out) const {
    Impl& impl = *m_impl;
    if (isRoot(path)) {
        const std::string what = "Cannot list the drives of " + impl.name;
        auto lease = impl.acquire(what);
        if (!lease)
            return lease.status();
        auto drives = impl.query(lease.value(), [](xbdm::XbdmClient& c) { return c.drives(); });
        if (!drives)
            return impl.failure(drives.error(), what);
        out.clear();
        for (const std::string& drive : *drives)
            out.push_back(driveEntry(drive));
        return Status::success();
    }
    auto consolePath = xbdmConsolePath(path);
    if (!consolePath)
        return consolePath.status();
    const std::string what = "Cannot list " + path;
    auto lease = impl.acquire(what);
    if (!lease)
        return lease.status();
    auto listing = impl.query(lease.value(), [&](xbdm::XbdmClient& c) { return c.list(consolePath.value()); });
    if (!listing)
        return impl.pathFailure(lease.value(), listing.error(), what, consolePath.value());
    out.clear();
    out.reserve(listing->entries.size());
    for (const xbdm::DirEntry& e : listing->entries)
        out.push_back(toEntry(e.name, e));
    return Status::success();
}

Status XbdmFileSystem::stat(const std::string& path, Entry& out) const {
    Impl& impl = *m_impl;
    if (isRoot(path)) {
        out = driveEntry({});
        return Status::success();
    }
    auto consolePath = xbdmConsolePath(path);
    if (!consolePath)
        return consolePath.status();
    const std::string what = "Cannot open " + path;
    auto lease = impl.acquire(what);
    if (!lease)
        return lease.status();
    if (xbdm::isDriveRoot(consolePath.value())) {
        auto drive = impl.mountedDrive(lease.value(), *xbdm::driveOf(consolePath.value()), what);
        if (!drive)
            return drive.status();
        out = driveEntry(drive.value());
        return Status::success();
    }
    auto found = impl.lookUp(lease.value(), consolePath.value(), false);
    if (!found)
        return impl.pathFailure(lease.value(), found.error(), what, consolePath.value());
    if (!*found)
        return impl.missing(lease.value(), "No such file or folder: " + path, consolePath.value());
    out = toEntry(*xbdm::nameOf(consolePath.value()), **found);
    return Status::success();
}

Status XbdmFileSystem::describe(const std::string& path, Details& out) const {
    Impl& impl = *m_impl;
    if (isRoot(path)) {
        std::vector<Entry> drives;
        if (Status st = list(path, drives); !st)
            return st;
        out = {};
        out.title = impl.name;
        out.kind = "folder";
        out.subtitle = "Console drives";
        out.groups.push_back({"General", {{"Name", impl.name}, {"Location", "/"},
                                          {"Drives", std::to_string(drives.size())}}});
        return Status::success();
    }
    auto consolePath = xbdmConsolePath(path);
    if (!consolePath)
        return consolePath.status();
    const std::string& cp = consolePath.value();
    const std::string what = "Cannot open " + path;
    auto lease = impl.acquire(what);
    if (!lease)
        return lease.status();
    Lease& l = lease.value();

    out = {};
    if (xbdm::isDriveRoot(cp)) {
        auto drive = impl.mountedDrive(l, *xbdm::driveOf(cp), what);
        if (!drive)
            return drive.status();
        out.title = drive.value();
        out.kind = "folder";
        out.subtitle = "Drive";
        out.groups.push_back(
            {"General", {{"Name", drive.value()}, {"Location", path}, {"Console path", drive.value() + ":\\"}}});
        auto space = l.client().driveSpace(drive.value());
        if (space) {
            out.groups.push_back({"Capacity",
                                  {{"Total", humanSize(space->totalBytes)},
                                   {"Free", humanSize(space->freeToCaller)},
                                   {"Used", humanSize(space->usedBytes())}}});
        } else if (xbdm::consoleStatusCode(space.error())) {
            out.groups.push_back({"Capacity", {{"Free space", "Unknown (the console did not tell)"}}});
        } else {
            return impl.failure(space.error(), what);
        }
        return Status::success();
    }

    auto found = impl.lookUp(l, cp, false);
    if (!found)
        return impl.pathFailure(l, found.error(), what, cp);
    if (!*found)
        return impl.missing(l, "No such file or folder: " + path, cp);
    const xbdm::FileAttributes& a = **found;
    const std::string name = *xbdm::nameOf(cp);
    out.title = name;
    out.kind = a.isDirectory ? "folder" : kindFromName(name);
    out.subtitle = kindLabel(out.kind);

    PropertyGroup general{"General", {{"Name", name}, {"Location", path}, {"Console path", cp}}};
    if (a.isDirectory) {
        auto listing = impl.query(l, [&](xbdm::XbdmClient& c) { return c.list(cp); });
        if (listing) {
            general.items.push_back({"Items", std::to_string(listing->entries.size())});
            if (listing->skipped > 0)
                general.items.push_back({"Not shown", std::to_string(listing->skipped) +
                                                          " (names the console cannot take back)"});
        }
    } else {
        general.items.push_back({"Size", a.sizeKnown ? humanSize(a.size) : "Unknown (the console did not send it)"});
    }
    if (const auto t = unixSeconds(a.changedFileTime))
        general.items.push_back({"Modified", formatTimestamp(t)});
    if (const auto t = unixSeconds(a.createdFileTime))
        general.items.push_back({"Created", formatTimestamp(t)});
    out.groups.push_back(std::move(general));

    PropertyGroup entry{"XBDM entry", {{"Attributes", attributeText(a)}}};
    entry.items.push_back({"Created (FILETIME)", a.createdFileTime ? hex(*a.createdFileTime, 16) : "Not sent"});
    entry.items.push_back({"Changed (FILETIME)", a.changedFileTime ? hex(*a.changedFileTime, 16) : "Not sent"});
    out.groups.push_back(std::move(entry));
    return Status::success();
}

Status XbdmFileSystem::describeFileSystem(Details& out) const {
    Impl& impl = *m_impl;
    out = {};
    out.title = impl.name;
    out.subtitle = "Console over XBDM";
    out.kind = "filesystem";

    PropertyGroup caps{"Supported operations", {}};
    for (const auto& op : capabilityNames(capabilities()))
        caps.items.push_back({op, "Yes"});

    const std::string what = "Cannot describe " + impl.name;
    auto lease = impl.acquire(what);
    if (!lease) {
        out.notice = lease.status().message;
        out.groups.push_back({"Connection", {{"Address", impl.address}, {"State", "Disconnected"}}});
        out.groups.push_back(std::move(caps));
        return lease.status().cancelled ? lease.status() : Status::success();
    }
    Lease& l = lease.value();
    auto info = impl.query(l, [](xbdm::XbdmClient& c) { return c.consoleInfo(); });
    if (!info)
        return impl.failure(info.error(), what);
    const auto shown = [](const std::optional<std::string>& v) { return v ? printable(*v) : "Unknown"; };
    PropertyGroup console{"Console",
                          {{"Name", shown(info->debugName)},
                           {"Type", shown(info->consoleType)},
                           {"Console ID", shown(info->consoleId)},
                           {"Execution state", info->execState ? printable(info->execState->text) : "Unknown"},
                           {"Running title", info->runningTitle ? printable(info->runningTitle->name) : "None"},
                           {"Title address", info->titleAddress ? printable(info->titleAddress->text) + " (" +
                                                                      hex(info->titleAddress->raw, 8) + ")"
                                                                : "Unknown"}}};
    out.groups.push_back(std::move(console));
    out.groups.push_back({"Connection",
                          {{"Address", impl.address},
                           {"State", "Connected"},
                           {"Connections", std::to_string(openConnections()) + " of at most " +
                                               std::to_string(impl.options.maxConnections)}}});

    auto drives = impl.query(l, [](xbdm::XbdmClient& c) { return c.drives(); });
    if (!drives)
        return impl.failure(drives.error(), what);
    PropertyGroup space{"Drives", {}};
    for (const std::string& drive : *drives) {
        auto s = l.client().driveSpace(drive);
        if (s) {
            space.items.push_back({drive + ":", humanSize(s->freeToCaller) + " free of " + humanSize(s->totalBytes)});
        } else if (xbdm::consoleStatusCode(s.error())) {
            space.items.push_back({drive + ":", "Free space unknown"});
        } else {
            return impl.failure(s.error(), what);
        }
    }
    out.groups.push_back(std::move(space));
    out.groups.push_back(std::move(caps));

    impl.sweepLeftovers(l);
    const auto left = leftoverFiles();
    if (!left.empty()) {
        out.notice = "Temporary uploads are left on the console:";
        for (const auto& p : left)
            out.notice += " " + virtualOf(p);
    }
    return Status::success();
}

Result<std::unique_ptr<ByteSource>> XbdmFileSystem::openRead(const std::string& path) const {
    Impl& impl = *m_impl;
    auto consolePath = xbdmConsolePath(path);
    if (!consolePath)
        return consolePath.status();
    const std::string& cp = consolePath.value();
    if (xbdm::isDriveRoot(cp))
        return Status::failure("Is a drive: " + path);
    const std::string what = "Cannot read " + path;
    auto lease = impl.acquire(what);
    if (!lease)
        return lease.status();
    Lease& l = lease.value();
    auto found = impl.lookUp(l, cp, true);
    if (!found)
        return impl.pathFailure(l, found.error(), what, cp);
    if (!*found)
        return impl.missing(l, "No such file: " + path, cp);
    if ((*found)->isDirectory)
        return Status::failure("Is a folder: " + path);
    std::optional<std::uint64_t> expected;
    if ((*found)->sizeKnown) {
        expected = (*found)->size;
        if (*expected > kMaxGetfileBytes)
            return Status::failure(what + ": it is " + humanSize(*expected) +
                                   ", and XBDM cannot download files of 4 GiB or more");
    }
    auto reader = impl.query(l, [&](xbdm::XbdmClient& c) { return c.openRead(cp, expected); });
    if (!reader)
        return impl.pathFailure(l, reader.error(), what, cp);
    return std::unique_ptr<ByteSource>(
        std::make_unique<XbdmSource>(m_impl, std::move(l), std::move(*reader), path));
}

Result<std::unique_ptr<ByteSink>> XbdmFileSystem::openWrite(const std::string& path, std::optional<std::uint64_t> size,
                                                            bool overwrite) {
    Impl& impl = *m_impl;
    auto consolePath = xbdmConsolePath(path);
    if (!consolePath)
        return consolePath.status();
    const std::string& cp = consolePath.value();
    if (xbdm::isDriveRoot(cp))
        return Status::failure("Invalid path: " + path + " is a drive");
    if (Status st = checkXboxName(*xbdm::nameOf(cp)); !st)
        return st;
    const std::string what = "Cannot write " + path;
    const std::uint64_t limit = impl.options.client.maxUploadBytes;
    if (size && *size > limit)
        return Status::failure(what + ": it is " + humanSize(*size) + ", and XBDM can send at most " +
                               humanSize(limit) + " (files of 4 GiB or more are not possible)");
    auto lease = impl.acquire(what);
    if (!lease)
        return lease.status();
    Lease& l = lease.value();
    if (Status st = impl.prepareUpload(l, cp, path, overwrite, size); !st)
        return st;
    if (!size) {
        l.release();
        return std::unique_ptr<ByteSink>(std::make_unique<StagedSink>(m_impl, path, cp, overwrite));
    }
    auto writer = impl.query(l, [&](xbdm::XbdmClient& c) { return c.openWrite(cp, *size); });
    if (!writer)
        return impl.pathFailure(l, writer.error(), what, cp);
    return std::unique_ptr<ByteSink>(std::make_unique<UploadSink>(m_impl, std::move(l), std::move(*writer), path));
}

Status XbdmFileSystem::makeDirectory(const std::string& path) {
    Impl& impl = *m_impl;
    auto consolePath = xbdmConsolePath(path);
    if (!consolePath)
        return consolePath.status();
    const std::string& cp = consolePath.value();
    if (xbdm::isDriveRoot(cp))
        return Status::failure("Cannot create a drive: " + path);
    if (Status st = checkXboxName(*xbdm::nameOf(cp)); !st)
        return st;
    const std::string what = "Cannot create the folder " + path;
    auto lease = impl.acquire(what);
    if (!lease)
        return lease.status();
    auto r = impl.query(lease.value(), [&](xbdm::XbdmClient& c) { return c.makeDirectory(cp); });
    if (!r) {
        if (xbdm::consoleStatusCode(r.error()) == xbdm::status::kAlreadyExists)
            return Status::failure("Already exists: " + path);
        return impl.pathFailure(lease.value(), r.error(), what, cp);
    }
    return Status::success();
}

Status XbdmFileSystem::rename(const std::string& path, const std::string& newName) {
    Impl& impl = *m_impl;
    auto consolePath = xbdmConsolePath(path);
    if (!consolePath)
        return consolePath.status();
    const std::string& cp = consolePath.value();
    if (xbdm::isDriveRoot(cp))
        return Status::failure("Cannot rename a drive: " + path);
    if (Status st = checkXboxName(newName); !st)
        return st;
    const std::string oldName = *xbdm::nameOf(cp);
    if (newName == oldName)
        return Status::success();
    auto target = xbdm::joinPath(*xbdm::parentOf(cp), newName);
    if (!target)
        return Status::failure("Cannot rename " + path + ": " + target.error().message);
    const std::string what = "Cannot rename " + path + " to " + newName;
    auto lease = impl.acquire(what);
    if (!lease)
        return lease.status();
    Lease& l = lease.value();

    if (sameName(oldName, newName)) {
        // Only the case changes, and the console compares names without case:
        // through a temporary name.
        char tempName[32];
        std::snprintf(tempName, sizeof tempName, "~ren%08x.tmp", static_cast<unsigned>(std::random_device{}()));
        auto temp = xbdm::joinPath(*xbdm::parentOf(cp), tempName);
        if (auto r = impl.query(l, [&](xbdm::XbdmClient& c) { return c.rename(cp, *temp); }); !r)
            return impl.pathFailure(l, r.error(), what, cp);
        if (auto r = l.client().rename(*temp, *target); !r) {
            Status st = impl.failure(r.error(), what);
            if (!l.client().rename(*temp, cp))
                st.message += "; it is left as " + virtualOf(*temp);
            return st;
        }
        return Status::success();
    }

    auto existing = impl.lookUp(l, *target, false);
    if (!existing)
        return impl.pathFailure(l, existing.error(), what, *target);
    if (*existing)
        return Status::failure("Already exists: " + newName);
    if (auto r = impl.query(l, [&](xbdm::XbdmClient& c) { return c.rename(cp, *target); }); !r)
        return impl.pathFailure(l, r.error(), what, cp);
    return Status::success();
}

Status XbdmFileSystem::remove(const std::string& path) {
    Impl& impl = *m_impl;
    if (isRoot(path))
        return Status::failure("Cannot delete the console's root");
    auto consolePath = xbdmConsolePath(path);
    if (!consolePath)
        return consolePath.status();
    const std::string& cp = consolePath.value();
    if (xbdm::isDriveRoot(cp))
        return Status::failure("Cannot delete a drive: " + path);
    const std::string what = "Cannot delete " + path;
    auto lease = impl.acquire(what);
    if (!lease)
        return lease.status();
    Lease& l = lease.value();
    auto found = impl.lookUp(l, cp, false);
    if (!found)
        return impl.pathFailure(l, found.error(), what, cp);
    if (!*found)
        return impl.missing(l, "No such file or folder: " + path, cp);
    if (!(*found)->isDirectory) {
        if (auto r = impl.query(l, [&](xbdm::XbdmClient& c) { return c.removeFile(cp); }); !r)
            return impl.pathFailure(l, r.error(), what, cp);
        return Status::success();
    }
    return impl.removeTree(l, cp, path, true);
}

Status XbdmFileSystem::clear(const std::string& path) {
    Impl& impl = *m_impl;
    if (isRoot(path))
        return Status::failure("Cannot clear the console's root");
    auto consolePath = xbdmConsolePath(path);
    if (!consolePath)
        return consolePath.status();
    const std::string& cp = consolePath.value();
    if (xbdm::isDriveRoot(cp))
        return Status::failure("Clearing a whole drive is not offered over XBDM: " + path);
    const std::string what = "Cannot clear " + path;
    auto lease = impl.acquire(what);
    if (!lease)
        return lease.status();
    Lease& l = lease.value();
    auto found = impl.lookUp(l, cp, false);
    if (!found)
        return impl.pathFailure(l, found.error(), what, cp);
    if (!*found)
        return impl.missing(l, "No such folder: " + path, cp);
    if (!(*found)->isDirectory)
        return Status::failure(what + ": not a folder");
    return impl.removeTree(l, cp, path, false);
}

XbdmConnectionState XbdmFileSystem::connectionState() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->state;
}

std::string XbdmFileSystem::connectionError() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->state == XbdmConnectionState::Connected ? std::string{} : m_impl->stateReason;
}

std::string XbdmFileSystem::address() const { return m_impl->address; }

std::string XbdmFileSystem::consoleType() const { return m_impl->type; }

std::size_t XbdmFileSystem::openConnections() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return static_cast<std::size_t>(std::count_if(m_impl->slots.begin(), m_impl->slots.end(),
                                                  [](const std::unique_ptr<Slot>& s) { return s->client != nullptr; }));
}

Status XbdmFileSystem::reconnect() {
    Impl& impl = *m_impl;
    const std::string what = "Cannot reconnect to " + impl.name;
    auto lease = impl.acquirePrimary(what);
    if (!lease)
        return lease.status();
    Lease& l = lease.value();
    auto r = l.slot().client ? l.client().reconnect() : impl.open(l.slot());
    if (!r)
        return impl.failure(r.error(), what);
    impl.setState(XbdmConnectionState::Connected, {});
    impl.sweepLeftovers(l);
    return Status::success();
}

void XbdmFileSystem::cancel() noexcept {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& slot : m_impl->slots) {
        if (slot->busy && slot->client)
            slot->client->cancel();
    }
}

Result<std::unique_ptr<xbdm::XbdmClient>> XbdmFileSystem::openClient() const {
    auto client = xbdm::XbdmClient::open(m_impl->connector, m_impl->options.client);
    if (!client)
        return m_impl->failure(client.error(), "Cannot open a connection to " + m_impl->name);
    return std::make_unique<xbdm::XbdmClient>(std::move(*client));
}

std::vector<std::string> XbdmFileSystem::leftoverFiles() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    std::vector<std::string> out = m_impl->kept;
    out.insert(out.end(), m_impl->leftovers.begin(), m_impl->leftovers.end());
    return out;
}

Result<std::unique_ptr<FileSystem>> connectXbdm(const std::string& host, std::uint16_t port) {
    XbdmConnectOptions options;
    options.host = host;
    options.port = port;
    auto fs = XbdmFileSystem::connect(std::move(options));
    if (!fs)
        return fs.status();
    return std::unique_ptr<FileSystem>(std::move(fs.value()));
}

// --- discovery -----------------------------------------------------------------

namespace {

// Hands every receive out in short slices, so a cancel flag stops a search
// promptly.
class CancellableSocket final : public net::IDatagramSocket {
public:
    CancellableSocket(std::unique_ptr<net::IDatagramSocket> inner, std::shared_ptr<std::atomic_bool> cancel)
        : m_inner(std::move(inner)), m_cancel(std::move(cancel)) {}

    updclient::Result<void> bind(std::uint16_t port, bool reuse) override { return m_inner->bind(port, reuse); }
    updclient::Result<void> bindWith(const net::DatagramBindOptions& options) override {
        return m_inner->bindWith(options);
    }
    updclient::Result<std::size_t> sendTo(std::span<const std::uint8_t> data, std::string_view address,
                                          std::uint16_t port) override {
        if (cancelled())
            return updclient::fail(ErrorCode::Cancelled, "the search was cancelled");
        return m_inner->sendTo(data, address, port);
    }
    updclient::Result<std::optional<net::Datagram>> receive(std::chrono::milliseconds timeout) override {
        const auto end = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            if (cancelled())
                return updclient::fail(ErrorCode::Cancelled, "the search was cancelled");
            const auto left = std::chrono::ceil<std::chrono::milliseconds>(end - std::chrono::steady_clock::now());
            auto r = m_inner->receive(std::clamp(left, std::chrono::milliseconds(0), kDiscoverySlice));
            if (!r || *r || left <= kDiscoverySlice)
                return r;
        }
    }
    void close() noexcept override { m_inner->close(); }

private:
    bool cancelled() const { return m_cancel && m_cancel->load(); }

    std::unique_ptr<net::IDatagramSocket> m_inner;
    std::shared_ptr<std::atomic_bool> m_cancel;
};

} // namespace

Result<std::vector<DiscoveredConsole>> discoverConsoles(const XbdmDiscoveryOptions& options) {
    const auto cancel = options.cancel;
    const auto cancelled = [cancel] { return cancel && cancel->load(); };
    if (cancelled())
        return Status::cancelledByUser();
    const net::DatagramSocketFactory base = options.sockets ? options.sockets : net::DatagramSocketFactory(net::makeUdpSocket);
    net::DatagramSocketFactory sockets = [base, cancel]() -> std::unique_ptr<net::IDatagramSocket> {
        auto inner = base();
        if (!inner)
            return nullptr;
        return std::make_unique<CancellableSocket>(std::move(inner), cancel);
    };
    xbdm::XbdmDiscovery::Connector connector = [inner = options.connector,
                                                cancel](const net::Endpoint& endpoint)
        -> updclient::Result<net::TransportPtr> {
        if (cancel && cancel->load())
            return updclient::fail(ErrorCode::Cancelled, "the search was cancelled");
        if (inner)
            return inner(endpoint);
        return net::TcpTransport::connect(endpoint);
    };
    xbdm::XbdmDiscovery discovery(std::move(sockets), options.discovery, std::move(connector));
    auto found = discovery.discover(options.timeout, false);
    if (!found) {
        if (cancelled() || found.error().code == ErrorCode::Cancelled)
            return Status::cancelledByUser();
        return Status::failure("Cannot search for consoles: " + found.error().message);
    }
    if (found->empty() && cancelled())
        return Status::cancelledByUser();
    std::vector<DiscoveredConsole> out;
    for (const auto& device : *found) {
        DiscoveredConsole console;
        console.address = device.address;
        if (const auto it = device.info.find("name"); it != device.info.end())
            console.name = printable(it->second);
        if (const auto it = device.info.find("port"); it != device.info.end()) {
            const unsigned long port = std::strtoul(it->second.c_str(), nullptr, 10);
            if (port > 0 && port <= 0xFFFF)
                console.port = static_cast<std::uint16_t>(port);
        }
        out.push_back(std::move(console));
    }
    return out;
}

} // namespace unnamed::core
