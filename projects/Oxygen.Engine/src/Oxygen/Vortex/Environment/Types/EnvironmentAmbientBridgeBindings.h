//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::vortex {

inline constexpr std::uint32_t kEnvironmentAmbientBridgeFlagEnabled
  = 1U << 0U;

struct EnvironmentAmbientBridgeBindings {
  ShaderVisibleIndex irradiance_map_srv { kInvalidShaderVisibleIndex };
  float ambient_intensity { 1.0F };
  float average_brightness { 1.0F };
  float blend_fraction { 0.0F };
  std::uint32_t flags { 0U };
};

static_assert(std::is_standard_layout_v<EnvironmentAmbientBridgeBindings>);
static_assert(sizeof(EnvironmentAmbientBridgeBindings) == 20);
static_assert(alignof(EnvironmentAmbientBridgeBindings) == 4);
static_assert(offsetof(EnvironmentAmbientBridgeBindings, irradiance_map_srv) == 0);
static_assert(offsetof(EnvironmentAmbientBridgeBindings, ambient_intensity) == 4);
static_assert(offsetof(EnvironmentAmbientBridgeBindings, average_brightness) == 8);
static_assert(offsetof(EnvironmentAmbientBridgeBindings, blend_fraction) == 12);
static_assert(offsetof(EnvironmentAmbientBridgeBindings, flags) == 16);

} // namespace oxygen::vortex
