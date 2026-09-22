//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Vortex/Shadows/Types/CubeLocalShadowRecord.h>
#include <Oxygen/Vortex/Shadows/Types/DirectionalShadowRecord.h>
#include <Oxygen/Vortex/Shadows/Types/ProjectedLocalShadowRecord.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowCascadeBinding.h>
#include <Oxygen/Vortex/Types/ShadowFrameBindings.h>

namespace oxygen::vortex {

inline constexpr std::uint32_t kDirectionalShadowStorageDedicatedArray = 1U;
inline constexpr std::uint32_t kMaxDirectionalCascades = 4U;

//! CPU preparation and inspection data; only the header and records are
//! uploaded.
struct ShadowFrameData {
  ShadowFrameBindings bindings {};
  std::vector<DirectionalShadowRecord> directional_records;
  std::vector<ShadowCascadeBinding> cascades;
  std::vector<ProjectedLocalShadowRecord> projected_local_records;
  std::vector<CubeLocalShadowRecord> cube_local_records;
  glm::vec4 light_direction_to_source { 0.0F };
  glm::uvec2 backing_resolution { 0U };
  std::uint32_t storage_flags { 0U };
};

} // namespace oxygen::vortex
