//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

#include <Oxygen/Host/PersistenceLock.h>
#include <Oxygen/Serio/FileLock.h>

namespace oxygen::host {

namespace {
  constexpr auto kLockFileName = "instance.lock";
} // namespace

struct PersistenceLock::Handle {
  serio::FileLock lock;
};

PersistenceLock::PersistenceLock(const std::filesystem::path& folder)
{
  auto error = std::error_code {};
  std::filesystem::create_directories(folder, error);
  // The lock file exists only while the owner holds it, and the operating
  // system releases it however the owning process ends.
  auto lock = serio::FileLock::TryAcquire(folder / kLockFileName,
    serio::FileLockMode::kExclusive, serio::FileLockOpenMode::kTemporary);
  if (lock) {
    handle_
      = std::make_unique<Handle>(Handle { .lock = std::move(lock).value() });
  }
}

PersistenceLock::~PersistenceLock() = default;

} // namespace oxygen::host
