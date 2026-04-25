//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>

namespace oxygen::vortex {

inline constexpr std::uint32_t kEnvironmentEvaluationFlagAmbientBridgeEligible
  = 1U << 0U;

struct EnvironmentEvaluationParameters {
  float ambient_intensity { 1.0F };
  float average_brightness { 1.0F };
  float blend_fraction { 0.0F };
  std::uint32_t flags { 0U };
};

static_assert(sizeof(EnvironmentEvaluationParameters) == 16);
static_assert(alignof(EnvironmentEvaluationParameters) == 4);
static_assert(offsetof(EnvironmentEvaluationParameters, ambient_intensity) == 0);
static_assert(offsetof(EnvironmentEvaluationParameters, average_brightness) == 4);
static_assert(offsetof(EnvironmentEvaluationParameters, blend_fraction) == 8);
static_assert(offsetof(EnvironmentEvaluationParameters, flags) == 12);

} // namespace oxygen::vortex
