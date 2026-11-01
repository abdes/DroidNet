//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

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

struct FileLock::Impl final {
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
#else
  int handle = -1;
  //! The file a temporary lock removes, while it still holds the lock.
  std::filesystem::path temporary;
#endif

  Impl() = default;
  OXYGEN_MAKE_NON_COPYABLE(Impl)
  OXYGEN_MAKE_NON_MOVABLE(Impl)

  ~Impl() noexcept
  {
#ifdef _WIN32
    if (handle != INVALID_HANDLE_VALUE) {
      static_cast<void>(CloseHandle(handle));
    }
#else
    if (handle >= 0) {
      if (!temporary.empty()) {
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
  // A temporary lock's file is deleted when its last handle closes.
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
    static_cast<DWORD>(FILE_ATTRIBUTE_NORMAL)
      | (temporary ? static_cast<DWORD>(FILE_FLAG_DELETE_ON_CLOSE) : 0U),
    nullptr);
  if (owner->handle == INVALID_HANDLE_VALUE) {
    const auto error = base::LastSystemError();
    const auto code = static_cast<DWORD>(error.value());
    // A file another holder is removing refuses to open until it is gone.
    const auto contended = code == ERROR_SHARING_VIOLATION
      || (temporary && code == ERROR_ACCESS_DENIED);
    return contended ? Err(std::errc::device_or_resource_busy) : Err(error);
  }
  OVERLAPPED overlapped {};
  const DWORD flags = (contention == Contention::kFail
                          ? static_cast<DWORD>(LOCKFILE_FAIL_IMMEDIATELY)
                          : 0U)
    | (mode == FileLockMode::kExclusive
        ? static_cast<DWORD>(LOCKFILE_EXCLUSIVE_LOCK)
        : 0U);
  if (LockFileEx(owner->handle, flags, 0, MAXDWORD, MAXDWORD, &overlapped)
    == FALSE) {
    const auto error = base::LastSystemError();
    return static_cast<DWORD>(error.value()) == ERROR_LOCK_VIOLATION
      ? Err(std::errc::device_or_resource_busy)
      : Err(error);
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
    // A holder removes the file before it releases, so the locked file must
    // still be the one at the path; otherwise another holder removed it.
    struct stat held {};
    struct stat named {};
    if (fstat(owner->handle, &held) != 0 || stat(path.c_str(), &named) != 0
      || held.st_ino != named.st_ino || held.st_dev != named.st_dev) {
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
