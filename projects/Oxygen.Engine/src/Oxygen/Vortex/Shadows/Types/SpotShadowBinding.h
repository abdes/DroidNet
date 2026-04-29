//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>

#include <Oxygen/Core/Constants.h>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace oxygen::vortex {

struct alignas(packing::kShaderDataFieldAlignment) SpotShadowBinding {
  glm::mat4 light_view_projection { 1.0F };
  glm::vec4 position_and_inv_range { 0.0F };
  glm::vec4 direction_and_bias { 0.0F, -1.0F, 0.0F, 0.0F };
  glm::vec4 sampling_metadata0 { 0.0F };
  glm::vec4 sampling_metadata1 { 0.0F };
};

static_assert(alignof(SpotShadowBinding) == packing::kShaderDataFieldAlignment);
static_assert(sizeof(SpotShadowBinding) == 128U);
static_assert(offsetof(SpotShadowBinding, light_view_projection) == 0U);
static_assert(offsetof(SpotShadowBinding, position_and_inv_range) == 64U);
static_assert(offsetof(SpotShadowBinding, direction_and_bias) == 80U);
static_assert(offsetof(SpotShadowBinding, sampling_metadata0) == 96U);
static_assert(offsetof(SpotShadowBinding, sampling_metadata1) == 112U);
static_assert(sizeof(SpotShadowBinding) % packing::kShaderDataFieldAlignment == 0U);

} // namespace oxygen::vortex
