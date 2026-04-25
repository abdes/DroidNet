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

struct EnvironmentProbeBindings {
  ShaderVisibleIndex environment_map_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex irradiance_map_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex prefiltered_map_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex brdf_lut_srv { kInvalidShaderVisibleIndex };
  std::uint32_t probe_revision { 0U };
};

static_assert(std::is_standard_layout_v<EnvironmentProbeBindings>);
static_assert(sizeof(ShaderVisibleIndex) == 4);
static_assert(alignof(ShaderVisibleIndex) == 4);
static_assert(sizeof(EnvironmentProbeBindings) == 20);
static_assert(alignof(EnvironmentProbeBindings) == 4);
static_assert(offsetof(EnvironmentProbeBindings, environment_map_srv) == 0);
static_assert(offsetof(EnvironmentProbeBindings, irradiance_map_srv) == 4);
static_assert(offsetof(EnvironmentProbeBindings, prefiltered_map_srv) == 8);
static_assert(offsetof(EnvironmentProbeBindings, brdf_lut_srv) == 12);
static_assert(offsetof(EnvironmentProbeBindings, probe_revision) == 16);

} // namespace oxygen::vortex
