//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <system_error>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio::detail {

//! Gives `temporary` the access permissions of `destination`, so a
//! replacement keeps them, made as durable as `durability` asks. A missing
//! destination has none to give; any other failure to read them is an error.
//! Platform code.
[[nodiscard]] auto CopyAccessControl(const std::filesystem::path& destination,
  const std::filesystem::path& temporary, Durability durability) noexcept
  -> Result<void>;

//! The folders a move of `from` to `to` changes: the destination's, and the
//! source's when it is another folder. Both are flushed for a stable move.
struct MovedFolders {
  std::filesystem::path destination;
  std::optional<std::filesystem::path> source;
};

//! The folders `MoveFileOver` flushes, from absolute paths.
[[nodiscard]] auto FoldersOf(const std::filesystem::path& from,
  const std::filesystem::path& to) -> MovedFolders;

//! The error a failed move of `from` over `to` reports: `kIsDirectory` when
//! either names a folder, `error` otherwise. Classified after the failure, so
//! nothing is checked before the move.
[[nodiscard]] auto ClassifyReplaceError(const std::filesystem::path& from,
  const std::filesystem::path& to, std::error_code error) noexcept
  -> std::error_code;

} // namespace oxygen::serio::detail
