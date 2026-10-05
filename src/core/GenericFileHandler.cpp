#include "core/GenericFileHandler.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace unnamed::core {

// --- checksums ---------------------------------------------------------------

namespace {

std::array<std::uint32_t, 256> makeCrcTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
    }
    return table;
}

std::uint32_t rotl(std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

} // namespace

void Crc32::update(const void* data, std::size_t size) {
    static const auto table = makeCrcTable();
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i)
        m_crc = table[(m_crc ^ p[i]) & 0xFFu] ^ (m_crc >> 8);
}

Sha1::Sha1() : m_h{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u}, m_buffer{} {}

void Sha1::block(const std::uint8_t* p) {
    std::uint32_t w[80];
    for (int i = 0; i < 16; ++i)
        w[i] = std::uint32_t(p[4 * i]) << 24 | std::uint32_t(p[4 * i + 1]) << 16 | std::uint32_t(p[4 * i + 2]) << 8 |
               std::uint32_t(p[4 * i + 3]);
    for (int i = 16; i < 80; ++i)
        w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    std::uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3], e = m_h[4];
    for (int i = 0; i < 80; ++i) {
        std::uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        const std::uint32_t t = rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl(b, 30);
        b = a;
        a = t;
    }
    m_h[0] += a;
    m_h[1] += b;
    m_h[2] += c;
    m_h[3] += d;
    m_h[4] += e;
}

void Sha1::update(const void* data, std::size_t size) {
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

std::string Sha1::hexDigest() {
    const std::uint64_t bits = m_length * 8;
    const std::uint8_t pad = 0x80;
    update(&pad, 1);
    const std::uint8_t zero = 0;
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

// --- operations ----------------------------------------------------------------

namespace {

constexpr std::size_t kChunk = 1 << 20;

const std::vector<std::string> kTextFilters = {"Text files (*.txt)", "All files (*)"};

OperationDescriptor checksumOperation() {
    OperationDescriptor op;
    op.id = "checksum";
    op.name = "Checksum";
    op.description = "Computes the file's CRC-32 or SHA-1, to compare a copy with its original.";

    ParameterDescriptor algorithm;
    algorithm.id = "algorithm";
    algorithm.kind = ParameterKind::Choice;
    algorithm.label = "Algorithm";
    algorithm.options = {{"crc32", "CRC-32"}, {"sha1", "SHA-1"}, {"both", "CRC-32 and SHA-1"}};
    algorithm.defaultValue = std::string{"sha1"};

    ParameterDescriptor save;
    save.id = "save";
    save.kind = ParameterKind::Boolean;
    save.label = "Save to a file";
    save.help = "Also writes the result as a text file (BSD checksum format).";
    save.defaultValue = false;

    ParameterDescriptor output;
    output.id = "output";
    output.kind = ParameterKind::OutputFile;
    output.label = "Output file";
    output.required = true;
    output.nameFilters = kTextFilters;
    output.suggestedName = "{name}.checksum.txt";
    output.visibleWhen = {"save", {true}};

    op.parameters = {algorithm, save, output};
    return op;
}

OperationDescriptor hexDumpOperation() {
    OperationDescriptor op;
    op.id = "hexdump";
    op.name = "Hex Dump";
    op.description = "Writes the file's bytes as hexadecimal text, in the layout of hexdump -C.";

    ParameterDescriptor range;
    range.id = "range";
    range.kind = ParameterKind::Choice;
    range.label = "Range";
    range.options = {{"part", "Part of the file"}, {"whole", "Whole file"}};
    range.defaultValue = std::string{"part"};

    ParameterDescriptor offset;
    offset.id = "offset";
    offset.kind = ParameterKind::Integer;
    offset.label = "Start offset";
    offset.help = "In bytes from the start of the file.";
    offset.defaultValue = std::int64_t{0};
    offset.minimum = 0;
    offset.visibleWhen = {"range", {std::string{"part"}}};

    ParameterDescriptor length;
    length.id = "length";
    length.kind = ParameterKind::Integer;
    length.label = "Length";
    length.help = "Number of bytes to dump (at most 16 MiB).";
    length.defaultValue = std::int64_t{512};
    length.minimum = 1;
    length.maximum = 16 << 20;
    length.visibleWhen = {"range", {std::string{"part"}}};

    ParameterDescriptor ascii;
    ascii.id = "ascii";
    ascii.kind = ParameterKind::Boolean;
    ascii.label = "Show characters";
    ascii.help = "Adds the printable characters of each line on the right.";
    ascii.defaultValue = true;

    ParameterDescriptor output;
    output.id = "output";
    output.kind = ParameterKind::OutputFile;
    output.label = "Output file";
    output.required = true;
    output.nameFilters = kTextFilters;
    output.suggestedName = "{name}.hex.txt";

    op.parameters = {range, offset, length, ascii, output};
    return op;
}

std::string hexLine(std::uint64_t offset, const std::uint8_t* p, std::size_t n, bool ascii) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%08llx ", static_cast<unsigned long long>(offset));
    std::string line = buf;
    for (std::size_t i = 0; i < 16; ++i) {
        if (i == 0 || i == 8)
            line += ' ';
        if (i < n) {
            std::snprintf(buf, sizeof buf, "%02x ", p[i]);
            line += buf;
        } else {
            line += "   ";
        }
    }
    if (ascii) {
        line += " |";
        for (std::size_t i = 0; i < n; ++i)
            line += (p[i] >= 0x20 && p[i] < 0x7F) ? static_cast<char>(p[i]) : '.';
        line += '|';
    } else {
        while (!line.empty() && line.back() == ' ')
            line.pop_back();
    }
    line += '\n';
    return line;
}

Result<std::string> runChecksum(OperationContext& ctx) {
    const std::string& algorithm = ctx.text("algorithm");
    const bool crc = algorithm != "sha1";
    const bool sha = algorithm != "crc32";
    Crc32 crc32;
    Sha1 sha1;
    std::vector<std::uint8_t> buffer(kChunk);
    std::uint64_t done = 0;
    for (;;) {
        if (!ctx.progress(done, ctx.sourceSize()))
            return Status::cancelledByUser();
        auto n = ctx.source().read(buffer.data(), buffer.size());
        if (!n)
            return n.status();
        if (n.value() == 0)
            break;
        if (crc)
            crc32.update(buffer.data(), n.value());
        if (sha)
            sha1.update(buffer.data(), n.value());
        done += n.value();
    }

    std::string report;
    if (crc) {
        char hex[16];
        std::snprintf(hex, sizeof hex, "%08x", crc32.value());
        report += "CRC32 (" + ctx.sourceName() + ") = " + hex + "\n";
    }
    if (sha)
        report += "SHA1 (" + ctx.sourceName() + ") = " + sha1.hexDigest() + "\n";

    if (ctx.active("output")) {
        auto out = ctx.createOutputFile("output", report.size());
        if (!out)
            return out.status();
        if (const Status st = out.value()->write(report.data(), report.size()); !st)
            return st;
        if (const Status st = out.value()->finish(); !st)
            return st;
    }
    return report;
}

Result<std::string> runHexDump(OperationContext& ctx) {
    const bool whole = ctx.text("range") == "whole";
    const std::uint64_t start = whole ? 0 : static_cast<std::uint64_t>(ctx.integer("offset"));
    if (!whole && start >= ctx.sourceSize())
        return Status::failure("The start offset is past the end of the file (" + std::to_string(ctx.sourceSize()) +
                               " bytes)");
    const std::uint64_t end =
        whole ? ctx.sourceSize() : std::min(ctx.sourceSize(), start + static_cast<std::uint64_t>(ctx.integer("length")));
    const bool ascii = ctx.flag("ascii");

    auto out = ctx.createOutputFile("output", std::nullopt);
    if (!out)
        return out.status();

    std::vector<std::uint8_t> buffer(kChunk);
    std::uint8_t line[16];
    std::size_t inLine = 0; // bytes collected for the current line, which starts at pos - inLine
    std::uint64_t pos = 0;
    std::string text;
    while (pos < end) {
        if (!ctx.progress(pos, end))
            return Status::cancelledByUser(); // the unfinished sink leaves nothing
        const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), end - pos));
        auto n = ctx.source().read(buffer.data(), want);
        if (!n)
            return n.status();
        if (n.value() == 0)
            break; // the file is shorter than its listed size
        text.clear();
        for (std::size_t i = 0; i < n.value(); ++i, ++pos) {
            if (pos < start)
                continue;
            line[inLine++] = buffer[i];
            if (inLine == sizeof line) {
                text += hexLine(pos + 1 - inLine, line, inLine, ascii);
                inLine = 0;
            }
        }
        if (const Status st = out.value()->write(text.data(), text.size()); !st)
            return st;
    }
    if (inLine) {
        text = hexLine(pos - inLine, line, inLine, ascii);
        if (const Status st = out.value()->write(text.data(), text.size()); !st)
            return st;
    }
    char last[32];
    std::snprintf(last, sizeof last, "%08llx\n", static_cast<unsigned long long>(std::min(pos, end)));
    if (const Status st = out.value()->write(last, std::strlen(last)); !st)
        return st;
    if (const Status st = out.value()->finish(); !st)
        return st;
    return std::string{};
}

} // namespace

std::vector<OperationDescriptor> GenericFileHandler::operations() const {
    return {checksumOperation(), hexDumpOperation()};
}

Result<std::string> GenericFileHandler::run(const std::string& operationId, OperationContext& context) const {
    if (operationId == "checksum")
        return runChecksum(context);
    if (operationId == "hexdump")
        return runHexDump(context);
    return Status::failure("Unknown operation: " + operationId);
}

} // namespace unnamed::core
