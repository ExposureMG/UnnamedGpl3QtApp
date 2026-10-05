#pragma once

#include "core/FormatHandler.hpp"

namespace unnamed::core {

// Tools that work on any file: checksums (CRC-32, SHA-1) and a hex dump to a
// text file. Always built in; it also keeps the format-handler path testable
// without the optional format libraries.
class GenericFileHandler final : public FormatHandler {
public:
    std::string id() const override { return "file"; }
    std::string name() const override { return "Any file"; }
    int probe(const FileProbe&) const override { return kMatchAnyFile; }
    std::vector<OperationDescriptor> operations() const override;
    Result<std::string> run(const std::string& operationId, OperationContext& context) const override;
};

// CRC-32 (IEEE, as zip) and SHA-1, incrementally.
class Crc32 {
public:
    void update(const void* data, std::size_t size);
    std::uint32_t value() const { return ~m_crc; }

private:
    std::uint32_t m_crc = 0xFFFFFFFFu;
};

class Sha1 {
public:
    Sha1();
    void update(const void* data, std::size_t size);
    // Lowercase hex digest; the object is spent afterwards.
    std::string hexDigest();

private:
    void block(const std::uint8_t* p);

    std::uint32_t m_h[5];
    std::uint8_t m_buffer[64];
    std::size_t m_used = 0;
    std::uint64_t m_length = 0;
};

} // namespace unnamed::core
