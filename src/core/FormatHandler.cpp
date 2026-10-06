#include "core/FormatHandler.hpp"
#include "core/PathUtil.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <system_error>

namespace unnamed::core {

std::string FileProbe::extension() const {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0)
        return {};
    std::string ext = name.substr(dot + 1);
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

bool FileProbe::startsWith(std::string_view magic) const {
    return head.size() >= magic.size() && std::memcmp(head.data(), magic.data(), magic.size()) == 0;
}

Result<FileProbe> probeFile(const FileSystem& fs, const std::string& path) {
    Entry entry;
    if (const Status st = fs.stat(path, entry); !st)
        return st;
    if (entry.type != EntryType::File)
        return Status::failure("Not a file: " + path);
    auto in = fs.openRead(path);
    if (!in)
        return in.status();

    FileProbe probe;
    probe.name = entry.name.empty() ? path.substr(path.find_last_of('/') + 1) : entry.name;
    probe.size = entry.size;
    probe.head.resize(kProbeSize);
    std::size_t filled = 0;
    while (filled < probe.head.size()) {
        auto n = in.value()->read(probe.head.data() + filled, probe.head.size() - filled);
        if (!n)
            return n.status();
        if (n.value() == 0)
            break;
        filled += n.value();
    }
    probe.head.resize(filled);
    return probe;
}

// --- host I/O ------------------------------------------------------------------

namespace {

// Names come from the file being processed, which may be hostile.
Status checkPlainName(const std::string& name) {
    if (name == "." || name == ".." || name.find_first_of("/\\:") != std::string::npos ||
        name.find('\0') != std::string::npos)
        return Status::failure("“" + name + "” is not a valid file name");
    return Status::success();
}

Result<std::unique_ptr<ByteSink>> replaceThrough(FileSystem* fs, const std::string& path,
                                                 std::optional<std::uint64_t> size) {
    if (!fs || !hasCapability(fs->capabilities(), Capability::Replace))
        return Status::failure("The file cannot be changed here");
    return fs->openWrite(path, size, true);
}

} // namespace

Result<std::unique_ptr<ByteSource>> HostOperationIo::openInput(const std::string& location) {
    if (location.empty())
        return Status::failure("No input file chosen");
    return openFileSource(pathFromUtf8(location));
}

Result<std::unique_ptr<ByteSink>> HostOperationIo::createOutput(const std::string& location, const std::string& name,
                                                                std::optional<std::uint64_t>) {
    if (location.empty())
        return Status::failure("No output chosen");
    if (name.empty())
        return openFileSink(pathFromUtf8(location), true);
    if (const Status st = checkPlainName(name); !st)
        return st;
    const std::filesystem::path folder = pathFromUtf8(location);
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec) && !std::filesystem::create_directories(folder, ec))
        return Status::failure("Cannot create the folder " + location + (ec ? ": " + ec.message() : ""));
    return openFileSink(folder / pathFromUtf8(name), false);
}

Result<std::unique_ptr<ByteSink>> HostOperationIo::replaceSource(std::optional<std::uint64_t> size) {
    return replaceThrough(m_sourceFs, m_sourcePath, size);
}

// --- I/O inside a filesystem -----------------------------------------------------

Result<std::unique_ptr<ByteSource>> FileSystemOperationIo::openInput(const std::string& location) {
    if (location.empty())
        return Status::failure("No input file chosen");
    return m_target.openRead(location);
}

Result<std::unique_ptr<ByteSink>> FileSystemOperationIo::createOutput(const std::string& location,
                                                                      const std::string& name,
                                                                      std::optional<std::uint64_t> size) {
    if (location.empty())
        return Status::failure("No output chosen");
    if (name.empty())
        return m_target.openWrite(location, size, true);
    if (const Status st = checkPlainName(name); !st)
        return st;
    if (Entry entry; !m_target.stat(location, entry)) {
        if (const Status st = m_target.makeDirectory(location); !st)
            return st;
    }
    return m_target.openWrite(joinPath(location, name), size, false);
}

Result<std::unique_ptr<ByteSink>> FileSystemOperationIo::replaceSource(std::optional<std::uint64_t> size) {
    return replaceThrough(m_sourceFs, m_sourcePath, size);
}

// --- running -------------------------------------------------------------------

bool OperationContext::flag(const std::string& id) const { return std::get<bool>(m_parameters.at(id)); }

std::int64_t OperationContext::integer(const std::string& id) const {
    return std::get<std::int64_t>(m_parameters.at(id));
}

const std::string& OperationContext::text(const std::string& id) const {
    return std::get<std::string>(m_parameters.at(id));
}

bool OperationContext::active(const std::string& id) const {
    return isParameterActive(m_operation, m_parameters, id);
}

Result<std::unique_ptr<ByteSource>> OperationContext::openInput(const std::string& id) {
    return m_io.openInput(text(id));
}

Result<std::unique_ptr<ByteSink>> OperationContext::createOutputFile(const std::string& id,
                                                                     std::optional<std::uint64_t> size) {
    return m_io.createOutput(text(id), {}, size);
}

Result<std::unique_ptr<ByteSink>> OperationContext::createInFolder(const std::string& id, const std::string& name,
                                                                   std::optional<std::uint64_t> size) {
    if (name.empty())
        return Status::failure("Empty file name");
    return m_io.createOutput(text(id), name, size);
}

bool OperationContext::progress(std::uint64_t done, std::uint64_t total, const std::string& current) {
    if (!m_progress)
        return true;
    TransferProgress p;
    p.bytesDone = done;
    p.bytesTotal = total;
    p.current = current.empty() ? m_probe.name : current;
    return m_progress(p);
}

Result<std::vector<std::uint8_t>> readAll(ByteSource& source, std::uint64_t limit, OperationContext* progress) {
    std::vector<std::uint8_t> data;
    const auto known = source.size();
    if (known && *known > limit)
        return Status::failure("The file is too large (" + std::to_string(*known) + " bytes, at most " +
                               std::to_string(limit) + ")");
    if (known)
        data.reserve(static_cast<std::size_t>(*known));
    std::vector<std::uint8_t> chunk(1 << 20);
    for (;;) {
        if (progress && !progress->progress(data.size(), known.value_or(0)))
            return Status::cancelledByUser();
        auto n = source.read(chunk.data(), chunk.size());
        if (!n)
            return n.status();
        if (n.value() == 0)
            break;
        if (data.size() + n.value() > limit)
            return Status::failure("The file is too large (more than " + std::to_string(limit) + " bytes)");
        data.insert(data.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n.value()));
    }
    return data;
}

Status FormatHandler::describe(ByteSource&, const FileProbe&, std::vector<PropertyGroup>& out) const {
    out.clear();
    return Status::success();
}

std::vector<OperationDescriptor> applicableOperations(const FormatHandler& handler, const FileProbe& file) {
    std::vector<OperationDescriptor> out;
    if (handler.probe(file) <= kMatchNone)
        return out;
    for (OperationDescriptor& op : handler.operations()) {
        if (!op.appliesTo || op.appliesTo(file))
            out.push_back(std::move(op));
    }
    return out;
}

Result<std::string> runOperation(const FormatHandler& handler, const std::string& operationId, const FileSystem& fs,
                                 const std::string& path, Parameters parameters, OperationIo& io,
                                 const ProgressFn& progress) {
    auto probe = probeFile(fs, path);
    if (!probe)
        return probe.status();
    const auto ops = applicableOperations(handler, probe.value());
    const auto op = std::find_if(ops.begin(), ops.end(),
                                 [&](const OperationDescriptor& o) { return o.id == operationId; });
    if (op == ops.end())
        return Status::failure(handler.name() + ": “" + operationId + "” does not apply to " + probe->name);
    if (const Status st = validateParameters(*op, parameters); !st)
        return st;
    if (rewritesSource(*op, parameters) && !hasCapability(fs.capabilities(), Capability::Replace))
        return Status::failure(op->name + " changes the file, which " + fs.name() + " does not allow");

    auto source = fs.openRead(path);
    if (!source)
        return source.status();
    OperationContext context(*source.value(), probe.value(), *op, parameters, io, progress);
    if (!context.progress(0, probe->size))
        return Status::cancelledByUser();
    return handler.run(operationId, context);
}

} // namespace unnamed::core
