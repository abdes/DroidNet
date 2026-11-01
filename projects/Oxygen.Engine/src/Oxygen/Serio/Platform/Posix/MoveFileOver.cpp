//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <filesystem>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/SystemError.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/Detail/Publication.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

auto MoveFileOver(const std::filesystem::path& from,
  const std::filesystem::path& to, const Durability durability) noexcept
  -> Result<FileReplacement>
{
  if (::rename(from.c_str(), to.c_str()) != 0) {
    return Result<FileReplacement>::Err(
      detail::ClassifyReplaceError(from, to, base::LastSystemError()));
  }
  if (durability != Durability::kStable) {
    return Ok(FileReplacement {});
  }
  // The rename is durable once the directory that names the file is.
  const auto parent
    = to.has_parent_path() ? to.parent_path() : std::filesystem::path(".");
  const auto directory
    = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory < 0) {
    return Ok(FileReplacement { .durability_error = base::LastSystemError() });
  }
  const auto close
    = ScopeGuard([directory] noexcept -> void { ::close(directory); });
  if (::fsync(directory) != 0) {
    return Ok(FileReplacement { .durability_error = base::LastSystemError() });
  }
  return Ok(FileReplacement {});
}

namespace detail {

  //! Gives the temporary the destination's mode.
  auto CopyAccessControl(const std::filesystem::path& destination,
    const std::filesystem::path& temporary) noexcept -> Result<void>
  {
    struct stat original {};
    if (::stat(destination.c_str(), &original) == 0
      && ::chmod(temporary.c_str(), original.st_mode) != 0) {
      return Result<void>::Err(base::LastSystemError());
    }
    return {};
  }

} // namespace detail

} // namespace oxygen::serio
