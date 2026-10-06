#pragma once

#include "core/FileSystem.hpp"
#include "core/OperationDescriptor.hpp"
#include "core/Transfer.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace unnamed::core {

// A file format with its own information and tools (XEX now, STFS later):
// the handler recognises a file from a small probe, adds property groups to
// its expanded view and offers single-file operations that read the file as a
// stream. Where the file lives (host folder, FATX drive, console, ...) is the
// FileSystem's business, not the handler's.

constexpr std::size_t kProbeSize = 4096;

// What a handler sees to decide whether it applies: name, size, first bytes.
struct FileProbe {
    std::string name;
    std::uint64_t size = 0;
    std::vector<std::uint8_t> head; // the first kProbeSize bytes, fewer for a short file

    // Lowercase extension without the dot; empty if none.
    std::string extension() const;
    bool startsWith(std::string_view magic) const;
};

// Reads the probe of the file at `path` (Capability::Extract).
Result<FileProbe> probeFile(const FileSystem& fs, const std::string& path);

// probe() results; higher is a better match.
constexpr int kMatchNone = 0;     // not this format
constexpr int kMatchAnyFile = 1;  // generic tools that work on any file
constexpr int kMatchName = 50;    // the extension fits, the content was not checked
constexpr int kMatchContent = 100; // the magic or header fits

// Where an operation reads extra inputs and writes its results. Locations are
// the values of InputFile, OutputFile and OutputFolder parameters; what they
// mean (host paths, paths in a FileSystem) is up to the implementation.
class OperationIo {
public:
    virtual ~OperationIo() = default;
    virtual Result<std::unique_ptr<ByteSource>> openInput(const std::string& location) = 0;
    // A new file at an OutputFile `location` (`name` empty), or called `name`
    // inside an OutputFolder `location`. Nothing appears before the sink's
    // finish() succeeds.
    virtual Result<std::unique_ptr<ByteSink>> createOutput(const std::string& location, const std::string& name,
                                                           std::optional<std::uint64_t> size) = 0;
    // New contents for the source file (OperationDescriptor::modifiesSource).
    // Read all of the source before finishing this sink.
    virtual Result<std::unique_ptr<ByteSink>> replaceSource(std::optional<std::uint64_t> size) = 0;
};

// Host paths for inputs and outputs (UTF-8). An OutputFile replaces an
// existing file (the save dialog asked); a file in an OutputFolder does not,
// and its name must be a plain name. A missing OutputFolder is created. The source is replaced through its
// FileSystem (`sourceFs` may be null when nothing may be replaced).
class HostOperationIo final : public OperationIo {
public:
    HostOperationIo(FileSystem* sourceFs, std::string sourcePath)
        : m_sourceFs(sourceFs), m_sourcePath(std::move(sourcePath)) {}

    Result<std::unique_ptr<ByteSource>> openInput(const std::string& location) override;
    Result<std::unique_ptr<ByteSink>> createOutput(const std::string& location, const std::string& name,
                                                   std::optional<std::uint64_t> size) override;
    Result<std::unique_ptr<ByteSink>> replaceSource(std::optional<std::uint64_t> size) override;

private:
    FileSystem* m_sourceFs;
    std::string m_sourcePath;
};

// Paths inside a FileSystem for inputs and outputs (a FATX place, another
// host folder, ...), with the same rules as HostOperationIo: an OutputFile
// replaces an existing file, a file in an OutputFolder does not and must have
// a plain name, and a missing OutputFolder is created (its parent must exist). Both filesystems must outlive the object.
class FileSystemOperationIo final : public OperationIo {
public:
    FileSystemOperationIo(FileSystem& target, FileSystem* sourceFs, std::string sourcePath)
        : m_target(target), m_sourceFs(sourceFs), m_sourcePath(std::move(sourcePath)) {}

    Result<std::unique_ptr<ByteSource>> openInput(const std::string& location) override;
    Result<std::unique_ptr<ByteSink>> createOutput(const std::string& location, const std::string& name,
                                                   std::optional<std::uint64_t> size) override;
    Result<std::unique_ptr<ByteSink>> replaceSource(std::optional<std::uint64_t> size) override;

private:
    FileSystem& m_target;
    FileSystem* m_sourceFs;
    std::string m_sourcePath;
};

// Everything a running operation gets. Parameters are validated and complete
// (defaults filled in), so the typed getters cannot fail for declared ids.
class OperationContext {
public:
    OperationContext(ByteSource& source, const FileProbe& probe, const OperationDescriptor& operation,
                     const Parameters& parameters, OperationIo& io, ProgressFn progress = {})
        : m_source(source), m_probe(probe), m_operation(operation), m_parameters(parameters), m_io(io),
          m_progress(std::move(progress)) {}

    ByteSource& source() { return m_source; }
    const FileProbe& probe() const { return m_probe; }
    const std::string& sourceName() const { return m_probe.name; }
    std::uint64_t sourceSize() const { return m_probe.size; }
    const OperationDescriptor& operation() const { return m_operation; }
    const Parameters& parameters() const { return m_parameters; }
    OperationIo& io() { return m_io; }

    bool flag(const std::string& id) const;
    std::int64_t integer(const std::string& id) const;
    // Choice, Text and file parameters.
    const std::string& text(const std::string& id) const;
    bool active(const std::string& id) const;

    // Streams for file parameters (by parameter id).
    Result<std::unique_ptr<ByteSource>> openInput(const std::string& id);
    Result<std::unique_ptr<ByteSink>> createOutputFile(const std::string& id, std::optional<std::uint64_t> size);
    Result<std::unique_ptr<ByteSink>> createInFolder(const std::string& id, const std::string& name,
                                                     std::optional<std::uint64_t> size);

    // Reports progress; false means the user cancelled, so stop and return
    // Status::cancelledByUser() (sinks dropped unfinished leave nothing).
    bool progress(std::uint64_t done, std::uint64_t total, const std::string& current = {});

private:
    ByteSource& m_source;
    const FileProbe& m_probe;
    const OperationDescriptor& m_operation;
    const Parameters& m_parameters;
    OperationIo& m_io;
    ProgressFn m_progress;
};

// Reads a whole stream into memory, refusing more than `limit` bytes.
Result<std::vector<std::uint8_t>> readAll(ByteSource& source, std::uint64_t limit, OperationContext* progress = nullptr);

// Handlers are stateless: every method may be called from several worker
// threads at once.
class FormatHandler {
public:
    virtual ~FormatHandler() = default;

    virtual std::string id() const = 0;   // "xex"
    virtual std::string name() const = 0; // "Xbox 360 executable"
    // One of the kMatch* values (or anything between): how well the file fits.
    virtual int probe(const FileProbe& file) const = 0;
    // Property groups for the file's expanded view; none by default.
    virtual Status describe(ByteSource& source, const FileProbe& file, std::vector<PropertyGroup>& out) const;
    virtual std::vector<OperationDescriptor> operations() const = 0;
    // Runs a validated operation; the result is a report for the user (may be empty).
    virtual Result<std::string> run(const std::string& operationId, OperationContext& context) const = 0;
};

// The handler's operations that apply to this file.
std::vector<OperationDescriptor> applicableOperations(const FormatHandler& handler, const FileProbe& file);

// Probes the file at `path`, checks that the operation applies to it,
// validates `parameters` and runs the operation with the file as its source.
// Operations that modify the source need Capability::Replace on `fs`.
Result<std::string> runOperation(const FormatHandler& handler, const std::string& operationId, const FileSystem& fs,
                                 const std::string& path, Parameters parameters, OperationIo& io,
                                 const ProgressFn& progress = {});

} // namespace unnamed::core
