//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>

#include <Oxygen/Core/Constants.h>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace oxygen::vortex {

struct alignas(packing::kShaderDataFieldAlignment) PointShadowBinding {
  std::array<glm::mat4, 6U> face_light_view_projection {};
  glm::vec4 position_and_inv_range { 0.0F };
  glm::vec4 sampling_metadata0 { 0.0F };
  glm::vec4 sampling_metadata1 { 0.0F };
  glm::vec4 _padding0 { 0.0F };
};

static_assert(alignof(PointShadowBinding) == packing::kShaderDataFieldAlignment);
static_assert(sizeof(PointShadowBinding) == 448U);
static_assert(offsetof(PointShadowBinding, face_light_view_projection) == 0U);
static_assert(offsetof(PointShadowBinding, position_and_inv_range) == 384U);
static_assert(offsetof(PointShadowBinding, sampling_metadata0) == 400U);
static_assert(offsetof(PointShadowBinding, sampling_metadata1) == 416U);
static_assert(sizeof(PointShadowBinding) % packing::kShaderDataFieldAlignment == 0U);

} // namespace oxygen::vortex
