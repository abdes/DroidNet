//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/api_export.h>

namespace oxygen::serio {

enum class FileLockMode : uint8_t { kShared, kExclusive };
//! How a lock opens its file.
/*!
 - `kExisting`: the file must exist, as a lease marker a generation owns does.
 - `kOpenOrCreate`: the file is created if missing and stays after release.
 - `kTemporary`: the file exists only while it is locked. It is created if
   missing and removed when the last holder releases it, so a mutex leaves no
   file behind. A contender that races the removal reports contention. Holders
   that release together, or a killed holder, may leave the file, which the
   next acquisition locks again. A denied access is always the caller's
   permission (`kAccessDenied`), never contention.
*/
enum class FileLockOpenMode : uint8_t { kExisting, kOpenOrCreate, kTemporary };

//! A movable, nonblocking operating-system file lock.
class FileLock final {
public:
  OXGN_SERIO_API FileLock() noexcept;
  OXGN_SERIO_API ~FileLock();
  OXYGEN_MAKE_NON_COPYABLE(FileLock)
  OXGN_SERIO_API FileLock(FileLock&&) noexcept;
  OXGN_SERIO_API auto operator=(FileLock&&) noexcept -> FileLock&;

  //! Contention returns device_or_resource_busy; other I/O errors are
  //! preserved.
  OXGN_SERIO_NDAPI static auto TryAcquire(const std::filesystem::path& path,
    FileLockMode mode, FileLockOpenMode open_mode = FileLockOpenMode::kExisting)
    -> Result<FileLock>;

  //! Waits until the lock is granted. Each acquisition opens its own handle,
  //! so threads of one process exclude each other as processes do. A
  //! temporary lock file cannot be waited for (`invalid_argument`): its
  //! holder removes it.
  OXGN_SERIO_NDAPI static auto Acquire(const std::filesystem::path& path,
    FileLockMode mode, FileLockOpenMode open_mode = FileLockOpenMode::kExisting)
    -> Result<FileLock>;

  [[nodiscard]] auto IsLocked() const noexcept -> bool
  {
    return impl_ != nullptr;
  }

private:
  struct Impl;

  //! Whether an acquisition fails on contention or waits it out.
  enum class Contention : uint8_t { kFail, kWait };

  static auto Lock(const std::filesystem::path& path, FileLockMode mode,
    FileLockOpenMode open_mode, Contention contention) -> Result<FileLock>;

  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::serio
