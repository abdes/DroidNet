//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::vortex {

struct OcclusionConfig {
  bool enabled { false };
  std::uint32_t max_candidate_count { 256U * 256U };
  float tiny_object_radius_threshold { 0.0F };
};

} // namespace oxygen::vortex
