//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>
#include <system_error>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h> // IWYU pragma: keep
#else
#  include <fcntl.h>
#  include <sys/file.h>
#  include <unistd.h>
#endif

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Host/PersistenceLock.h>

namespace oxygen::host {

namespace {
  constexpr auto kLockFileName = "instance.lock";
} // namespace

#ifdef _WIN32
struct PersistenceLock::Handle {
  HANDLE file { INVALID_HANDLE_VALUE };
  Handle() = default;
  ~Handle() { CloseHandle(file); }
  OXYGEN_MAKE_NON_COPYABLE(Handle)
  OXYGEN_MAKE_NON_MOVABLE(Handle)
};
#else
struct PersistenceLock::Handle {
  int file { -1 };
  Handle() = default;
  ~Handle() { close(file); }
  OXYGEN_MAKE_NON_COPYABLE(Handle)
  OXYGEN_MAKE_NON_MOVABLE(Handle)
};
#endif

PersistenceLock::PersistenceLock(const std::filesystem::path& folder)
{
  auto error = std::error_code {};
  std::filesystem::create_directories(folder, error);
  const auto path = folder / kLockFileName;
#ifdef _WIN32
  // No sharing: a second open fails while the owner holds the file, and the
  // file goes when the owner's handle closes, however the process ends.
  auto* const file
    = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    handle_ = std::make_unique<Handle>();
    handle_->file = file;
  }
#else
  // NOLINTNEXTLINE(*-vararg) - POSIX open
  const auto file = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (file >= 0) {
    if (flock(file, LOCK_EX | LOCK_NB) == 0) {
      handle_ = std::make_unique<Handle>();
      handle_->file = file;
    } else {
      close(file);
    }
  }
#endif
}

PersistenceLock::~PersistenceLock() = default;

} // namespace oxygen::host
