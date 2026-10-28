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
#include <vector>

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
/*!
 The replacement succeeds while readers hold the destination open, as long as
 they share it for deletion, as `ReadPublishedFile` does: they keep the bytes
 they opened. On Windows, a reader that does not share deletion makes the
 replacement fail with access denied.
*/
OXGN_SERIO_NDAPI auto WriteFileAtomically(const std::filesystem::path& path,
  std::span<const std::byte> bytes) -> Result<AtomicFileCommit>;

//! Reads a whole file that `WriteFileAtomically` publishes.
/*!
 The file is opened sharing reads, writes and deletion, so a publication
 during the read replaces the file instead of failing; the read returns the
 bytes of the file it opened.
*/
OXGN_SERIO_NDAPI auto ReadPublishedFile(const std::filesystem::path& path)
  -> Result<std::vector<std::byte>>;

} // namespace oxygen::serio
