#pragma once

#include "core/Status.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace unnamed::core {

// Sequential byte streams are how file contents move between filesystems, so a
// backend never needs to know where the other side lives (host folder, FATX
// partition, console over the network, Android content:// URI, ...).

class ByteSource {
public:
    virtual ~ByteSource() = default;
    // Reads up to `size` bytes; returns the number read, 0 at end of stream.
    virtual Result<std::size_t> read(void* buffer, std::size_t size) = 0;
    // Total length if known.
    virtual std::optional<std::uint64_t> size() const { return std::nullopt; }
};

class ByteSink {
public:
    virtual ~ByteSink() = default;
    virtual Status write(const void* data, std::size_t size) = 0;
    // Completes the write. A sink destroyed without a successful finish() must
    // leave the destination untouched (discard partial data).
    virtual Status finish() = 0;
};

// --- host file streams (UTF-8 paths are converted portably) ------------------

Result<std::unique_ptr<ByteSource>> openFileSource(const std::filesystem::path& path);
// Writes to a temporary file and moves it into place on finish(), so a failed
// or cancelled transfer never leaves a half-written file.
Result<std::unique_ptr<ByteSink>> openFileSink(const std::filesystem::path& path, bool overwrite);

// --- in-memory streams -------------------------------------------------------

class MemorySource final : public ByteSource {
public:
    explicit MemorySource(std::vector<std::uint8_t> data) : m_data(std::move(data)) {}
    explicit MemorySource(const std::string& text) : m_data(text.begin(), text.end()) {}
    Result<std::size_t> read(void* buffer, std::size_t size) override;
    std::optional<std::uint64_t> size() const override { return m_data.size(); }

private:
    std::vector<std::uint8_t> m_data;
    std::size_t m_pos = 0;
};

class MemorySink final : public ByteSink {
public:
    Status write(const void* data, std::size_t size) override;
    Status finish() override {
        m_finished = true;
        return Status::success();
    }
    const std::vector<std::uint8_t>& data() const { return m_data; }
    bool finished() const { return m_finished; }

private:
    std::vector<std::uint8_t> m_data;
    bool m_finished = false;
};

} // namespace unnamed::core
