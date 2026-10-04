#include "core/Stream.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace unnamed::core {

namespace {

class FileSource final : public ByteSource {
public:
    FileSource(std::ifstream in, std::uint64_t size) : m_in(std::move(in)), m_size(size) {}

    Result<std::size_t> read(void* buffer, std::size_t size) override {
        m_in.read(static_cast<char*>(buffer), static_cast<std::streamsize>(size));
        if (m_in.bad())
            return Status::failure("Read error");
        return static_cast<std::size_t>(m_in.gcount());
    }
    std::optional<std::uint64_t> size() const override { return m_size; }

private:
    std::ifstream m_in;
    std::uint64_t m_size;
};

class FileSink final : public ByteSink {
public:
    FileSink(std::ofstream out, fs::path temp, fs::path target)
        : m_out(std::move(out)), m_temp(std::move(temp)), m_target(std::move(target)) {}

    ~FileSink() override {
        if (!m_done) {
            m_out.close();
            std::error_code ec;
            fs::remove(m_temp, ec);
        }
    }

    Status write(const void* data, std::size_t size) override {
        m_out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        return m_out ? Status::success() : Status::failure("Write error (disk full?)");
    }

    Status finish() override {
        m_out.flush();
        const bool good = static_cast<bool>(m_out);
        m_out.close();
        if (!good)
            return Status::failure("Write error (disk full?)");
        std::error_code ec;
        fs::rename(m_temp, m_target, ec); // replaces an existing target
        if (ec)
            return Status::failure(ec.message());
        m_done = true;
        return Status::success();
    }

private:
    std::ofstream m_out;
    fs::path m_temp;
    fs::path m_target;
    bool m_done = false;
};

} // namespace

Result<std::unique_ptr<ByteSource>> openFileSource(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec))
        return Status::failure("Not a file: " + path.string());
    const std::uint64_t size = fs::file_size(path, ec);
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Status::failure("Cannot open for reading: " + path.string());
    return std::unique_ptr<ByteSource>(new FileSource(std::move(in), ec ? 0 : size));
}

Result<std::unique_ptr<ByteSink>> openFileSink(const fs::path& path, bool overwrite) {
    std::error_code ec;
    if (!overwrite && fs::exists(path, ec))
        return Status::failure("Already exists: " + path.string());
    if (fs::is_directory(path, ec))
        return Status::failure("Is a folder: " + path.string());

    fs::path temp = path;
    temp += ".part";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out)
        return Status::failure("Cannot open for writing: " + path.string());
    return std::unique_ptr<ByteSink>(new FileSink(std::move(out), temp, path));
}

Result<std::size_t> MemorySource::read(void* buffer, std::size_t size) {
    const std::size_t n = std::min(size, m_data.size() - m_pos);
    if (n)
        std::memcpy(buffer, m_data.data() + m_pos, n);
    m_pos += n;
    return n;
}

Status MemorySink::write(const void* data, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    m_data.insert(m_data.end(), p, p + size);
    return Status::success();
}

} // namespace unnamed::core
