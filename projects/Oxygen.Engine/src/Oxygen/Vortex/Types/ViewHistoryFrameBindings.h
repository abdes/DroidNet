//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Core/Constants.h>

namespace oxygen::vortex {

enum class ViewHistoryValidityFlagBits : std::uint32_t {
  kPreviousViewValid = 1U << 0U,
};

[[nodiscard]] constexpr auto HasAnyViewHistoryValidityFlag(
  const std::uint32_t flags, const ViewHistoryValidityFlagBits bits) noexcept
  -> bool
{
  return (flags & static_cast<std::uint32_t>(bits)) != 0U;
}

struct alignas(packing::kShaderDataFieldAlignment) ViewHistoryFrameBindings {
  glm::mat4 current_view_matrix { 1.0F };
  glm::mat4 current_projection_matrix { 1.0F };
  glm::mat4 current_stable_projection_matrix { 1.0F };
  glm::mat4 current_inverse_view_projection_matrix { 1.0F };
  glm::mat4 previous_view_matrix { 1.0F };
  glm::mat4 previous_projection_matrix { 1.0F };
  glm::mat4 previous_stable_projection_matrix { 1.0F };
  glm::mat4 previous_inverse_view_projection_matrix { 1.0F };
  glm::vec2 current_pixel_jitter { 0.0F, 0.0F };
  glm::vec2 previous_pixel_jitter { 0.0F, 0.0F };
  std::uint32_t validity_flags { 0U };
  std::array<std::uint32_t, 1U> _pad_to_16 {};
};

static_assert(
  sizeof(ViewHistoryFrameBindings) % packing::kShaderDataFieldAlignment == 0);

} // namespace oxygen::vortex
