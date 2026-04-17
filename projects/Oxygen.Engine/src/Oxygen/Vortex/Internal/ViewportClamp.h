//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ViewPort.h>

namespace oxygen::vortex::internal {

struct ClampedViewportState {
  ViewPort viewport {};
  Scissors scissors {};
};

inline auto FullViewportForExtent(
  const std::uint32_t width, const std::uint32_t height) -> ViewPort
{
  return {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(width),
    .height = static_cast<float>(height),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
}

inline auto ScissorsFromViewport(const ViewPort& viewport) -> Scissors
{
  return {
    .left = static_cast<std::int32_t>(std::floor(viewport.top_left_x)),
    .top = static_cast<std::int32_t>(std::floor(viewport.top_left_y)),
    .right = static_cast<std::int32_t>(
      std::ceil(viewport.top_left_x + viewport.width)),
    .bottom = static_cast<std::int32_t>(
      std::ceil(viewport.top_left_y + viewport.height)),
  };
}

inline auto ClampViewportToExtent(const ViewPort& viewport,
  const std::uint32_t width, const std::uint32_t height) -> ViewPort
{
  const auto max_x = static_cast<float>(width);
  const auto max_y = static_cast<float>(height);

  auto clamped = viewport;
  clamped.top_left_x = std::clamp(clamped.top_left_x, 0.0F, max_x);
  clamped.top_left_y = std::clamp(clamped.top_left_y, 0.0F, max_y);
  clamped.width = std::clamp(clamped.width, 0.0F, max_x - clamped.top_left_x);
  clamped.height
    = std::clamp(clamped.height, 0.0F, max_y - clamped.top_left_y);
  if (!clamped.IsValid()) {
    return FullViewportForExtent(width, height);
  }
  return clamped;
}

inline auto ClampScissorsToExtent(const Scissors& scissors,
  const ViewPort& viewport, const std::uint32_t width,
  const std::uint32_t height) -> Scissors
{
  const auto max_x = static_cast<std::int32_t>(width);
  const auto max_y = static_cast<std::int32_t>(height);

  auto clamped = scissors;
  clamped.left = std::clamp(clamped.left, 0, max_x);
  clamped.top = std::clamp(clamped.top, 0, max_y);
  clamped.right = std::clamp(clamped.right, clamped.left, max_x);
  clamped.bottom = std::clamp(clamped.bottom, clamped.top, max_y);

  const auto viewport_scissors = ScissorsFromViewport(viewport);
  clamped.left = std::max(clamped.left, viewport_scissors.left);
  clamped.top = std::max(clamped.top, viewport_scissors.top);
  clamped.right = std::min(clamped.right, viewport_scissors.right);
  clamped.bottom = std::min(clamped.bottom, viewport_scissors.bottom);
  if (!clamped.IsValid()) {
    return viewport_scissors;
  }
  return clamped;
}

inline auto ResolveClampedViewportState(const ViewPort& viewport,
  const Scissors& scissors, const std::uint32_t width,
  const std::uint32_t height) -> ClampedViewportState
{
  const auto clamped_viewport = ClampViewportToExtent(viewport, width, height);
  return {
    .viewport = clamped_viewport,
    .scissors = ClampScissorsToExtent(
      scissors, clamped_viewport, width, height),
  };
}

} // namespace oxygen::vortex::internal
