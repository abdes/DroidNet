//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cmath>
#include <optional>

#include <Oxygen/Core/Types/ViewPort.h>

namespace oxygen::interop::module {

  //! Where a camera preview inset sits on its host surface: a fixed fraction
  //! of the surface, in its bottom-right corner.
  /*!
   Returns none when the surface is too small for a legible inset; the inset
   then neither renders nor composes until the surface grows again.
  */
  [[nodiscard]] inline auto ResolveInsetViewport(
    const float surface_width, const float surface_height)
    -> std::optional<ViewPort> {
    constexpr float kScale = 0.3F;
    constexpr float kMargin = 12.0F;
    constexpr float kMinWidth = 96.0F;
    constexpr float kMinHeight = 64.0F;

    const float width = std::floor(surface_width * kScale);
    const float height = std::floor(surface_height * kScale);
    if (!(width >= kMinWidth && height >= kMinHeight)) {
      return std::nullopt;
    }

    return ViewPort {
      .top_left_x = surface_width - kMargin - width,
      .top_left_y = surface_height - kMargin - height,
      .width = width,
      .height = height,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
  }

} // namespace oxygen::interop::module

#pragma managed(pop)
