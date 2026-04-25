//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Core/Constants.h>

namespace oxygen::vortex {

struct alignas(packing::kShaderDataFieldAlignment) ShadowCascadeBinding {
  glm::mat4 light_view_projection { 1.0F };
  float split_near { 0.0F };
  float split_far { 0.0F };
  glm::vec4 sampling_metadata0 { 0.0F };
  glm::vec4 sampling_metadata1 { 0.0F };
};

static_assert(
  alignof(ShadowCascadeBinding) == packing::kShaderDataFieldAlignment);
static_assert(
  sizeof(ShadowCascadeBinding) % packing::kShaderDataFieldAlignment == 0U);

} // namespace oxygen::vortex
