#include "core/BlockDevice.hpp"
#include "core/PathUtil.hpp"

#include <fstream>
#include <system_error>

namespace unnamed::core {

namespace {

class ImageFile final : public BlockDevice {
public:
    ImageFile(std::fstream stream, std::uint64_t size, bool writable, std::string description)
        : m_stream(std::move(stream)), m_size(size), m_writable(writable), m_description(std::move(description)) {}

    std::uint64_t size() const override { return m_size; }
    bool writable() const override { return m_writable; }
    std::string description() const override { return m_description; }

    bool readAt(std::uint64_t offset, void* data, std::size_t size) override {
        if (offset > m_size || size > m_size - offset)
            return false;
        m_stream.clear();
        m_stream.seekg(static_cast<std::streamoff>(offset));
        m_stream.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
        const bool ok = !m_stream.fail();
        m_stream.clear();
        return ok;
    }

    bool writeAt(std::uint64_t offset, const void* data, std::size_t size) override {
        if (!m_writable || offset > m_size || size > m_size - offset)
            return false;
        m_stream.clear();
        m_stream.seekp(static_cast<std::streamoff>(offset));
        m_stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        const bool ok = !m_stream.fail();
        m_stream.clear();
        return ok;
    }

    bool flush() override {
        if (!m_writable)
            return true;
        m_stream.clear();
        return !m_stream.flush().fail();
    }

private:
    std::fstream m_stream;
    std::uint64_t m_size;
    bool m_writable;
    std::string m_description;
};

} // namespace

Result<std::shared_ptr<BlockDevice>> openImageFile(const std::filesystem::path& path, bool writable) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec))
        return Status::failure("Not an image file: " + pathToUtf8(path));
    std::fstream stream(path, std::ios::binary | std::ios::in | (writable ? std::ios::out : std::ios::openmode{}));
    if (!stream.is_open())
        return Status::failure(std::string("Cannot open ") + pathToUtf8(path) +
                               (writable ? " for writing" : ""));
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (stream.fail() || end < 0)
        return Status::failure("Cannot read the size of " + pathToUtf8(path));
    return std::shared_ptr<BlockDevice>(
        std::make_shared<ImageFile>(std::move(stream), static_cast<std::uint64_t>(end), writable, pathToUtf8(path)));
}

} // namespace unnamed::core
