#include "core/Stream.hpp"
#include "core/PathUtil.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

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

// A staging file of our own next to the target: a fresh name created
// exclusively, so no file of the user's is ever opened, truncated or removed.
// An existing target's mode is kept: the staging file is private until it
// takes the target's place.
Result<fs::path> createStagingFile(const fs::path& target, bool replacing) {
    static std::atomic<std::uint64_t> counter{0};
    thread_local std::mt19937_64 random{std::random_device{}() ^
                                        static_cast<std::uint64_t>(
                                            std::chrono::steady_clock::now().time_since_epoch().count())};
    // A long name leaves room for the suffix by leaving itself out.
    std::string name = pathToUtf8(target.filename());
    if (name.size() > 200)
        name.clear();
    for (int attempt = 0; attempt < 100; ++attempt) {
        char suffix[40];
        std::snprintf(suffix, sizeof suffix, ".%08llx%04llx.part",
                      static_cast<unsigned long long>(random() & 0xFFFFFFFFu),
                      static_cast<unsigned long long>(counter++ & 0xFFFFu));
        fs::path temp = target.parent_path() / pathFromUtf8("." + name + suffix);
#ifdef _WIN32
        const int fd = _wopen(temp.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
        (void)replacing;
#else
        const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, replacing ? 0600 : 0666);
#endif
        if (fd >= 0) {
#ifdef _WIN32
            _close(fd);
#else
            ::close(fd);
#endif
            return temp;
        }
        if (errno != EEXIST)
            break;
    }
    return Status::failure("Cannot open for writing: " + pathToUtf8(target));
}

// The finished file takes the target's name; without `overwrite` only while
// nothing has it, checked by the system rather than before.
Status publish(const fs::path& temp, const fs::path& target, bool overwrite) {
    std::error_code ec;
    if (overwrite) {
#ifndef _WIN32
        struct stat st;
        if (::stat(target.c_str(), &st) == 0) {
            ::chmod(temp.c_str(), st.st_mode & 07777);
            if (::chown(temp.c_str(), st.st_uid, st.st_gid) != 0) {
                // only root may give a file away; the group may not be ours
            }
        }
#endif
        fs::rename(temp, target, ec);
        return ec ? Status::failure(ec.message()) : Status::success();
    }
    const auto exists = [&] { return Status::failure("Already exists: " + pathToUtf8(target)); };
#ifdef _WIN32
    if (MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
        return Status::success();
    const DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)
        return exists();
    return Status::failure(std::system_category().message(static_cast<int>(error)));
#else
    if (::link(temp.c_str(), target.c_str()) == 0) {
        ::unlink(temp.c_str());
        return Status::success();
    }
    if (errno == EEXIST)
        return exists();
#ifdef RENAME_NOREPLACE
    if (::renameat2(AT_FDCWD, temp.c_str(), AT_FDCWD, target.c_str(), RENAME_NOREPLACE) == 0)
        return Status::success();
    if (errno == EEXIST)
        return exists();
#endif
    // A filesystem without hard links or an exclusive rename (FAT, some
    // network shares): check, then rename.
    if (fs::exists(fs::symlink_status(target, ec)))
        return exists();
    fs::rename(temp, target, ec);
    return ec ? Status::failure(ec.message()) : Status::success();
#endif
}

class FileSink final : public ByteSink {
public:
    FileSink(std::ofstream out, fs::path temp, fs::path target, bool overwrite)
        : m_out(std::move(out)), m_temp(std::move(temp)), m_target(std::move(target)), m_overwrite(overwrite) {}

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
        if (const Status st = publish(m_temp, m_target, m_overwrite); !st)
            return st;
        m_done = true;
        return Status::success();
    }

private:
    std::ofstream m_out;
    fs::path m_temp;
    fs::path m_target;
    bool m_overwrite;
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

Result<std::unique_ptr<ByteSink>> openFileSink(const fs::path& requested, bool overwrite) {
    std::error_code ec;
    fs::path path = requested;
    const std::string name = pathToUtf8(requested);
    if (!overwrite && fs::exists(fs::symlink_status(path, ec)))
        return Status::failure("Already exists: " + name);
    if (fs::is_directory(path, ec))
        return Status::failure("Is a folder: " + name);
    const bool replacing = overwrite && fs::exists(path, ec);
    if (replacing) {
        // The file a link points to gets the new contents; the link stays.
        if (fs::is_symlink(fs::symlink_status(path, ec))) {
            path = fs::canonical(path, ec);
            if (ec)
                return Status::failure("Cannot follow the link " + name + ": " + ec.message());
        }
        if (!fs::is_regular_file(path, ec))
            return Status::failure("Not a regular file: " + name);
        if (const auto links = fs::hard_link_count(path, ec); !ec && links > 1)
            return Status::failure(name + " has " + std::to_string(links - 1) +
                                   (links == 2 ? " other hard link" : " other hard links") +
                                   ", which would keep the old contents");
#ifdef _WIN32
        if ((fs::status(path, ec).permissions() & fs::perms::owner_write) == fs::perms::none)
#else
        if (::access(path.c_str(), W_OK) != 0)
#endif
            return Status::failure(name + " is read-only");
    }

    auto temp = createStagingFile(path, replacing);
    if (!temp)
        return temp.status();
    std::ofstream out(temp.value(), std::ios::binary | std::ios::trunc);
    if (!out) {
        fs::remove(temp.value(), ec);
        return Status::failure("Cannot open for writing: " + name);
    }
    return std::unique_ptr<ByteSink>(new FileSink(std::move(out), std::move(temp.value()), path, overwrite));
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
