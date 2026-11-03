//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <system_error>

#include <Oxygen/Data/CookedFormatError.h>

namespace oxygen::content::internal {

//! Throws for the cooked artifact `path`, a pak or a loose cooked index, that
//! could not be opened with `error`.
/*!
 A missing artifact is refused as `CookedDefect::kAbsent`, as `CookedContent`
 refuses it; any other I/O failure, such as a sharing violation or a denied
 access, stays a `std::system_error`.
*/
[[noreturn]] inline auto ThrowOpenFailure(
  const std::error_code& error, const std::filesystem::path& path) -> void
{
  if (error == std::errc::no_such_file_or_directory) {
    throw data::CookedFormatError(data::CookedDefect::kAbsent,
      "'" + path.generic_string() + "' does not exist");
  }
  throw std::system_error(error, "open '" + path.string() + "'");
}

} // namespace oxygen::content::internal
