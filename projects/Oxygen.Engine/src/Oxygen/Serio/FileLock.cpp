//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>

#ifdef _WIN32
#  include <Windows.h> // IWYU pragma: keep

#  include <fileapi.h>
#  include <handleapi.h>
#  include <minwinbase.h>
#  include <minwindef.h>
#  include <winerror.h>
#  include <winnt.h>
#else
#  include <cerrno>

#  include <fcntl.h>
#  include <sys/file.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/SystemError.h>
#include <Oxygen/Serio/FileLock.h>

namespace oxygen::serio {

#ifdef _WIN32
namespace {
  //! Locks or unlocks every byte a lock covers.
  constexpr DWORD kWholeRange = MAXDWORD;

  //! Removes the file `handle` names at once, even while other handles hold
  //! it, so no open finds it pending deletion; a file system without POSIX
  //! deletes removes it when its last handle closes.
  auto RemoveNow(HANDLE handle) noexcept -> void
  {
    auto posix = FILE_DISPOSITION_INFO_EX {};
    posix.Flags = static_cast<DWORD>(FILE_DISPOSITION_FLAG_DELETE)
      | static_cast<DWORD>(FILE_DISPOSITION_FLAG_POSIX_SEMANTICS);
    if (SetFileInformationByHandle(
          handle, FileDispositionInfoEx, &posix, sizeof(posix))
      != FALSE) {
      return;
    }
    auto classic = FILE_DISPOSITION_INFO {};
    classic.DeleteFile = TRUE;
    static_cast<void>(SetFileInformationByHandle(
      handle, FileDispositionInfo, &classic, sizeof(classic)));
  }

  //! Whether `path` still names the file `handle` holds: busy when another
  //! holder removed it, or the path names another file now; the error when
  //! either cannot be read.
  auto StillNamed(HANDLE handle, const std::filesystem::path& path) noexcept
    -> std::error_code
  {
    auto held = FILE_ID_INFO {};
    if (GetFileInformationByHandleEx(handle, FileIdInfo, &held, sizeof(held))
      == FALSE) {
      return base::LastSystemError();
    }
    auto* const named = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
      static_cast<DWORD>(FILE_SHARE_READ) | static_cast<DWORD>(FILE_SHARE_WRITE)
        | static_cast<DWORD>(FILE_SHARE_DELETE),
      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (named == INVALID_HANDLE_VALUE) {
      const auto error = base::LastSystemError();
      const auto code = static_cast<DWORD>(error.value());
      return code == ERROR_FILE_NOT_FOUND
        ? std::make_error_code(std::errc::device_or_resource_busy)
        : error;
    }
    auto current = FILE_ID_INFO {};
    const auto read = GetFileInformationByHandleEx(
      named, FileIdInfo, &current, sizeof(current));
    const auto error
      = read == FALSE ? base::LastSystemError() : std::error_code {};
    static_cast<void>(CloseHandle(named));
    if (error) {
      return error;
    }
    const auto same = held.VolumeSerialNumber == current.VolumeSerialNumber
      && std::ranges::equal(held.FileId.Identifier, current.FileId.Identifier);
    return same ? std::error_code {}
                : std::make_error_code(std::errc::device_or_resource_busy);
  }
} // namespace
#endif

struct FileLock::Impl final {
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
  //! A temporary lock removes its file when it is the last holder.
  bool temporary = false;
#else
  int handle = -1;
  //! The file a temporary lock removes when it is the last holder.
  std::filesystem::path temporary;
#endif

  Impl() = default;
  OXYGEN_MAKE_NON_COPYABLE(Impl)
  OXYGEN_MAKE_NON_MOVABLE(Impl)

  ~Impl() noexcept
  {
#ifdef _WIN32
    if (handle != INVALID_HANDLE_VALUE) {
      // As on POSIX, only the last holder removes the file, proven by the
      // exclusive lock it takes once it released its own.
      if (temporary) {
        auto held = OVERLAPPED {};
        static_cast<void>(
          UnlockFileEx(handle, 0, kWholeRange, kWholeRange, &held));
        auto last = OVERLAPPED {};
        if (LockFileEx(handle,
              static_cast<DWORD>(LOCKFILE_EXCLUSIVE_LOCK)
                | static_cast<DWORD>(LOCKFILE_FAIL_IMMEDIATELY),
              0, kWholeRange, kWholeRange, &last)
          != FALSE) {
          RemoveNow(handle);
        }
      }
      static_cast<void>(CloseHandle(handle));
    }
#else
    if (handle >= 0) {
      // Only the last holder removes the file: removing it under another
      // shared holder would let a new file at the path take an exclusive lock
      // beside that holder's. The exclusive lock proves nobody else holds
      // this file. Holders releasing together may all fail to take it and
      // leave the file, which the next acquisition locks again.
      if (!temporary.empty()
        && flock(handle,
             static_cast<int>(
               static_cast<unsigned>(LOCK_EX) | static_cast<unsigned>(LOCK_NB)))
          == 0) {
        static_cast<void>(unlink(temporary.c_str()));
      }
      static_cast<void>(close(handle));
    }
#endif
  }
};

FileLock::FileLock() noexcept = default;
FileLock::~FileLock() = default;
FileLock::FileLock(FileLock&&) noexcept = default;
auto FileLock::operator=(FileLock&&) noexcept -> FileLock& = default;

auto FileLock::TryAcquire(const std::filesystem::path& path,
  const FileLockMode mode, const FileLockOpenMode open_mode) -> Result<FileLock>
{
  return Lock(path, mode, open_mode, Contention::kFail);
}

auto FileLock::Acquire(const std::filesystem::path& path,
  const FileLockMode mode, const FileLockOpenMode open_mode) -> Result<FileLock>
{
  if (open_mode == FileLockOpenMode::kTemporary) {
    return Err(std::errc::invalid_argument);
  }
  return Lock(path, mode, open_mode, Contention::kWait);
}

auto FileLock::Lock(const std::filesystem::path& path, const FileLockMode mode,
  const FileLockOpenMode open_mode, const Contention contention)
  -> Result<FileLock>
{
  if (path.empty()
    || (mode != FileLockMode::kShared && mode != FileLockMode::kExclusive)
    || (open_mode != FileLockOpenMode::kExisting
      && open_mode != FileLockOpenMode::kOpenOrCreate
      && open_mode != FileLockOpenMode::kTemporary)) {
    return Err(std::errc::invalid_argument);
  }
  auto owner = std::make_unique<Impl>();
#ifdef _WIN32
  const auto temporary = open_mode == FileLockOpenMode::kTemporary;
  // A temporary lock asks for deletion, which its last holder performs.
  const DWORD access
    = (mode == FileLockMode::kExclusive
          ? static_cast<DWORD>(GENERIC_READ) | static_cast<DWORD>(GENERIC_WRITE)
          : static_cast<DWORD>(GENERIC_READ))
    | (temporary ? static_cast<DWORD>(DELETE) : 0U);
  const auto native_path = base::ToNativePath(path);
  owner->handle = CreateFileW(native_path.c_str(), access,
    static_cast<DWORD>(FILE_SHARE_READ) | static_cast<DWORD>(FILE_SHARE_WRITE)
      | static_cast<DWORD>(FILE_SHARE_DELETE),
    nullptr,
    open_mode == FileLockOpenMode::kExisting ? OPEN_EXISTING : OPEN_ALWAYS,
    FILE_ATTRIBUTE_NORMAL, nullptr);
  if (owner->handle == INVALID_HANDLE_VALUE) {
    // Every holder shares everything: a sharing violation is a handle of
    // another program. A denied access is the caller's permission, never
    // contention: no file is left pending deletion.
    const auto error = base::LastSystemError();
    return static_cast<DWORD>(error.value()) == ERROR_SHARING_VIOLATION
      ? Err(std::errc::device_or_resource_busy)
      : Err(error);
  }
  OVERLAPPED overlapped {};
  const DWORD flags = (contention == Contention::kFail
                          ? static_cast<DWORD>(LOCKFILE_FAIL_IMMEDIATELY)
                          : 0U)
    | (mode == FileLockMode::kExclusive
        ? static_cast<DWORD>(LOCKFILE_EXCLUSIVE_LOCK)
        : 0U);
  if (LockFileEx(owner->handle, flags, 0, kWholeRange, kWholeRange, &overlapped)
    == FALSE) {
    const auto error = base::LastSystemError();
    return static_cast<DWORD>(error.value()) == ERROR_LOCK_VIOLATION
      ? Err(std::errc::device_or_resource_busy)
      : Err(error);
  }
  if (temporary) {
    // The last holder removes the file before it releases, so the locked
    // file must still be the one at the path.
    if (const auto named = StillNamed(owner->handle, native_path); named) {
      return Err(named);
    }
    owner->temporary = true;
  }
#else
  const auto flags = static_cast<unsigned>(
                       mode == FileLockMode::kExclusive ? O_RDWR : O_RDONLY)
    | static_cast<unsigned>(O_CLOEXEC)
    | static_cast<unsigned>(
      open_mode == FileLockOpenMode::kExisting ? 0 : O_CREAT);
  owner->handle = open(path.c_str(), static_cast<int>(flags),
    static_cast<mode_t>(S_IRUSR) | static_cast<mode_t>(S_IWUSR));
  if (owner->handle < 0) {
    return Err(base::LastSystemError());
  }
  const auto operation = static_cast<unsigned>(
                           mode == FileLockMode::kExclusive ? LOCK_EX : LOCK_SH)
    | static_cast<unsigned>(contention == Contention::kFail ? LOCK_NB : 0);
  if (flock(owner->handle, static_cast<int>(operation)) != 0) {
    const auto error = base::LastSystemError();
    return error.value() == EWOULDBLOCK || error.value() == EAGAIN
      ? Err(std::errc::device_or_resource_busy)
      : Err(error);
  }
  if (open_mode == FileLockOpenMode::kTemporary) {
    // The last holder removes the file before it releases, so the locked
    // file must still be the one at the path; otherwise a holder removed it
    // while this acquisition opened it. Failures to compare are errors.
    struct stat held {};
    if (fstat(owner->handle, &held) != 0) {
      return Err(base::LastSystemError());
    }
    struct stat named {};
    if (stat(path.c_str(), &named) != 0) {
      const auto error = base::LastSystemError();
      return error.value() == ENOENT ? Err(std::errc::device_or_resource_busy)
                                     : Err(error);
    }
    if (held.st_ino != named.st_ino || held.st_dev != named.st_dev) {
      return Err(std::errc::device_or_resource_busy);
    }
    owner->temporary = path;
  }
#endif
  FileLock result;
  result.impl_ = std::move(owner);
  return Ok(std::move(result));
}

} // namespace oxygen::serio
