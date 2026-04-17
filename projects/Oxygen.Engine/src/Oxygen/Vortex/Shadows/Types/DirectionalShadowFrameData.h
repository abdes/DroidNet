//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <glm/vec2.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Vortex/Types/ShadowFrameBindings.h>

namespace oxygen::vortex {

inline constexpr std::uint32_t kDirectionalShadowStorageDedicatedArray
  = 1U << 0U;

struct alignas(packing::kShaderDataFieldAlignment) DirectionalShadowFrameData {
  ShadowFrameBindings bindings {};
  glm::uvec2 backing_resolution { 0U, 0U };
  std::uint32_t storage_flags { 0U };
  std::uint32_t reserved0 { 0U };
};

static_assert(
  alignof(DirectionalShadowFrameData) == packing::kShaderDataFieldAlignment);
static_assert(
  sizeof(DirectionalShadowFrameData) % packing::kShaderDataFieldAlignment
  == 0U);

} // namespace oxygen::vortex
