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

#  include <errhandlingapi.h>
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
#include <Oxygen/Serio/FileLock.h>

namespace oxygen::serio {

struct FileLock::Impl final {
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
#else
  int handle = -1;
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
  if (path.empty()
    || (mode != FileLockMode::kShared && mode != FileLockMode::kExclusive)
    || (open_mode != FileLockOpenMode::kExisting
      && open_mode != FileLockOpenMode::kOpenOrCreate)) {
    return Err(std::errc::invalid_argument);
  }
  auto owner = std::make_unique<Impl>();
#ifdef _WIN32
  const DWORD access = mode == FileLockMode::kExclusive
    ? static_cast<DWORD>(GENERIC_READ) | static_cast<DWORD>(GENERIC_WRITE)
    : static_cast<DWORD>(GENERIC_READ);
  const auto native_path = base::ToNativePath(path);
  owner->handle = CreateFileW(native_path.c_str(), access,
    static_cast<DWORD>(FILE_SHARE_READ) | static_cast<DWORD>(FILE_SHARE_WRITE)
      | static_cast<DWORD>(FILE_SHARE_DELETE),
    nullptr,
    open_mode == FileLockOpenMode::kOpenOrCreate ? OPEN_ALWAYS : OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL, nullptr);
  if (owner->handle == INVALID_HANDLE_VALUE) {
    const auto error = GetLastError();
    return error == ERROR_SHARING_VIOLATION
      ? Err(std::errc::device_or_resource_busy)
      : Err(std::error_code(static_cast<int>(error), std::system_category()));
  }
  OVERLAPPED overlapped {};
  const DWORD flags = static_cast<DWORD>(LOCKFILE_FAIL_IMMEDIATELY)
    | (mode == FileLockMode::kExclusive
        ? static_cast<DWORD>(LOCKFILE_EXCLUSIVE_LOCK)
        : 0U);
  if (!LockFileEx(owner->handle, flags, 0, MAXDWORD, MAXDWORD, &overlapped)) {
    const auto error = GetLastError();
    return error == ERROR_LOCK_VIOLATION
      ? Err(std::errc::device_or_resource_busy)
      : Err(std::error_code(static_cast<int>(error), std::system_category()));
  }
#else
  const auto flags = static_cast<unsigned>(
                       mode == FileLockMode::kExclusive ? O_RDWR : O_RDONLY)
    | static_cast<unsigned>(O_CLOEXEC)
    | static_cast<unsigned>(
      open_mode == FileLockOpenMode::kOpenOrCreate ? O_CREAT : 0);
  owner->handle = open(path.c_str(), static_cast<int>(flags),
    static_cast<mode_t>(S_IRUSR) | static_cast<mode_t>(S_IWUSR));
  if (owner->handle < 0) {
    return Err(std::error_code(errno, std::generic_category()));
  }
  const auto operation = static_cast<unsigned>(
                           mode == FileLockMode::kExclusive ? LOCK_EX : LOCK_SH)
    | static_cast<unsigned>(LOCK_NB);
  if (flock(owner->handle, static_cast<int>(operation)) != 0) {
    const auto error = errno;
    return error == EWOULDBLOCK || error == EAGAIN
      ? Err(std::errc::device_or_resource_busy)
      : Err(std::error_code(error, std::generic_category()));
  }
#endif
  FileLock result;
  result.impl_ = std::move(owner);
  return Ok(std::move(result));
}

} // namespace oxygen::serio
