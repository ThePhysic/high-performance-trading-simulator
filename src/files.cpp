#include "hpts/files.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <streambuf>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace hpts {
namespace {

[[noreturn]] void file_error(const char* operation, const std::filesystem::path& path,
                             int error) {
    throw std::runtime_error(std::string(operation) + " " + path.string() + ": " +
                             std::error_code(error, std::system_category()).message());
}

class FileHandle {
public:
    FileHandle(const std::filesystem::path& path, bool writing) : path_(path) {
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(),
            (writing ? GENERIC_WRITE : GENERIC_READ) | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            writing ? OPEN_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            file_error("cannot open", path_, static_cast<int>(GetLastError()));
#else
        const int flags = (writing ? O_WRONLY | O_CREAT : O_RDONLY) | O_NONBLOCK | O_CLOEXEC;
        do { handle_ = ::open(path.c_str(), flags, 0666); } while (handle_ < 0 && errno == EINTR);
        if (handle_ < 0) file_error("cannot open", path_, errno);
#endif
        try {
#ifdef _WIN32
            if (GetFileType(handle_) != FILE_TYPE_DISK)
                throw std::runtime_error("unsupported file type: " + path_.string());
            FILE_ID_INFO information{};
            if (!GetFileInformationByHandleEx(handle_, FileIdInfo, &information, sizeof(information)))
                file_error("cannot identify", path_, static_cast<int>(GetLastError()));
            identity_.device = information.VolumeSerialNumber;
            std::copy(std::begin(information.FileId.Identifier), std::end(information.FileId.Identifier),
                      identity_.object.begin());
            regular_ = true;
#else
            struct stat information{};
            if (::fstat(handle_, &information) < 0) file_error("cannot identify", path_, errno);
            regular_ = S_ISREG(information.st_mode);
            if (!regular_ && !S_ISCHR(information.st_mode))
                throw std::runtime_error("unsupported file type: " + path_.string());
            identity_.device = static_cast<std::uint64_t>(information.st_dev);
            const auto inode = static_cast<std::uint64_t>(information.st_ino);
            std::memcpy(identity_.object.data(), &inode, sizeof(inode));
#endif
        } catch (...) { close_noexcept(); throw; }
    }

    ~FileHandle() { close_noexcept(); }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    [[nodiscard]] const FileIdentity& identity() const noexcept { return identity_; }

    void truncate() {
        if (!regular_) return; // Character devices such as /dev/full have no length.
#ifdef _WIN32
        LARGE_INTEGER beginning{};
        if (!SetFilePointerEx(handle_, beginning, nullptr, FILE_BEGIN) || !SetEndOfFile(handle_))
            file_error("cannot truncate", path_, static_cast<int>(GetLastError()));
#else
        int result;
        do { result = ::ftruncate(handle_, 0); } while (result < 0 && errno == EINTR);
        if (result < 0) file_error("cannot truncate", path_, errno);
        // Fresh handles are already at offset zero; no path is reopened.
#endif
    }

    std::size_t read(char* destination, std::size_t size) {
#ifdef _WIN32
        DWORD count = 0;
        const auto requested = static_cast<DWORD>((std::min)(size,
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        if (!ReadFile(handle_, destination, requested, &count, nullptr))
            file_error("cannot read", path_, static_cast<int>(GetLastError()));
        return count;
#else
        ssize_t count;
        do { count = ::read(handle_, destination, size); } while (count < 0 && errno == EINTR);
        if (count < 0) file_error("cannot read", path_, errno);
        return static_cast<std::size_t>(count);
#endif
    }

    void write(const char* data, std::size_t size) {
        while (size != 0) {
#ifdef _WIN32
            DWORD count = 0;
            const auto requested = static_cast<DWORD>((std::min)(size,
                static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
            if (!WriteFile(handle_, data, requested, &count, nullptr))
                file_error("cannot write", path_, static_cast<int>(GetLastError()));
#else
            const auto requested = (std::min)(size,
                static_cast<std::size_t>((std::numeric_limits<ssize_t>::max)()));
            ssize_t count;
            do { count = ::write(handle_, data, requested); } while (count < 0 && errno == EINTR);
            if (count < 0) file_error("cannot write", path_, errno);
#endif
            if (count == 0) throw std::runtime_error("zero-byte write: " + path_.string());
            data += count;
            size -= static_cast<std::size_t>(count);
        }
    }

    void close() {
#ifdef _WIN32
        if (handle_ == INVALID_HANDLE_VALUE) return;
        const auto closing = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        if (!CloseHandle(closing)) file_error("cannot close", path_, static_cast<int>(GetLastError()));
#else
        if (handle_ < 0) return;
        const int closing = handle_;
        handle_ = -1;
        // Do not retry close(EINTR): Linux has already released the descriptor.
        if (::close(closing) < 0) file_error("cannot close", path_, errno);
#endif
    }

private:
    void close_noexcept() noexcept {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) { CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }
#else
        if (handle_ >= 0) { ::close(handle_); handle_ = -1; }
#endif
    }
#ifdef _WIN32
    // Kept as a native handle throughout identity validation and actual I/O.
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int handle_ = -1;
#endif
    std::filesystem::path path_;
    FileIdentity identity_;
    bool regular_ = false;
};

void distinct(const std::vector<FileIdentity>& identities) {
    for (std::size_t i = 0; i < identities.size(); ++i)
        for (std::size_t j = 0; j < i; ++j)
            if (identities[i] == identities[j])
                throw std::runtime_error("file arguments must name distinct files");
}

}  // namespace

struct InputFile::Impl : std::streambuf {
    explicit Impl(const std::filesystem::path& path) : file(path, false) {}
    int_type underflow() override {
        const auto size = file.read(buffer.data(), buffer.size());
        if (size == 0) return traits_type::eof();
        setg(buffer.data(), buffer.data(), buffer.data() + size);
        return traits_type::to_int_type(*gptr());
    }
    FileHandle file;
    std::array<char, 8192> buffer;
};

InputFile::InputFile(const std::filesystem::path& path)
    : std::istream(nullptr), impl_(std::make_unique<Impl>(path)) { rdbuf(impl_.get()); }
InputFile::~InputFile() = default;
const FileIdentity& InputFile::identity() const noexcept { return impl_->file.identity(); }

struct OutputFile::Impl : std::streambuf {
    explicit Impl(const std::filesystem::path& path) : file(path, true) {
        // No put area is available before admission, even for a single char.
        setp(buffer.data(), buffer.data());
    }
    void admit() {
        if (prepared) throw std::logic_error("output already admitted");
        file.truncate();
        prepared = true;
        setp(buffer.data(), buffer.data() + buffer.size());
    }
    void require_open() const {
        if (!prepared || closed) throw std::logic_error("output is not admitted or is closed");
    }
    void flush_buffer() {
        require_open();
        const auto size = static_cast<std::size_t>(pptr() - pbase());
        if (size) file.write(pbase(), size);
        setp(buffer.data(), buffer.data() + buffer.size());
    }
    std::streamsize xsputn(const char* data, std::streamsize size) override {
        require_open();
        if (size <= 0) return 0;
        const auto available = static_cast<std::streamsize>(epptr() - pptr());
        if (size > available) flush_buffer();
        if (static_cast<std::size_t>(size) >= buffer.size()) file.write(data, static_cast<std::size_t>(size));
        else {
            std::memcpy(pptr(), data, static_cast<std::size_t>(size));
            pbump(static_cast<int>(size));
        }
        return size;
    }
    int_type overflow(int_type value = traits_type::eof()) override {
        flush_buffer();
        if (!traits_type::eq_int_type(value, traits_type::eof())) {
            *pptr() = traits_type::to_char_type(value);
            pbump(1);
        }
        return traits_type::not_eof(value);
    }
    int sync() override { flush_buffer(); return 0; }
    FileHandle file;
    std::array<char, 8192> buffer;
    bool prepared = false, closed = false;
};

OutputFile::OutputFile(const std::filesystem::path& path)
    : std::ostream(nullptr), impl_(std::make_unique<Impl>(path)) {
    rdbuf(impl_.get());
    exceptions(std::ios::badbit | std::ios::failbit);
}
OutputFile::~OutputFile() = default;
const FileIdentity& OutputFile::identity() const noexcept { return impl_->file.identity(); }
void OutputFile::finish() {
    if (impl_->closed) return;
    flush();
    impl_->file.close();
    impl_->closed = true;
}

void require_distinct_files(std::initializer_list<FileIdentity> identities) {
    distinct(std::vector<FileIdentity>(identities));
}

void prepare_outputs(std::initializer_list<OutputFile*> outputs,
                     std::initializer_list<FileIdentity> inputs) {
    std::vector<FileIdentity> identities(inputs);
    for (const auto* output : outputs) {
        if (!output) continue;
        if (output->impl_->prepared) throw std::logic_error("output already admitted");
        identities.push_back(output->identity());
    }
    distinct(identities);
    for (auto* output : outputs) {
        if (!output) continue;
        output->impl_->admit();
    }
}

}  // namespace hpts
