//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>

#include <Oxygen/Base/api_export.h>

namespace oxygen::base {

//! Convert a logical path at a physical filesystem boundary.
//! Windows paths become absolute, normalized extended-length paths; explicit
//! device/extended paths are preserved. Empty and non-Windows paths are
//! unchanged. Keep the original path for identities, persisted records and
//! diagnostics.
OXGN_BASE_NDAPI auto ToNativePath(const std::filesystem::path& path)
  -> std::filesystem::path;

//! Remove Windows drive/UNC extended-length prefixes from filesystem results.
//! Device paths without a normal drive/UNC spelling remain unchanged.
OXGN_BASE_NDAPI auto ToLogicalPath(const std::filesystem::path& path)
  -> std::filesystem::path;

} // namespace oxygen::base
