//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <new>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/SystemError.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/Detail/Publication.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

namespace {

  //! Commits the entries of `folder`, a rename into or out of it among them.
  auto FlushFolder(const std::filesystem::path& folder) noexcept
    -> std::error_code
  {
    const auto directory
      = ::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) {
      return base::LastSystemError();
    }
    const auto close
      = ScopeGuard([directory] noexcept -> void { ::close(directory); });
    if (::fsync(directory) != 0) {
      return base::LastSystemError();
    }
    return {};
  }

} // namespace

auto MoveFileOver(const std::filesystem::path& from,
  const std::filesystem::path& to, const Durability durability) noexcept
  -> Result<FileReplacement>
{
  try {
    // `rename(2)` moves folders too; only a file is published. The source is
    // the caller's own file, so nothing replaces it between check and move.
    struct stat source {};
    if (::lstat(from.c_str(), &source) != 0) {
      return Result<FileReplacement>::Err(base::LastSystemError());
    }
    if (S_ISDIR(source.st_mode)) {
      return Result<FileReplacement>::Err(make_error_code(Error::kIsDirectory));
    }
    if (::rename(from.c_str(), to.c_str()) != 0) {
      return Result<FileReplacement>::Err(
        detail::ClassifyReplaceError(from, to, base::LastSystemError()));
    }
    if (durability != Durability::kStable) {
      return Ok(FileReplacement {});
    }
    // The rename is durable once the folders that name the file are.
    const auto folders = detail::FoldersOf(
      std::filesystem::absolute(from), std::filesystem::absolute(to));
    if (const auto flushed = FlushFolder(folders.destination); flushed) {
      return Ok(FileReplacement { .durability_error = flushed });
    }
    if (folders.source) {
      if (const auto flushed = FlushFolder(*folders.source); flushed) {
        return Ok(FileReplacement { .durability_error = flushed });
      }
    }
    return Ok(FileReplacement {});
  } catch (const std::bad_alloc&) {
    return Result<FileReplacement>::Err(
      std::make_error_code(std::errc::not_enough_memory));
  } catch (const std::filesystem::filesystem_error& error) {
    return Result<FileReplacement>::Err(error.code());
  }
}

namespace detail {

  //! Gives the temporary the destination's mode, through a descriptor that
  //! then commits the change when `durability` is stable.
  auto CopyAccessControl(const std::filesystem::path& destination,
    const std::filesystem::path& temporary,
    const Durability durability) noexcept -> Result<void>
  {
    struct stat original {};
    if (::stat(destination.c_str(), &original) != 0) {
      if (errno == ENOENT) {
        return {};
      }
      return Result<void>::Err(base::LastSystemError());
    }
    const auto descriptor = ::open(temporary.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
      return Result<void>::Err(base::LastSystemError());
    }
    const auto close
      = ScopeGuard([descriptor] noexcept -> void { ::close(descriptor); });
    if (::fchmod(descriptor, original.st_mode & 07777) != 0) {
      return Result<void>::Err(base::LastSystemError());
    }
    if (durability == Durability::kStable && ::fsync(descriptor) != 0) {
      return Result<void>::Err(base::LastSystemError());
    }
    return {};
  }

} // namespace detail

} // namespace oxygen::serio
