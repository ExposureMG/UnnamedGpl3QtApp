#pragma once

#include "core/FormatHandler.hpp"

#include <cstdint>

namespace unnamed::core {

// Xbox 360 executables through the XexTool fork's embedding API
// (extern/XexTool, XexApi.h): the expanded view, the -l report, extraction,
// decrypt and encrypt, compression, signing, delta patches and the command
// line's single-flag edits. Every file it writes is byte for byte the one the
// XexTool command line writes for the same step. Built with UNNAMED_WITH_XEX.
//
// XexTool works on whole files in memory, so the source is read into memory
// first. Its calls cannot be interrupted: cancelling takes effect between
// reading, processing and writing.
class XexHandler final : public FormatHandler {
public:
    // Larger files are refused.
    static constexpr std::uint64_t kMaxSize = std::uint64_t{512} << 20;
    // The expanded view reads files up to this size; it only names the
    // format of larger ones (the Info tool still reads them).
    static constexpr std::uint64_t kMaxDescribeSize = std::uint64_t{64} << 20;

    std::string id() const override { return "xex"; }
    std::string name() const override { return "Xbox 360 executable"; }
    int probe(const FileProbe& file) const override;
    Status describe(ByteSource& source, const FileProbe& file, std::vector<PropertyGroup>& out) const override;
    std::vector<OperationDescriptor> operations() const override;
    Result<std::string> run(const std::string& operationId, OperationContext& context) const override;
};

} // namespace unnamed::core
