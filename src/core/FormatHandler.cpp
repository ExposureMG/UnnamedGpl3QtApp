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

// --- names ---------------------------------------------------------------------

Status checkPlainName(const std::string& name) {
    const auto invalid = [&] { return Status::failure("“" + name + "” is not a valid file name"); };
    if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ')
        return invalid();
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F || std::strchr("<>:\"/\\|?*", c))
            return invalid();
    }
    // Windows opens a device for these, whatever the extension.
    std::string base = name.substr(0, name.find('.'));
    while (!base.empty() && base.back() == ' ')
        base.pop_back();
    for (char& c : base)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    static const char* const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"};
    for (const char* device : devices) {
        if (base == device)
            return invalid();
    }
    if (base.size() == 4 && (base.starts_with("COM") || base.starts_with("LPT")) && base[3] >= '1' && base[3] <= '9')
        return invalid();
    return Status::success();
}

Status checkOutputs(const OperationDescriptor& operation, const Parameters& values, const OperationIo& io) {
    for (const ParameterDescriptor& p : operation.parameters) {
        if (p.kind != ParameterKind::OutputFile || !isParameterActive(operation, values, p.id))
            continue;
        const auto it = values.find(p.id);
        if (it == values.end() || !std::holds_alternative<std::string>(it->second) ||
            !io.isSource(std::get<std::string>(it->second)))
            continue;
        std::string message = (p.label.empty() ? p.id : p.label) + " is the file being processed: choose another file";
        // Name the choice that rewrites the source, where there is one.
        const Condition& when = operation.modifiesSourceWhen;
        const ParameterDescriptor* choice = operation.modifiesSource ? findParameter(operation, when.parameter) : nullptr;
        if (choice && choice->kind == ParameterKind::Choice && !when.values.empty()) {
            for (const ChoiceOption& o : choice->options) {
                if (o.id == std::get<std::string>(when.values.front()))
                    message += ", or “" + o.label + "” to change it";
            }
        }
        return Status::failure(message);
    }
    return Status::success();
}

// --- host I/O ------------------------------------------------------------------

namespace {

Result<std::unique_ptr<ByteSink>> replaceThrough(FileSystem* fs, const std::string& path,
                                                 std::optional<std::uint64_t> size) {
    if (!fs || !hasCapability(fs->capabilities(), Capability::Replace))
        return Status::failure("The file cannot be changed here");
    return fs->openWrite(path, size, true);
}

// The same host file, through links too; a missing one is no file's.
bool sameHostFile(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    return std::filesystem::equivalent(a, b, ec) && !ec;
}

// "/A//b/./c/" -> "a/b/c": virtual paths compared without case, as FATX does.
std::string comparablePath(const std::string& path) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find('/', start), path.size());
        std::string part = path.substr(start, end - start);
        if (part == "..") {
            if (!parts.empty())
                parts.pop_back();
        } else if (!part.empty() && part != ".") {
            for (char& c : part)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            parts.push_back(std::move(part));
        }
        start = end + 1;
    }
    std::string out;
    for (const std::string& part : parts)
        out += "/" + part;
    return out;
}

const char* const kOutputIsSource = "The output is the file being processed";

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
    if (name.empty()) {
        if (isSource(location))
            return Status::failure(kOutputIsSource);
        return openFileSink(pathFromUtf8(location), true);
    }
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

bool HostOperationIo::isSource(const std::string& location) const {
    const auto source = m_sourceFs ? m_sourceFs->hostPath(m_sourcePath) : std::nullopt;
    return source && !location.empty() && sameHostFile(pathFromUtf8(location), *source);
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
    if (name.empty()) {
        if (isSource(location))
            return Status::failure(kOutputIsSource);
        return m_target.openWrite(location, size, true);
    }
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

bool FileSystemOperationIo::isSource(const std::string& location) const {
    if (!m_sourceFs || location.empty())
        return false;
    const auto target = m_target.hostPath(location);
    const auto source = m_sourceFs->hostPath(m_sourcePath);
    if (target && source)
        return sameHostFile(*target, *source);
    return &m_target == m_sourceFs && comparablePath(location) == comparablePath(m_sourcePath);
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
    if (const Status st = checkOutputs(*op, parameters, io); !st)
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
