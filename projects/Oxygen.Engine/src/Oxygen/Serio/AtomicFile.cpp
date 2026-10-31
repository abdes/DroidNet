//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/SystemError.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/RegionStream.h>
#include <Oxygen/Serio/Types.h>

#ifdef OXYGEN_WINDOWS
#  include <Windows.h> // IWYU pragma: keep

#  include <AccCtrl.h>
#  include <AclAPI.h>
#  include <fileapi.h>
#  include <handleapi.h>
#  include <minwindef.h>
#  include <winbase.h>
#  include <winnt.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace oxygen::serio {
namespace {

#ifdef OXYGEN_WINDOWS
  //! Renames the temporary over the destination with POSIX semantics: readers
  //! holding the destination open with shared deletion keep the file they
  //! opened, and the name moves to the new file without waiting for them. A
  //! file system without POSIX renames gets a classic replacing move, which
  //! fails while any reader holds the destination.
  auto ReplaceDestination(const std::filesystem::path& temporary,
    const std::filesystem::path& destination) -> Result<AtomicFileCommit>
  {
    {
      auto* const handle
        = CreateFileW(temporary.c_str(), GENERIC_WRITE | DELETE | SYNCHRONIZE,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (handle == INVALID_HANDLE_VALUE) {
        return Result<AtomicFileCommit>::Err(base::LastSystemError());
      }
      const auto close
        = ScopeGuard([handle] noexcept -> void { CloseHandle(handle); });

      // FILE_RENAME_INFO ends with a variable-length name. FileNameLength
      // excludes the terminator, but Windows converts the name as a
      // NUL-terminated path, so the zeroed storage must hold one.
      const auto target = std::filesystem::absolute(destination).native();
      const auto name_bytes = target.size() * sizeof(wchar_t);
      const auto info_bytes
        = offsetof(FILE_RENAME_INFO, FileName) + name_bytes + sizeof(wchar_t);
      auto storage = std::vector<uint64_t>(
        (info_bytes + sizeof(uint64_t) - 1) / sizeof(uint64_t));
      // NOLINTNEXTLINE(*-reinterpret-cast) - Win32 variable-length record
      auto* const info = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
      info->Flags
        = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
      info->RootDirectory = nullptr;
      info->FileNameLength = static_cast<DWORD>(name_bytes);
      std::memcpy(
        static_cast<void*>(info->FileName), target.data(), name_bytes);
      if (SetFileInformationByHandle(handle, FileRenameInfoEx, info,
            static_cast<DWORD>(storage.size() * sizeof(uint64_t)))
        != FALSE) {
        // The handle now names the destination; flushing it commits the
        // rename as MOVEFILE_WRITE_THROUGH would.
        if (FlushFileBuffers(handle) == FALSE) {
          return Ok(
            AtomicFileCommit { .durability_error = base::LastSystemError() });
        }
        return Ok(AtomicFileCommit {});
      }
      const auto error = base::LastSystemError();
      const auto code = static_cast<DWORD>(error.value());
      if (code != ERROR_INVALID_PARAMETER && code != ERROR_NOT_SUPPORTED
        && code != ERROR_INVALID_FUNCTION) {
        return Result<AtomicFileCommit>::Err(error);
      }
    }
    if (MoveFileExW(temporary.c_str(), destination.c_str(),
          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
      == FALSE) {
      return Result<AtomicFileCommit>::Err(base::LastSystemError());
    }
    return Ok(AtomicFileCommit {});
  }

  //! Gives the temporary the destination's DACL, keeping its protection, so
  //! the rename preserves access permissions. A missing destination has none.
  auto CopyAccessControl(const std::filesystem::path& destination,
    const std::filesystem::path& temporary) -> Result<void>
  {
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto queried = GetNamedSecurityInfoW(destination.c_str(),
      SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl,
      nullptr, &descriptor);
    if (queried == ERROR_FILE_NOT_FOUND || queried == ERROR_PATH_NOT_FOUND) {
      return {};
    }
    if (queried != ERROR_SUCCESS) {
      return Result<void>::Err(
        std::error_code(static_cast<int>(queried), std::system_category()));
    }
    const auto release
      = ScopeGuard([descriptor] noexcept -> void { LocalFree(descriptor); });
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    if (GetSecurityDescriptorControl(descriptor, &control, &revision)
      == FALSE) {
      return Result<void>::Err(base::LastSystemError());
    }
    // Inherited ACEs in the copied DACL are recomputed from the shared parent.
    const DWORD protection = (control & SE_DACL_PROTECTED) != 0
      ? PROTECTED_DACL_SECURITY_INFORMATION
      : UNPROTECTED_DACL_SECURITY_INFORMATION;
    const auto applied = SetNamedSecurityInfoW(
      const_cast<wchar_t*>(temporary.c_str()), // NOLINT(*-const-cast)
      SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | protection, nullptr, nullptr,
      dacl, nullptr);
    if (applied != ERROR_SUCCESS) {
      return Result<void>::Err(
        std::error_code(static_cast<int>(applied), std::system_category()));
    }
    return {};
  }
#else
  //! Gives the temporary the destination's mode so the rename preserves access
  //! permissions. A missing destination has none.
  auto CopyAccessControl(const std::filesystem::path& destination,
    const std::filesystem::path& temporary) -> Result<void>
  {
    struct stat original {};
    if (::stat(destination.c_str(), &original) == 0
      && ::chmod(temporary.c_str(), original.st_mode) != 0) {
      return Result<void>::Err(base::LastSystemError());
    }
    return {};
  }
#endif

} // namespace

namespace {
  //! Removes the sibling unless it was published. Declared before the file
  //! and its stream, it is destroyed after them, once the handle is closed.
  struct Sibling {
    std::filesystem::path path;
    bool published = false;

    Sibling() = default;
    OXYGEN_MAKE_NON_COPYABLE(Sibling)
    OXYGEN_MAKE_NON_MOVABLE(Sibling)

    ~Sibling() noexcept
    {
      if (!published) {
        std::error_code error;
        std::filesystem::remove(path, error);
      }
    }
  };
} // namespace

struct AtomicFileWriter::State {
  State(std::filesystem::path destination_path,
    std::filesystem::path sibling_path, NativeFile opened) noexcept
    : destination(std::move(destination_path))
    , file(std::move(opened))
    , stream(file)
  {
    sibling.path = std::move(sibling_path);
  }

  OXYGEN_MAKE_NON_COPYABLE(State)
  OXYGEN_MAKE_NON_MOVABLE(State)
  ~State() noexcept = default;

  std::filesystem::path destination;
  // Keep this order. Members are destroyed in reverse: the stream (which
  // writes to `file`) first, then `file`, which closes the handle, and
  // `sibling` last, which can only delete the file once it is closed.
  Sibling sibling;
  NativeFile file;
  RegionWriteStream<NativeFile> stream;
};

AtomicFileWriter::AtomicFileWriter(std::unique_ptr<State> state) noexcept
  : state_(std::move(state))
{
}

AtomicFileWriter::~AtomicFileWriter() noexcept = default;

AtomicFileWriter::AtomicFileWriter(AtomicFileWriter&& other) noexcept = default;

auto AtomicFileWriter::operator=(AtomicFileWriter&& other) noexcept
  -> AtomicFileWriter& = default;

auto AtomicFileWriter::Create(const std::filesystem::path& path)
  -> Result<AtomicFileWriter>
{
  if (path.filename().empty()) {
    return Result<AtomicFileWriter>::Err(
      std::make_error_code(std::errc::invalid_argument));
  }
  auto temporary = base::ToNativePath(
    path.parent_path() / (".oxygen-" + Uuid::Generate().ToString() + ".tmp"));
  auto file = NativeFile::Open(temporary,
    OpenSpec {
      .access = Access::kWrite,
      .creation = Creation::kCreateNew,
      .sharing = Sharing::kNone,
      .hint = AccessHint::kSequential,
    });
  if (!file) {
    return Result<AtomicFileWriter>::Err(file.error());
  }
  return Ok(AtomicFileWriter(std::make_unique<State>(
    base::ToNativePath(path), std::move(temporary), std::move(*file))));
}

auto AtomicFileWriter::Stream() noexcept -> RegionWriteStream<NativeFile>&
{
  return state_->stream;
}

auto AtomicFileWriter::Commit() && -> Result<AtomicFileCommit>
{
  const auto state = std::move(state_);
  if (const auto flushed = state->stream.Flush(); !flushed) {
    return Result<AtomicFileCommit>::Err(flushed.error());
  }
  if (const auto synced = state->file.Flush(Durability::kStable); !synced) {
    return Result<AtomicFileCommit>::Err(synced.error());
  }
  // The replacement reopens the sibling by name, and this handle shares
  // nothing: close it. The stream holds no bytes, so it never writes again.
  {
    [[maybe_unused]] const auto closed = std::move(state->file);
  }
  const auto access
    = CopyAccessControl(state->destination, state->sibling.path);
  if (!access) {
    return Result<AtomicFileCommit>::Err(access.error());
  }
#ifdef OXYGEN_WINDOWS
  // One same-directory rename that always replaces: concurrent publishers
  // never see a missing destination or race an existence check.
  auto commit = ReplaceDestination(state->sibling.path, state->destination);
  state->sibling.published = commit.has_value();
  return commit;
#else
  if (::rename(state->sibling.path.c_str(), state->destination.c_str()) != 0) {
    return Result<AtomicFileCommit>::Err(base::LastSystemError());
  }
  state->sibling.published = true;
  const auto parent = state->destination.has_parent_path()
    ? state->destination.parent_path()
    : std::filesystem::path(".");
  const auto directory
    = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory < 0) {
    return Ok(AtomicFileCommit { .durability_error = base::LastSystemError() });
  }
  const auto close = ScopeGuard([directory]() noexcept { ::close(directory); });
  if (::fsync(directory) != 0) {
    return Ok(AtomicFileCommit { .durability_error = base::LastSystemError() });
  }
  return Ok(AtomicFileCommit {});
#endif
}

auto WriteFileAtomically(const std::filesystem::path& path,
  const std::span<const std::byte> bytes) -> Result<AtomicFileCommit>
{
  auto writer = AtomicFileWriter::Create(path);
  if (!writer) {
    return Result<AtomicFileCommit>::Err(writer.error());
  }
  if (const auto written = writer->Stream().Write(bytes); !written) {
    return Result<AtomicFileCommit>::Err(written.error());
  }
  return std::move(*writer).Commit();
}

auto OpenPublishedFile(const std::filesystem::path& path) -> Result<NativeFile>
{
  return NativeFile::Open(path,
    OpenSpec {
      .sharing = Sharing::kReadWriteDelete,
      .hint = AccessHint::kSequential,
    });
}

auto ReadPublishedFile(const std::filesystem::path& path)
  -> Result<std::vector<std::byte>>
{
  const auto file = OpenPublishedFile(path);
  if (!file) {
    return Result<std::vector<std::byte>>::Err(file.error());
  }
  const auto size = file->Size();
  if (!size) {
    return Result<std::vector<std::byte>>::Err(size.error());
  }
  if (size->get() > std::numeric_limits<size_t>::max()) {
    return Result<std::vector<std::byte>>::Err(
      std::make_error_code(std::errc::file_too_large));
  }
  auto bytes = std::vector<std::byte>(static_cast<size_t>(size->get()));
  if (const auto read = ReadExactlyAt(*file, ByteOffset { 0 }, bytes); !read) {
    return Result<std::vector<std::byte>>::Err(read.error());
  }
  return Ok(std::move(bytes));
}

} // namespace oxygen::serio
