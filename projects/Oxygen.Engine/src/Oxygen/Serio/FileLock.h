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
enum class FileLockOpenMode : uint8_t { kExisting, kOpenOrCreate };

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

  [[nodiscard]] auto IsLocked() const noexcept -> bool
  {
    return impl_ != nullptr;
  }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::serio
