//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Oxygen/Scene/ExposureSettings.h>

namespace oxygen::vortex {

//! Compiled target function: 64 authored knots, two clamps, two window edges.
struct alignas(16) ExposureTargetData {
  static constexpr std::size_t kMaxKeys = 68U;
  static constexpr std::uint32_t kLocked = 1U;
  static constexpr std::uint32_t kZeroTarget = 2U;

  std::uint32_t key_count { 0U };
  std::uint32_t flags { 0U };
  float initial_log_gain { 0.0F };
  float dark_log_gain { 0.0F };
  std::array<scene::ExposureLogTargetKey, kMaxKeys> keys {};
};

static_assert(sizeof(scene::ExposureLogTargetKey) == 8U);
static_assert(offsetof(scene::ExposureLogTargetKey, log_gain) == 4U);
static_assert(sizeof(ExposureTargetData) == 560U);
static_assert(offsetof(ExposureTargetData, key_count) == 0U);
static_assert(offsetof(ExposureTargetData, flags) == 4U);
static_assert(offsetof(ExposureTargetData, initial_log_gain) == 8U);
static_assert(offsetof(ExposureTargetData, dark_log_gain) == 12U);
static_assert(offsetof(ExposureTargetData, keys) == 16U);

} // namespace oxygen::vortex
