//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <Oxygen/Graphics/Common/Texture.h>

namespace oxygen::vortex {
//! Untimed render-thread inspection. Resource references live only in the
//! result. Native placement bytes must be queried from the backend, not
//! inferred from texel dimensions. Closing backings are a subset of the unique
//! resources.
struct ShadowSharingDiagnostics {
  struct Backing {
    std::shared_ptr<const graphics::Texture> texture;
    std::uint64_t spare_texel_bytes { 0 };
    bool closing { false };
  };
  std::vector<Backing> backings;
  std::uint64_t aliases { 0 };
  std::uint64_t live_versions { 0 };
  std::uint64_t canonical_records { 0 };
  std::uint64_t canonical_record_bytes { 0 };
  std::uint64_t version_payload_bytes { 0 };
  std::uint64_t prepared_request_bytes { 0 };
  // Cumulative acquisition decisions; failed producers may follow a miss.
  std::uint64_t cache_hits { 0 };
  std::uint64_t cache_misses { 0 };
  std::uint64_t in_place_updates { 0 };
  std::uint64_t first_allocations { 0 };
  std::uint64_t copy_on_write_shape { 0 };
  std::uint64_t copy_on_write_family { 0 };
  std::uint64_t copy_on_write_reader { 0 };
};
} // namespace oxygen::vortex
