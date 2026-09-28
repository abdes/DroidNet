//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <system_error>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/api_export.h>

namespace oxygen::serio {

//! A successful atomic replacement. A durability error means the replacement
//! committed, but synchronizing its parent directory failed afterward.
struct AtomicFileCommit {
  std::error_code durability_error {};
};

//! Writes and flushes a unique sibling file before atomically replacing the
//! destination. The parent directory must exist. Existing destination access
//! permissions are preserved. Callers own locking and expected-revision checks.
OXGN_SERIO_NDAPI auto WriteFileAtomically(const std::filesystem::path& path,
  std::span<const std::byte> bytes) -> Result<AtomicFileCommit>;

} // namespace oxygen::serio
