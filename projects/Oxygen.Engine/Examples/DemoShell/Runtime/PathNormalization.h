//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>

#include <Oxygen/Base/Filesystem.h>

namespace oxygen::examples::runtime {

//! Normalize a path for identity comparisons and persisted keys.
/*!
 Uses `weakly_canonical` when possible and falls back to lexical normalization
 when path components do not exist yet.
*/
inline auto NormalizePath(const std::filesystem::path& path)
  -> std::filesystem::path
{
  std::error_code ec;
  auto normalized = base::ToLogicalPath(
    std::filesystem::weakly_canonical(base::ToNativePath(path), ec));
  if (ec) {
    normalized = path.lexically_normal();
  }
  return normalized;
}

} // namespace oxygen::examples::runtime
