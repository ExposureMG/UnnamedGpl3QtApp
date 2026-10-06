#include "core/XexHandler.hpp"

#include "XexApi.h"

#include <chrono>
#include <cstdio>
#include <functional>
#include <iterator>
#include <optional>
#include <utility>

namespace unnamed::core {

namespace {

namespace xt = xextool;

// --- formatting -----------------------------------------------------------------

std::string hex32(std::uint32_t value) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08X", value);
    return buf;
}

std::string hexBytes(const xt::Bytes& bytes) {
    std::string out;
    char buf[4];
    for (const std::uint8_t b : bytes) {
        std::snprintf(buf, sizeof buf, out.empty() ? "%02X" : " %02X", b);
        out += buf;
    }
    return out;
}

std::string versionText(const xt::Version& v) {
    return "v" + std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.build) + "." +
           std::to_string(v.qfe);
}

std::string byteCount(std::uint64_t n) {
    char buf[32];
    if (n < 1024)
        return std::to_string(n) + (n == 1 ? " byte" : " bytes");
    if (n < (1u << 20))
        std::snprintf(buf, sizeof buf, n % 1024 ? "%.1f KiB" : "%.0f KiB", double(n) / 1024.0);
    else
        std::snprintf(buf, sizeof buf, n % (1u << 20) ? "%.1f MiB" : "%.0f MiB", double(n) / double(1u << 20));
    return buf;
}

std::string sizeText(std::uint32_t n) { return hex32(n) + " (" + byteCount(n) + ")"; }

std::string utcTime(std::int64_t unixSeconds) {
    using namespace std::chrono;
    const sys_seconds t{seconds{unixSeconds}};
    const auto day = floor<days>(t);
    const year_month_day ymd{day};
    const hh_mm_ss hms{t - day};
    char buf[48];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u %02d:%02d:%02d UTC", int(ymd.year()), unsigned(ymd.month()),
                  unsigned(ymd.day()), int(hms.hours().count()), int(hms.minutes().count()),
                  int(hms.seconds().count()));
    return buf;
}

// A Windows FILETIME (100 ns steps since 1601).
std::string fileTimeText(std::uint64_t value) {
    return utcTime(static_cast<std::int64_t>(value / 10000000) - 11644473600LL);
}

// "4E4D07D0 (NM-2000)": two publisher letters and a number, as XexTool shows it.
std::string titleIdText(std::uint32_t id) {
    char buf[32];
    const char a = static_cast<char>(id >> 24), b = static_cast<char>(id >> 16);
    if (a >= 0x20 && a < 0x7F && b >= 0x20 && b < 0x7F)
        std::snprintf(buf, sizeof buf, "%08X (%c%c-%u)", id, a, b, id & 0xFFFFu);
    else
        std::snprintf(buf, sizeof buf, "%08X", id);
    return buf;
}

std::string join(const std::vector<std::string>& items, const char* separator = ", ") {
    std::string out;
    for (const std::string& s : items)
        out += (out.empty() ? "" : separator) + s;
    return out;
}

std::string machineText(const xt::XexInfo& info) {
    if (info.machine == xt::Machine::Devkit)
        return "Devkit";
    return info.signatureCleared ? "Retail (signature cleared)" : "Retail";
}

std::string compressionText(xt::Compression c) {
    switch (c) {
    case xt::Compression::Normal: return "Normal (LZX compressed)";
    case xt::Compression::Basic: return "Basic (zeros left out)";
    case xt::Compression::Uncompressed: return "Uncompressed";
    case xt::Compression::Delta: return "Delta patch";
    case xt::Compression::Keep: break;
    }
    return "Unknown";
}

// What an edit wrote, in the words of XexTool's last line.
std::string resultText(const xt::EditResult& r) {
    return std::string(r.machine == xt::Machine::Devkit ? "devkit" : "retail") + ", " +
           (r.encrypted ? "encrypted" : "not encrypted") + ", " + compressionText(r.compression);
}

// --- reading and writing ---------------------------------------------------------

Status apiFailure(const xt::Status& st) {
    return Status::failure(st.error.empty() ? std::string("XexTool failed") : st.error);
}

std::string tooLarge(std::uint64_t size) {
    return "The file is too large for the XEX tools (" + byteCount(size) + "; at most " +
           byteCount(XexHandler::kMaxSize) + ")";
}

// Bytes for XexTool, named for its messages: XexTool names an input by its
// path, and reads the path only when it gets no bytes. Without a name its
// errors say "xex" and "patch".
xt::Input memoryInput(const xt::Bytes& data, const std::string& location) {
    xt::Input input = xt::Input::memory(data);
    input.path = location.substr(location.find_last_of("/\\") + 1);
    return input;
}

// The whole source, which XexTool needs in memory.
Result<xt::Bytes> readSource(OperationContext& ctx) {
    if (ctx.sourceSize() > XexHandler::kMaxSize)
        return Status::failure(tooLarge(ctx.sourceSize()));
    return readAll(ctx.source(), XexHandler::kMaxSize, &ctx);
}

Result<xt::Bytes> readInput(OperationContext& ctx, const std::string& id) {
    auto in = ctx.openInput(id);
    if (!in)
        return in.status();
    return readAll(*in.value(), XexHandler::kMaxSize);
}

// XexTool's calls cannot be interrupted, so cancelling is checked around them.
bool keepGoing(OperationContext& ctx, const std::string& step) {
    return ctx.progress(ctx.sourceSize(), ctx.sourceSize(), ctx.sourceName() + ": " + step);
}

Status writeAll(Result<std::unique_ptr<ByteSink>> sink, const xt::Bytes& bytes) {
    if (!sink)
        return sink.status();
    if (const Status st = sink.value()->write(bytes.data(), bytes.size()); !st)
        return st;
    return sink.value()->finish();
}

// A new xex goes to the chosen file, or over the source when that was chosen.
Status writeXex(OperationContext& ctx, const xt::Bytes& bytes) {
    if (ctx.active("output"))
        return writeAll(ctx.createOutputFile("output", bytes.size()), bytes);
    return writeAll(ctx.io().replaceSource(bytes.size()), bytes);
}

std::string writtenTo(OperationContext& ctx) {
    return ctx.active("output") ? ctx.text("output") : ctx.sourceName();
}

using EditCall = std::function<xt::Status(const xt::Input&, const xt::Output&, xt::EditResult*)>;

// Reads the source, runs one XexTool edit into memory and writes the result.
Result<std::string> runEdit(OperationContext& ctx, const EditCall& call, const std::string& note = {}) {
    auto data = readSource(ctx);
    if (!data)
        return data.status();
    if (!keepGoing(ctx, "processing"))
        return Status::cancelledByUser();
    xt::Bytes out;
    xt::EditResult result;
    const xt::Status st = call(memoryInput(data.value(), ctx.sourceName()), xt::Output::memory(out), &result);
    if (!st)
        return apiFailure(st);
    if (!keepGoing(ctx, "writing"))
        return Status::cancelledByUser();
    if (const Status written = writeXex(ctx, out); !written)
        return written;
    return st.log + "Wrote " + writtenTo(ctx) + ": " + resultText(result) + ".\n" + note;
}

// --- operations -------------------------------------------------------------------

const std::vector<std::string> kXexFilters = {"Xbox 360 executables (*.xex)", "All files (*)"};
const std::vector<std::string> kTextFilters = {"Text files (*.txt)", "All files (*)"};

bool isXex2(const FileProbe& file) { return file.startsWith("XEX2"); }

std::uint32_t moduleFlags(const FileProbe& file) {
    if (file.head.size() < 8)
        return 0;
    const auto* p = file.head.data() + 4;
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | std::uint32_t(p[3]);
}

ParameterDescriptor makeParameter(std::string id, ParameterKind kind, std::string label, ParameterValue defaultValue,
                                  std::string help = {}) {
    ParameterDescriptor p;
    p.id = std::move(id);
    p.kind = kind;
    p.label = std::move(label);
    p.defaultValue = std::move(defaultValue);
    p.help = std::move(help);
    return p;
}

OperationDescriptor makeOperation(std::string id, std::string name, std::string description) {
    OperationDescriptor op;
    op.id = std::move(id);
    op.name = std::move(name);
    op.description = std::move(description);
    op.appliesTo = isXex2;
    return op;
}

ParameterDescriptor outputFile(std::string suggestedName, std::vector<std::string> filters) {
    auto p = makeParameter("output", ParameterKind::OutputFile, "Output file", std::string{});
    p.required = true;
    p.suggestedName = std::move(suggestedName);
    p.nameFilters = std::move(filters);
    return p;
}

// Where an edited xex goes: a new file (the default) or over the source.
void addDestination(OperationDescriptor& op, const std::string& suggestedName) {
    auto target = makeParameter("target", ParameterKind::Choice, "Write to", std::string{"new"},
                                "In place replaces the file only once the new one is complete. Keep a copy if it "
                                "matters.");
    target.options = {{"new", "A new file"}, {"source", "This file (in place)"}};
    auto output = outputFile(suggestedName, kXexFilters);
    output.visibleWhen = {"target", {std::string{"new"}}};
    op.parameters.push_back(std::move(target));
    op.parameters.push_back(std::move(output));
    op.modifiesSource = true;
    op.modifiesSourceWhen = {"target", {std::string{"source"}}};
}

OperationDescriptor infoOperation() {
    auto op = makeOperation("info", "Info",
                            "Shows XexTool's information about the executable, and can save it as a text file.");
    auto detail = makeParameter("detail", ParameterKind::Choice, "Detail", std::string{"full"},
                                "The full report is what XexTool -l prints; the summary is what XexTool prints for "
                                "a file given without options.");
    detail.options = {{"full", "Full report"}, {"summary", "Summary"}};
    auto save = makeParameter("save", ParameterKind::Boolean, "Save to a file", false);
    auto output = outputFile("{stem}.txt", kTextFilters);
    output.visibleWhen = {"save", {true}};
    op.parameters = {detail, save, output};
    return op;
}

OperationDescriptor basefileOperation() {
    auto op = makeOperation("basefile", "Extract Basefile",
                            "Writes the basefile, the PE image as it is loaded into memory (XexTool -b). Load it in "
                            "a disassembler as a binary file at the load address, not as a PE.");
    op.parameters = {outputFile("{stem}.bin", {"Basefiles (*.bin)", "All files (*)"})};
    return op;
}

OperationDescriptor idcOperation() {
    auto op = makeOperation("idc", "Extract IDC Script",
                            "Writes an IDA script that describes the basefile: sections, imports and exports "
                            "(XexTool -i). Only for PE basefiles.");
    auto output = outputFile("{stem}.idc", {"IDA scripts (*.idc)", "All files (*)"});
    output.help = "Lines end in LF on every system; XexTool on Windows writes CRLF.";
    op.parameters = {output};
    return op;
}

OperationDescriptor resourcesOperation() {
    auto op = makeOperation("resources", "Extract Resources",
                            "Writes each resource (title data, images, string tables) as a file named after it "
                            "(XexTool -d).");
    auto folder = makeParameter("folder", ParameterKind::OutputFolder, "Folder", std::string{},
                                "Existing files are not replaced. If the run stops part way, the resources written "
                                "so far stay.");
    folder.required = true;
    folder.suggestedName = "{stem}_resources";
    op.parameters = {folder};
    return op;
}

OperationDescriptor decryptOperation() {
    auto op = makeOperation("decrypt", "Decrypt",
                            "Writes the executable with its basefile decrypted (XexTool -e u). Signing, compression "
                            "and the rest stay as they are.");
    addDestination(op, "{stem}.decrypted.xex");
    return op;
}

OperationDescriptor encryptOperation() {
    auto op = makeOperation("encrypt", "Encrypt",
                            "Writes the executable with its basefile encrypted with its image key (XexTool -e e).");
    addDestination(op, "{stem}.encrypted.xex");
    return op;
}

OperationDescriptor compressionOperation() {
    auto op = makeOperation("compression", "Compress / Decompress",
                            "Writes the executable with its basefile stored another way (XexTool -c).");
    auto format = makeParameter(
        "format", ParameterKind::Choice, "Storage", std::string{"normal"},
        "Normal compresses the basefile with LZX (XexTool -c c). Basic stores it without compression but leaves "
        "out runs of zeros; XexTool calls this \"uncompressed\" (-c u). Uncompressed stores every byte; XexTool "
        "calls this \"binary\" (-c b). Basic and Uncompressed are the same basefile format, so a file written "
        "Uncompressed shows as Basic afterwards.");
    format.options = {{"normal", "Normal (LZX compressed)"},
                      {"basic", "Basic (zeros left out)"},
                      {"uncompressed", "Uncompressed (every byte stored)"}};
    op.parameters = {format};
    addDestination(op, "{stem}.out.xex");
    return op;
}

OperationDescriptor machineOperation() {
    auto op = makeOperation("machine", "Sign (Devkit / Retail)",
                            "Writes the executable for a development kit or a retail console (XexTool -m).");
    auto machine = makeParameter(
        "machine", ParameterKind::Choice, "Target machine", std::string{"devkit"},
        "Devkit signs the file with the devkit key, for development kits. Retail clears the signature: no retail "
        "private key exists, so the signature is written as all zeros, and the file loads only on consoles that "
        "skip the signature check (a patched console), never on an unmodified retail one.");
    machine.options = {{"devkit", "Devkit (signed with the devkit key)"},
                       {"retail", "Retail / patched console (signature cleared)"}};
    op.parameters = {machine};
    addDestination(op, "{stem}.signed.xex");
    return op;
}

OperationDescriptor patchOperation() {
    auto op = makeOperation("patch", "Apply Patch",
                            "Applies a delta patch (.xexp, as title updates ship) made for this executable "
                            "(XexTool -p).");
    op.appliesTo = [](const FileProbe& file) {
        constexpr std::uint32_t kPatchFlags = 0x10 | 0x20 | 0x40; // patch module, full patch, delta patch
        return isXex2(file) && (moduleFlags(file) & kPatchFlags) == 0;
    };
    auto patch = makeParameter("patch", ParameterKind::InputFile, "Patch file", std::string{},
                               "XexTool refuses a patch made for another executable.");
    patch.required = true;
    patch.nameFilters = {"Delta patches (*.xexp)", "All files (*)"};
    op.parameters = {patch};
    addDestination(op, "{stem}.patched.xex");
    return op;
}

struct LimitFlag {
    const char* id;
    const char* label;
    bool xt::RemoveLimits::*member;
};

const LimitFlag kLimits[] = {
    {"media", "Media limit (allow all media)", &xt::RemoveLimits::media},
    {"regions", "Region limit (allow all regions)", &xt::RemoveLimits::regions},
    {"boundingPath", "Bounding path", &xt::RemoveLimits::boundingPath},
    {"boundingDeviceId", "Bounding device id", &xt::RemoveLimits::boundingDeviceId},
    {"consoleIds", "Console id restriction", &xt::RemoveLimits::consoleIds},
    {"dates", "Date restriction", &xt::RemoveLimits::dates},
    {"keyvaultPrivileges", "Keyvault privilege restriction", &xt::RemoveLimits::keyvaultPrivileges},
    {"signedKeyvaultOnly", "Signed keyvault only", &xt::RemoveLimits::signedKeyvaultOnly},
    {"libraryVersions", "Minimum library versions", &xt::RemoveLimits::libraryVersions},
    {"revocationCheck", "Required revocation check", &xt::RemoveLimits::revocationCheck},
    {"discSwapChecks", "Disc swap checks", &xt::RemoveLimits::discSwapChecks},
    {"mediaId", "Media id (set to zero)", &xt::RemoveLimits::mediaId},
};

// The limits that complete, validated values ask to remove.
xt::RemoveLimits chosenLimits(const Parameters& values) {
    if (std::get<bool>(values.at("all")))
        return xt::RemoveLimits::all();
    xt::RemoveLimits limits;
    for (const LimitFlag& limit : kLimits)
        limits.*limit.member = std::get<bool>(values.at(limit.id));
    return limits;
}

OperationDescriptor limitsOperation() {
    auto op = makeOperation("limits", "Remove Limits",
                            "Removes restrictions on where and how the executable runs (XexTool -r).");
    op.parameters.push_back(makeParameter("all", ParameterKind::Boolean, "All limits", true,
                                          "Removes every limit XexTool knows (-r a). Turn it off to choose."));
    for (const LimitFlag& limit : kLimits) {
        auto p = makeParameter(limit.id, ParameterKind::Boolean, limit.label, false);
        p.visibleWhen = {"all", {false}};
        op.parameters.push_back(std::move(p));
    }
    addDestination(op, "{stem}.unlocked.xex");
    op.checkValues = [](const Parameters& values) {
        return chosenLimits(values).any() ? Status::success() : Status::failure("Choose at least one limit to remove");
    };
    return op;
}

OperationDescriptor boundingPathOperation() {
    auto op = makeOperation("boundingPath", "Add Bounding Path",
                            "Restricts the executable to run only from one folder (XexTool -a).");
    auto path = makeParameter("path", ParameterKind::Text, "Bounding path", std::string{},
                              "A console path, such as \\Device\\Cdrom0\\Games.");
    path.required = true;
    op.parameters = {path};
    addDestination(op, "{stem}.bound.xex");
    return op;
}

OperationDescriptor updateFixOperation() {
    auto op = makeOperation("updateFix", "Fix Updated Executable",
                            "Lets an executable that a title update patched run without the separate patch file "
                            "(XexTool -u).");
    addDestination(op, "{stem}.fixed.xex");
    return op;
}

OperationDescriptor exportInfoOperation() {
    auto op = makeOperation("exportInfo", "Export Info XML",
                            "Writes the title id, media id, regions, ratings and the other facts Import Info XML "
                            "can set, as an XML document (XexTool -z g).");
    op.parameters = {outputFile("{stem}.info.xml", {"XML documents (*.xml)", "All files (*)"})};
    return op;
}

OperationDescriptor importInfoOperation() {
    auto op = makeOperation("importInfo", "Import Info XML",
                            "Sets the facts in an info document, as Export Info XML writes it, into the executable "
                            "(XexTool -z s). Facts the document leaves out stay as they are.");
    auto info = makeParameter("info", ParameterKind::InputFile, "Info document", std::string{});
    info.required = true;
    info.nameFilters = {"XML documents (*.xml)", "All files (*)"};
    op.parameters = {info};
    addDestination(op, "{stem}.info.xex");
    return op;
}

// --- running ---------------------------------------------------------------------

Result<std::string> runInfo(OperationContext& ctx) {
    auto data = readSource(ctx);
    if (!data)
        return data.status();
    if (!keepGoing(ctx, "reading"))
        return Status::cancelledByUser();
    xt::XexInfo info;
    xt::ReadInfoParams params;
    params.xex = memoryInput(data.value(), ctx.sourceName());
    if (const xt::Status st = xt::readInfo(params, info); !st)
        return apiFailure(st);
    std::string text = ctx.text("detail") == "summary" ? info.summary : info.report;
    if (ctx.active("output")) {
        if (!keepGoing(ctx, "writing"))
            return Status::cancelledByUser();
        const xt::Bytes bytes(text.begin(), text.end());
        if (const Status st = writeAll(ctx.createOutputFile("output", bytes.size()), bytes); !st)
            return st;
    }
    return text;
}

// Extractions: one XexTool call into memory, then one output file.
using ExtractCall = std::function<xt::Status(const xt::Input&, xt::Bytes&)>;

Result<std::string> runExtract(OperationContext& ctx, const ExtractCall& call) {
    auto data = readSource(ctx);
    if (!data)
        return data.status();
    if (!keepGoing(ctx, "extracting"))
        return Status::cancelledByUser();
    xt::Bytes out;
    if (const xt::Status st = call(memoryInput(data.value(), ctx.sourceName()), out); !st)
        return apiFailure(st);
    if (!keepGoing(ctx, "writing"))
        return Status::cancelledByUser();
    if (const Status st = writeAll(ctx.createOutputFile("output", out.size()), out); !st)
        return st;
    return "Wrote " + ctx.text("output") + " (" + byteCount(out.size()) + ").\n";
}

Result<std::string> runBasefile(OperationContext& ctx) {
    xt::XexInfo info;
    auto report = runExtract(
        ctx,
        [&](const xt::Input& xex, xt::Bytes& out) {
            xt::ExtractBasefileParams params;
            params.xex = xex;
            params.basefile = xt::Output::memory(out);
            const xt::Status st = xt::extractBasefile(params);
            if (st) {
                xt::ReadInfoParams read;
                read.xex = xex;
                xt::readInfo(read, info);
            }
            return st;
        });
    if (!report || info.basefileType != "PE")
        return report;
    char details[256];
    std::snprintf(details, sizeof details,
                  "\nLoad it into IDA with these details (not as a PE or EXE file, the format is not valid):\n"
                  "File type:       Binary file\n"
                  "Processor type:  PowerPC: ppc\n"
                  "Load address:    0x%08X\n"
                  "Entry point:     0x%08X\n",
                  info.loadAddress, info.entryPoint);
    return report.value() + details;
}

Result<std::string> runIdc(OperationContext& ctx) {
    return runExtract(
        ctx,
        [](const xt::Input& xex, xt::Bytes& out) {
            xt::ExtractIdcParams params;
            params.xex = xex;
            params.idc = xt::Output::memory(out);
            return xt::extractIdc(params);
        });
}

Result<std::string> runExportInfo(OperationContext& ctx) {
    return runExtract(
        ctx,
        [](const xt::Input& xex, xt::Bytes& out) {
            xt::GetInfoParams params;
            params.xex = xex;
            params.infoFile = xt::Output::memory(out);
            return xt::getInfo(params);
        });
}

bool isPlainName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\:") == std::string::npos &&
           name.find('\0') == std::string::npos;
}

Result<std::string> runResources(OperationContext& ctx) {
    auto data = readSource(ctx);
    if (!data)
        return data.status();
    if (!keepGoing(ctx, "extracting"))
        return Status::cancelledByUser();
    xt::ExtractResourcesParams params;
    params.xex = memoryInput(data.value(), ctx.sourceName());
    std::vector<xt::ExtractedResource> resources;
    if (const xt::Status st = xt::extractResources(params, &resources); !st)
        return apiFailure(st);
    if (resources.empty())
        return std::string("The file contains no resources.\n");
    // Names come from the file: check them all before writing any.
    for (const xt::ExtractedResource& r : resources) {
        if (!isPlainName(r.name))
            return Status::failure("Resource name “" + r.name + "” is not a safe file name");
    }
    std::string names;
    for (const xt::ExtractedResource& r : resources) {
        if (!keepGoing(ctx, r.name))
            return Status::cancelledByUser();
        if (const Status st = writeAll(ctx.createInFolder("folder", r.name, r.data.size()), r.data); !st)
            return st;
        names += "  " + r.name + " (" + byteCount(r.data.size()) + ")\n";
    }
    return "Extracted " + std::to_string(resources.size()) + (resources.size() == 1 ? " resource" : " resources") +
           " to " + ctx.text("folder") + ":\n" + names;
}

xt::Compression compressionChoice(const std::string& id) {
    if (id == "basic")
        return xt::Compression::Basic;
    if (id == "uncompressed")
        return xt::Compression::Uncompressed;
    return xt::Compression::Normal;
}

Result<std::string> runConvert(OperationContext& ctx, xt::Compression compression, xt::Encryption encryption) {
    return runEdit(ctx, [&](const xt::Input& xex, const xt::Output& out, xt::EditResult* result) {
        xt::ConvertParams params;
        params.xex = xex;
        params.output = out;
        params.compression = compression;
        params.encryption = encryption;
        return xt::convert(params, result);
    });
}

Result<std::string> runMachine(OperationContext& ctx) {
    const bool retail = ctx.text("machine") == "retail";
    // XexTool's retail signing reports failure by design; the API does not
    // pass that on, so a retail run fails only for real errors.
    return runEdit(
        ctx,
        [&](const xt::Input& xex, const xt::Output& out, xt::EditResult* result) {
            xt::SignParams params;
            params.xex = xex;
            params.output = out;
            params.machine = retail ? xt::Machine::Retail : xt::Machine::Devkit;
            return xt::sign(params, result);
        },
        retail ? "The signature is cleared (all zero): no retail private key exists, so the file loads only on "
                 "consoles that skip the signature check.\n"
               : "Signed with the devkit key.\n");
}

Result<std::string> runPatch(OperationContext& ctx) {
    auto patch = readInput(ctx, "patch");
    if (!patch)
        return patch.status();
    return runEdit(ctx, [&](const xt::Input& xex, const xt::Output& out, xt::EditResult* result) {
        xt::PatchParams params;
        params.xex = xex;
        params.patch = memoryInput(patch.value(), ctx.text("patch"));
        params.output = out;
        return xt::applyPatch(params, result);
    });
}

Result<std::string> runLimits(OperationContext& ctx) {
    const xt::RemoveLimits limits = chosenLimits(ctx.parameters());
    if (!limits.any())
        return Status::failure("Choose at least one limit to remove");
    return runEdit(ctx, [&](const xt::Input& xex, const xt::Output& out, xt::EditResult* result) {
        xt::EditParams params;
        params.xex = xex;
        params.output = out;
        params.removeLimits = limits;
        return xt::edit(params, result);
    });
}

Result<std::string> runBoundingPath(OperationContext& ctx) {
    const std::string path = ctx.text("path");
    return runEdit(ctx, [&](const xt::Input& xex, const xt::Output& out, xt::EditResult* result) {
        xt::EditParams params;
        params.xex = xex;
        params.output = out;
        params.addBoundingPath = path;
        return xt::edit(params, result);
    });
}

Result<std::string> runUpdateFix(OperationContext& ctx) {
    bool applied = false;
    auto report = runEdit(ctx, [&](const xt::Input& xex, const xt::Output& out, xt::EditResult* result) {
        xt::EditParams params;
        params.xex = xex;
        params.output = out;
        params.updatePatchFix = true;
        const xt::Status st = xt::edit(params, result);
        applied = result->updatePatchFixApplied;
        return st;
    });
    if (report && !applied)
        report.value() += "The executable needed no fix; it was written unchanged.\n";
    return report;
}

Result<std::string> runImportInfo(OperationContext& ctx) {
    auto document = readInput(ctx, "info");
    if (!document)
        return document.status();
    const std::string xml(document->begin(), document->end());
    return runEdit(ctx, [&](const xt::Input& xex, const xt::Output& out, xt::EditResult* result) {
        xt::EditParams params;
        params.xex = xex;
        params.output = out;
        params.infoXml = xml;
        return xt::edit(params, result);
    });
}

// --- expanded view -------------------------------------------------------------------

void addIf(PropertyGroup& group, const char* label, const std::optional<std::uint32_t>& value, bool size = false) {
    if (value)
        group.items.push_back({label, size ? sizeText(*value) : hex32(*value)});
}

std::vector<PropertyGroup> describeInfo(const xt::XexInfo& info) {
    std::vector<PropertyGroup> out;

    PropertyGroup general{"Executable", {}};
    general.items.push_back({"Format", "XEX2"});
    if (!info.gameName.empty())
        general.items.push_back({"Title", info.gameName});
    std::string basefile = info.basefileType;
    if (info.basefileType == "PE")
        basefile = info.basefileIsDll ? "PE (DLL)" : "PE (executable)";
    general.items.push_back({"Basefile", basefile});
    general.items.push_back({"Module flags", hex32(info.moduleFlags)});
    if (!info.flagNames.empty())
        general.items.push_back({"Flags", join(info.flagNames)});
    if (!info.originalPeName.empty())
        general.items.push_back({"Original PE name", info.originalPeName});
    general.items.push_back({"Base address", hex32(info.loadAddress)});
    if (info.basefileType == "PE") {
        general.items.push_back({"Entry point", hex32(info.entryPoint)});
        general.items.push_back({"PE timestamp", hex32(info.filetime) + " (" + utcTime(info.filetime) + ")"});
        general.items.push_back({"PE checksum", hex32(info.checksum)});
    }
    general.items.push_back({"Image size", sizeText(info.imageSize)});
    if (info.exportTableAddress)
        general.items.push_back({"Export table", hex32(info.exportTableAddress)});
    addIf(general, "Stack size", info.stackSize, true);
    addIf(general, "Heap size", info.heapSize, true);
    addIf(general, "Page heap size", info.pageHeapSize, true);
    addIf(general, "Page heap flags", info.pageHeapFlags);
    addIf(general, "Workspace size", info.workspaceSize, true);
    addIf(general, "Filesystem cache size", info.filesystemCacheSize, true);
    if (info.extraDebugMemoryMB)
        general.items.push_back({"Extra debug memory", std::to_string(*info.extraDebugMemoryMB) + " MiB"});
    if (info.callCap1)
        general.items.push_back({"Call cap", hex32(*info.callCap1) + ", " + hex32(info.callCap2.value_or(0))});
    addIf(general, "Fast cap", info.fastCap);
    if (info.tlsSlots)
        general.items.push_back({"TLS", std::to_string(*info.tlsSlots) + " slots, data " +
                                            byteCount(info.tlsDataSize.value_or(0)) + ", raw data " +
                                            hex32(info.tlsRawAddress.value_or(0)) + " (" +
                                            byteCount(info.tlsRawSize.value_or(0)) + ")"});
    if (info.hasLogoData)
        general.items.push_back({"Logo", "Present"});
    if (info.exportsByName)
        general.items.push_back({"Exports by name", "Yes"});
    out.push_back(std::move(general));

    if (info.executionId || !info.altTitleIds.empty()) {
        PropertyGroup ids{"Execution ID", {}};
        if (const auto& e = info.executionId) {
            ids.items.push_back({"Title ID", titleIdText(e->titleId)});
            ids.items.push_back({"Media ID", hex32(e->mediaId)});
            ids.items.push_back({"Version", versionText(e->version)});
            ids.items.push_back({"Base version", versionText(e->baseVersion)});
            ids.items.push_back({"Disc", std::to_string(e->discNumber) + " of " + std::to_string(e->discCount)});
            ids.items.push_back({"Platform", std::to_string(e->platform)});
            ids.items.push_back({"Executable type", std::to_string(e->executableType)});
            ids.items.push_back({"Savegame ID", hex32(e->savegameId)});
        }
        for (const std::uint32_t id : info.altTitleIds)
            ids.items.push_back({"Alternate title ID", titleIdText(id)});
        out.push_back(std::move(ids));
    }

    PropertyGroup security{"Security", {}};
    security.items.push_back({"Machine", machineText(info)});
    security.items.push_back({"Encryption", info.encrypted ? "Encrypted" : "Not encrypted"});
    security.items.push_back({"Compression", compressionText(info.compression)});
    security.items.push_back({"Page size", sizeText(info.pageSize)});
    security.items.push_back({"Regions", join(info.regionNames) + " (" + hex32(info.regions) + ")"});
    security.items.push_back({"Allowed media", join(info.mediaNames) + " (" + hex32(info.mediaTypes) + ")"});
    security.items.push_back({"Image flags", hex32(info.imageFlags)});
    security.items.push_back({"System flags", hex32(info.systemFlags) + ", " + hex32(info.systemFlags2)});
    security.items.push_back({"Media ID", hexBytes(info.mediaId)});
    for (const xt::Bytes& id : info.multidiscMediaIds)
        security.items.push_back({"Multidisc media ID", hexBytes(id)});
    security.items.push_back({"Encryption key", hexBytes(info.imageKey)});
    if (info.lanKey)
        security.items.push_back({"LAN key", hexBytes(*info.lanKey)});
    if (info.discProfileId)
        security.items.push_back({"Disc profile ID", hexBytes(*info.discProfileId)});
    if (info.baseReference)
        security.items.push_back({"Base reference", hexBytes(*info.baseReference)});
    if (info.boundingPath)
        security.items.push_back({"Bounding path", *info.boundingPath});
    if (info.boundingDeviceId)
        security.items.push_back({"Bounding device ID", hexBytes(*info.boundingDeviceId)});
    if (info.restrictDatesStart)
        security.items.push_back({"Valid dates", fileTimeText(*info.restrictDatesStart) + " to " +
                                                     fileTimeText(info.restrictDatesEnd.value_or(0))});
    if (info.restrictKeyvaultMask) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "mask %016llX, value %016llX",
                      static_cast<unsigned long long>(*info.restrictKeyvaultMask),
                      static_cast<unsigned long long>(info.restrictKeyvaultValue.value_or(0)));
        security.items.push_back({"Keyvault privileges", buf});
    }
    for (const xt::Bytes& id : info.restrictConsoleIds)
        security.items.push_back({"Console ID", hexBytes(id)});
    for (std::size_t i = 0; i < info.sections.size(); ++i) {
        const xt::Section& s = info.sections[i];
        security.items.push_back({"Section " + std::to_string(i), hex32(s.address) + " – " +
                                                                      hex32(s.address + s.size) + " " + s.typeName});
    }
    out.push_back(std::move(security));

    if (info.gameRatings && info.gameRatings->size() >= 14) {
        // the rating codes of each board, in the order of the ratings block
        static const char* const boards[] = {"ESRB", "PEGI", "PEGI-FI", "PEGI-PT", "PEGI-BBFC", "CERO", "USK",
                                             "OFLC AU", "OFLC NZ", "KMRB", "Brazil", "FPB", "Taiwan", "Singapore"};
        PropertyGroup ratings{"Ratings", {}};
        char buf[8];
        for (std::size_t i = 0; i < std::size(boards); ++i) {
            std::snprintf(buf, sizeof buf, "%02X", (*info.gameRatings)[i]);
            ratings.items.push_back({boards[i], buf});
        }
        out.push_back(std::move(ratings));
    }

    if (!info.staticLibraries.empty() || !info.importLibraries.empty()) {
        PropertyGroup libraries{"Libraries", {}};
        for (const xt::StaticLibrary& lib : info.staticLibraries) {
            std::string value = versionText(lib.version) + ", " + lib.approvalName;
            if (lib.debugBuild)
                value += ", debug build";
            libraries.items.push_back({lib.name, value});
        }
        for (const xt::ImportLibrary& lib : info.importLibraries) {
            libraries.items.push_back({lib.name + " (imports)", versionText(lib.version) + ", at least " +
                                                                    versionText(lib.minVersion) + ", " +
                                                                    std::to_string(lib.records.size()) +
                                                                    (lib.records.size() == 1 ? " record" : " records")});
        }
        out.push_back(std::move(libraries));
    }

    if (!info.resources.empty()) {
        PropertyGroup resources{"Resources", {}};
        for (const xt::Resource& r : info.resources)
            resources.items.push_back({r.name, hex32(r.address) + ", " + byteCount(r.size)});
        out.push_back(std::move(resources));
    }

    if (!info.unknownEntries.empty()) {
        PropertyGroup other{"Other header entries", {}};
        for (const xt::UnknownEntry& e : info.unknownEntries) {
            other.items.push_back(
                {"Key " + hex32(e.key), e.data.empty() ? hex32(e.value) : byteCount(e.data.size()) + " of data"});
        }
        out.push_back(std::move(other));
    }
    return out;
}

} // namespace

int XexHandler::probe(const FileProbe& file) const {
    if (!file.startsWith("XEX2") && !file.startsWith("XEX1"))
        return kMatchNone;
    // The extension only breaks ties.
    const std::string ext = file.extension();
    return ext == "xex" || ext == "xexp" ? kMatchContent : kMatchContent - 1;
}

Status XexHandler::describe(ByteSource& source, const FileProbe& file, std::vector<PropertyGroup>& out) const {
    out.clear();
    if (file.startsWith("XEX1")) {
        out.push_back({"Executable", {{"Format", "XEX1 (pre-release)"}, {"Note", "XexTool reads only XEX2 files"}}});
        return Status::success();
    }
    if (!isXex2(file))
        return Status::failure("Not an Xbox 360 executable");
    if (file.size > kMaxDescribeSize) {
        out.push_back({"Executable",
                       {{"Format", "XEX2"},
                        {"Note", "Larger than " + byteCount(kMaxDescribeSize) + ": use File Tools, Info"}}});
        return Status::success();
    }
    auto data = readAll(source, kMaxDescribeSize);
    if (!data)
        return data.status();
    xt::XexInfo info;
    xt::ReadInfoParams params;
    params.xex = memoryInput(data.value(), file.name);
    if (const xt::Status st = xt::readInfo(params, info); !st) {
        out.push_back({"Executable", {{"Format", "XEX2"}, {"Error", st.error}}});
        return Status::success();
    }
    out = describeInfo(info);
    return Status::success();
}

std::vector<OperationDescriptor> XexHandler::operations() const {
    return {infoOperation(),     basefileOperation(), idcOperation(),          resourcesOperation(),
            decryptOperation(),  encryptOperation(),  compressionOperation(),  machineOperation(),
            patchOperation(),    limitsOperation(),   boundingPathOperation(), updateFixOperation(),
            exportInfoOperation(), importInfoOperation()};
}

Result<std::string> XexHandler::run(const std::string& operationId, OperationContext& context) const {
    if (operationId == "info")
        return runInfo(context);
    if (operationId == "basefile")
        return runBasefile(context);
    if (operationId == "idc")
        return runIdc(context);
    if (operationId == "resources")
        return runResources(context);
    if (operationId == "decrypt")
        return runConvert(context, xt::Compression::Keep, xt::Encryption::Decrypted);
    if (operationId == "encrypt")
        return runConvert(context, xt::Compression::Keep, xt::Encryption::Encrypted);
    if (operationId == "compression")
        return runConvert(context, compressionChoice(context.text("format")), xt::Encryption::Keep);
    if (operationId == "machine")
        return runMachine(context);
    if (operationId == "patch")
        return runPatch(context);
    if (operationId == "limits")
        return runLimits(context);
    if (operationId == "boundingPath")
        return runBoundingPath(context);
    if (operationId == "updateFix")
        return runUpdateFix(context);
    if (operationId == "exportInfo")
        return runExportInfo(context);
    if (operationId == "importInfo")
        return runImportInfo(context);
    return Status::failure("Unknown operation: " + operationId);
}

} // namespace unnamed::core
