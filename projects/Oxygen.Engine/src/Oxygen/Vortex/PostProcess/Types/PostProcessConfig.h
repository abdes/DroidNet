//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen::vortex {

struct PostProcessConfig {
  bool enable_bloom { true };
  bool enable_auto_exposure { true };
  float fixed_exposure { 1.0F };
  float bloom_intensity { 0.5F };
  float bloom_threshold { 1.0F };
};

} // namespace oxygen::vortex
