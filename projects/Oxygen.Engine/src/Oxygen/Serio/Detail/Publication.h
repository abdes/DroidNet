//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <system_error>

#include <Oxygen/Base/Result.h>

namespace oxygen::serio::detail {

//! Gives `temporary` the access permissions of `destination`, so a
//! replacement keeps them. A missing destination has none to give.
//! Platform code.
[[nodiscard]] auto CopyAccessControl(const std::filesystem::path& destination,
  const std::filesystem::path& temporary) noexcept -> Result<void>;

//! The error a failed move of `from` over `to` reports: `kIsDirectory` when
//! either names a folder, `error` otherwise. Classified after the failure, so
//! nothing is checked before the move.
[[nodiscard]] auto ClassifyReplaceError(const std::filesystem::path& from,
  const std::filesystem::path& to, std::error_code error) noexcept
  -> std::error_code;

} // namespace oxygen::serio::detail
