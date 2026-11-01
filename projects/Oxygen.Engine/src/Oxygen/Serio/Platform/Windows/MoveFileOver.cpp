//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Windows.h> // IWYU pragma: keep

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <system_error>
#include <vector>

#include <AccCtrl.h>
#include <AclAPI.h>
#include <fileapi.h>
#include <handleapi.h>
#include <minwindef.h>
#include <winbase.h>
#include <winnt.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/SystemError.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/Detail/Publication.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

namespace {

  //! One move of `from` over `to`, named so the two paths cannot be swapped.
  struct Move {
    observer_ptr<const std::filesystem::path> from;
    observer_ptr<const std::filesystem::path> to;
    Durability durability;
  };

  //! Commits the entries of `folder`, a rename into it among them.
  auto FlushFolder(const std::filesystem::path& folder) noexcept
    -> std::error_code
  {
    auto* const handle = CreateFileW(folder.c_str(), GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      return base::LastSystemError();
    }
    const auto close
      = ScopeGuard([handle] noexcept -> void { CloseHandle(handle); });
    if (FlushFileBuffers(handle) == FALSE) {
      return base::LastSystemError();
    }
    return {};
  }

  //! Renames `move.from` over `move.to` through a handle with POSIX
  //! semantics. Returns nothing when the file system has no POSIX renames, so
  //! the caller falls back to a replacing move.
  /*!
   The caller has flushed and closed `move.from`. The handle asks only for
   deletion: it names `move.to` once renamed, and a handle that could write
   would fail every reader not sharing writes until it closes.
  */
  auto PosixRename(const Move& move) noexcept
    -> std::optional<Result<FileReplacement>>
  {
    const auto& [from, to, durability] = move;
    auto* const handle = CreateFileW(from->c_str(), DELETE | SYNCHRONIZE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      return Result<FileReplacement>::Err(base::LastSystemError());
    }
    const auto close
      = ScopeGuard([handle] noexcept -> void { CloseHandle(handle); });

    auto absolute_error = std::error_code {};
    const auto target = std::filesystem::absolute(*to, absolute_error).native();
    if (absolute_error) {
      return Result<FileReplacement>::Err(absolute_error);
    }
    // FILE_RENAME_INFO ends with a variable-length name. FileNameLength
    // excludes the terminator, but Windows converts the name as a
    // NUL-terminated path, so the zeroed storage must hold one.
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
    std::memcpy(static_cast<void*>(info->FileName), target.data(), name_bytes);
    if (SetFileInformationByHandle(handle, FileRenameInfoEx, info,
          static_cast<DWORD>(storage.size() * sizeof(uint64_t)))
      != FALSE) {
      // The rename is an entry of the destination's folder: flushing the
      // folder commits it.
      if (durability == Durability::kStable) {
        const auto folder = std::filesystem::path(target).parent_path();
        if (const auto flushed = FlushFolder(folder); flushed) {
          return Ok(FileReplacement { .durability_error = flushed });
        }
      }
      return Ok(FileReplacement {});
    }
    const auto error = base::LastSystemError();
    const auto code = static_cast<DWORD>(error.value());
    if (code != ERROR_INVALID_PARAMETER && code != ERROR_NOT_SUPPORTED
      && code != ERROR_INVALID_FUNCTION) {
      return Result<FileReplacement>::Err(error);
    }
    return std::nullopt;
  }

} // namespace

auto MoveFileOver(const std::filesystem::path& from,
  const std::filesystem::path& to, const Durability durability) noexcept
  -> Result<FileReplacement>
{
  if (auto renamed = PosixRename(Move {
        .from = make_observer(&from),
        .to = make_observer(&to),
        .durability = durability,
      })) {
    if (!*renamed) {
      return Result<FileReplacement>::Err(
        detail::ClassifyReplaceError(from, to, renamed->error()));
    }
    return *renamed;
  }
  // No POSIX renames: a classic replacing move, which fails while any reader
  // holds the destination.
  const DWORD flags = durability == Durability::kStable
    ? MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
    : MOVEFILE_REPLACE_EXISTING;
  if (MoveFileExW(from.c_str(), to.c_str(), flags) == FALSE) {
    return Result<FileReplacement>::Err(
      detail::ClassifyReplaceError(from, to, base::LastSystemError()));
  }
  return Ok(FileReplacement {});
}

namespace detail {

  //! Gives the temporary the destination's DACL, keeping its protection.
  auto CopyAccessControl(const std::filesystem::path& destination,
    const std::filesystem::path& temporary) noexcept -> Result<void>
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

} // namespace detail

} // namespace oxygen::serio
