#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <istream>
#include <memory>
#include <ostream>

namespace hpts {

struct FileIdentity {
    std::uint64_t device = 0;
    std::array<unsigned char, 16> object{};
    bool operator==(const FileIdentity&) const = default;
};

// The streams own the handles used for identity checks and actual I/O. They
// cannot move, because their rdbuf pointers refer to their own implementation.
class InputFile final : public std::istream {
public:
    explicit InputFile(const std::filesystem::path& path);
    ~InputFile() override;
    InputFile(const InputFile&) = delete;
    InputFile& operator=(const InputFile&) = delete;
    [[nodiscard]] const FileIdentity& identity() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class OutputFile final : public std::ostream {
public:
    // Opens/creates without truncation. No bytes may be written until admitted
    // by prepare_outputs, after all handles have been opened and compared.
    explicit OutputFile(const std::filesystem::path& path);
    ~OutputFile() override;
    OutputFile(const OutputFile&) = delete;
    OutputFile& operator=(const OutputFile&) = delete;
    [[nodiscard]] const FileIdentity& identity() const noexcept;
    // Checked flush AND close. Call before claiming a complete output/journal.
    void finish();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend void prepare_outputs(std::initializer_list<OutputFile*>,
                                std::initializer_list<FileIdentity>);
};

// Null optional outputs are allowed. Admission/open/identity errors leave all
// existing bytes intact; newly created empty files can remain. Once truncation
// starts, subsequent I/O failure can leave partial outputs (no multi-file
// transaction or crash-durability guarantee).
void prepare_outputs(std::initializer_list<OutputFile*> outputs,
                     std::initializer_list<FileIdentity> inputs);
void require_distinct_files(std::initializer_list<FileIdentity> identities);

}  // namespace hpts
