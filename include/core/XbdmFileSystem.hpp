#pragma once

// An Xbox 360 console over XBDM, the debug monitor on TCP port 730, through
// UpdClient's protocol client (extern/UpdClient). Only built with
// UNNAMED_WITH_XBDM, which is defined when that submodule is present. Every
// target that speaks XBDM (devkits, retail consoles with an XBDM plugin,
// emulators) is handled alike.
//
// Paths. The console's drives are the top-level folders: the virtual path
// "/HDD/Content/a.bin" is the console path "HDD:\Content\a.bin", converted
// with UpdClient's own helpers (xbdmConsolePath, xbdmVirtualPath).
//
// Connections. A place has one command connection, opened by connect() and
// used by the place's worker. A file transfer owns the connection it was
// opened on until its stream ends or is destroyed. A call that finds every
// open connection busy (a listing while a transfer runs, a copy from the
// console to itself) opens another one to the same console, closed again when
// that call or transfer ends; at most maxConnections are open at once. The
// console has a limit of its own, unknown, and refuses a connection beyond it
// with 401, which is reported as "connection limit reached". Unlike the other
// backends, this one may therefore be called from several threads at once.
//
// A connection the place closed itself (a cancelled or abandoned transfer) is
// opened again before its next use. A command that finds its connection
// dropped by the console is sent again once, on a new connection. That cannot
// change anything twice: a second new folder, delete or rename of the same
// name is refused. The drop may have come after the console carried out the
// first one, so that refusal counts as success when the console shows the
// change done (the folder exists; the file is gone; the old name is gone and
// the new one exists). File data is never sent again. A console that stops
// responding or cannot be reached on the command connection puts the place in
// the Disconnected state: every call then fails at once, instead of each
// waiting for its own timeout, until a new connection reaches the console
// (reconnect()). An extra connection that cannot be opened fails its call
// only.
//
// Writes. A new file is uploaded under a temporary name in its folder and
// renamed in finish(), so nothing appears under the final name before the
// console confirmed the data. An abandoned upload closes its connection; the
// temporary file is then deleted over a new connection, or listed by
// leftoverFiles() when that fails. Replacing a file deletes the old one right
// before the rename; if the rename then fails, or the answer to that delete
// is lost (a cancel included), the console is asked where the upload is: a
// rename that took effect is success; otherwise the result is a failure,
// never a plain cancel, that says whether the upload is kept under its
// temporary name (or may be, when the console cannot be asked), which
// leftoverFiles() then lists and which is never deleted.
//
// Known gap (UpdClient's FileWriter::finish() always replaces): with
// overwrite false the final name is checked once, before the data is sent
// (for an unknown size, in finish()); a file that another client creates
// under that name while the data is sent is deleted and replaced.

#include "core/FileSystem.hpp"

#include <net/datagram.hpp>
#include <net/endpoint.hpp>
#include <protocols/xbdm/client.hpp>
#include <protocols/xbdm/discovery.hpp>
#include <protocols/xbdm/protocol.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace unnamed::core {

enum class XbdmConnectionState { Connected, Disconnected };

struct XbdmConnectOptions {
    std::string host;
    std::uint16_t port = updclient::xbdm::kXbdmPort;
    // Bounds each TCP connect, which cannot be cancelled.
    std::chrono::milliseconds connectTimeout{5000};
    // Timeouts and limits of every connection.
    updclient::xbdm::ClientOptions client;
    // Connections this place opens at most, the command connection included.
    std::size_t maxConnections = 3;
    // The place's name; empty uses the console's debug name, or the address.
    std::string displayName;
    // Opens a connection instead of TCP to host:port (tests use the mock
    // console's in-memory connections).
    updclient::xbdm::XbdmClient::Connector connector;
    // Called whenever connectionState() changes, on the thread that noticed.
    std::function<void(XbdmConnectionState)> onStateChanged;
};

class XbdmFileSystem final : public FileSystem {
public:
    struct Impl;

    // Opens the command connection and reads the console's name.
    static Result<std::unique_ptr<XbdmFileSystem>> connect(XbdmConnectOptions options);

    ~XbdmFileSystem() override;

    std::string name() const override;
    Capability capabilities() const override;

    Status list(const std::string& path, std::vector<Entry>& out) const override;
    Status stat(const std::string& path, Entry& out) const override;
    Status describe(const std::string& path, Details& out) const override;
    Status describeFileSystem(Details& out) const override;
    Result<std::unique_ptr<ByteSource>> openRead(const std::string& path) const override;
    // Files of 4 GiB or more are refused before anything is sent (XBDM's
    // lengths have 32 bits). Without a size the data is staged on the host, in
    // memory up to 16 MiB and in a temporary file beyond, and sent in finish().
    Result<std::unique_ptr<ByteSink>> openWrite(const std::string& path, std::optional<std::uint64_t> size,
                                                bool overwrite) override;
    Status makeDirectory(const std::string& path) override;
    Status rename(const std::string& path, const std::string& newName) override;
    // A folder is emptied depth first by the client, after its whole tree was
    // listed: nothing is deleted when it is nested more than 32 levels deep,
    // holds more than 100,000 entries, or holds an entry whose name cannot be
    // sent back to the console. A drive and the root are never deleted.
    Status remove(const std::string& path) override;
    // Deletes what a folder holds, as remove() does; refused for a drive and
    // the root.
    Status clear(const std::string& path) override;

    // The rest is thread-safe.
    XbdmConnectionState connectionState() const;
    // Why the place is disconnected; empty while connected.
    std::string connectionError() const;
    // "host:port"; with a connector and no host, "console".
    std::string address() const;
    // The console's type as it answered at connect ("devkit", ...); empty
    // when it did not.
    std::string consoleType() const;
    // The console's id (getconsoleid) as it answered at connect; empty when
    // it did not.
    std::string consoleId() const;
    // Connections this place holds now (the console may have closed some).
    std::size_t openConnections() const;
    // Closes the command connection, opens it again and deletes the temporary
    // uploads left behind. Refused while a transfer uses that connection.
    Status reconnect();
    // Ends every call and transfer in progress on this place at once; they
    // fail as cancelled and their connections are opened again on next use.
    // A call still opening an extra connection is reached too, once its TCP
    // connect returns (that connect cannot be interrupted).
    void cancel() noexcept;
    // A connection of its own to the same console, outside this place's
    // connections, for tools that need the whole protocol (a memory viewer).
    // It counts against the console's connection limit. The caller owns it and
    // uses it from one thread; its cancel() works from any.
    Result<std::unique_ptr<updclient::xbdm::XbdmClient>> openClient() const;
    // Console paths of temporary uploads still on the console: ones kept
    // because they hold the only copy, then ones that could not be deleted.
    // describeFileSystem() asks the console again and drops the ones that are
    // gone.
    std::vector<std::string> leftoverFiles() const;

private:
    explicit XbdmFileSystem(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> m_impl;
};

// connect() with a host and a port, for the GUI and the registry ("XBDM",
// whose path is "host" or "host:port").
Result<std::unique_ptr<FileSystem>> connectXbdm(const std::string& host,
                                                std::uint16_t port = updclient::xbdm::kXbdmPort);

// "/HDD/a/b" -> "HDD:\a\b", "/HDD" -> "HDD:\". The root "/", which lists the
// drives, has no console path.
Result<std::string> xbdmConsolePath(const std::string& virtualPath);
// "HDD:\a\b" -> "/HDD/a/b", "HDD:\" -> "/HDD".
Result<std::string> xbdmVirtualPath(const std::string& consolePath);
// A name the app creates on a console: one UpdClient can send, as FATX
// stores it: ASCII letters, digits, spaces and ! # $ % & ' ( ) - . @ [ ] ^ _
// ` { } ~, at most 42 characters.
Status checkXboxName(const std::string& name);

struct DiscoveredConsole {
    std::string name;    // the console's debug name, or the name in its UDP reply
    std::string address; // IPv4 address
    std::uint16_t port = updclient::xbdm::kXbdmPort;
};

struct XbdmDiscoveryOptions {
    std::chrono::milliseconds timeout{3000};
    // Set from another thread to stop: the consoles found so far are
    // returned, or a cancelled Status when there are none. The search notices
    // within 50 ms, a name query in progress when it ends (a few seconds).
    std::shared_ptr<std::atomic_bool> cancel;
    // Broadcast address, UDP port and the TCP name queries.
    updclient::xbdm::DiscoveryOptions discovery;
    // Empty for a real UDP socket and TCP; tests pass the mock console's.
    updclient::net::DatagramSocketFactory sockets;
    updclient::xbdm::XbdmDiscovery::Connector connector;
};

// One search with UpdClient's XBDM discovery (UDP name protocol on port 730).
// Networks often drop the broadcast; a console that does not appear can still
// be opened by its address.
Result<std::vector<DiscoveredConsole>> discoverConsoles(const XbdmDiscoveryOptions& options = {});

} // namespace unnamed::core
