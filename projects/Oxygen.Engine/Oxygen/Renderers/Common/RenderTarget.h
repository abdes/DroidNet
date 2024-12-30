//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"

namespace oxygen::renderer {

  struct ViewPort
  {
    ViewPort() = default;
    ViewPort(
      const float top_left_x, const float top_left_y,
      const float width, const float height,
      const float min_depth, const float max_depth)
      : top_left_x(top_left_x)
      , top_left_y(top_left_y)
      , width(width)
      , height(height)
      , min_depth(min_depth)
      , max_depth(max_depth)
    {
    }

    float top_left_x{ 0.f };
    float top_left_y{ 0.f };
    float width{ 0.f };
    float height{ 0.f };
    float min_depth{ 0.f };
    float max_depth{ 0.f };
  };

  struct Scissors
  {
    Scissors() = default;
    Scissors(
      const int32_t left, const int32_t top,
      const int32_t right, const int32_t bottom)
      : left(left)
      , top(top)
      , right(right)
      , bottom(bottom)
    {
    }
    int32_t left{ 0 };
    int32_t top{ 0 };
    int32_t right{ 0 };
    int32_t bottom{ 0 };
  };

  class RenderTarget
  {
  public:
    RenderTarget() = default;
    virtual ~RenderTarget() = default;

    OXYGEN_DEFAULT_COPYABLE(RenderTarget);
    OXYGEN_DEFAULT_MOVABLE(RenderTarget);

    [[nodiscard]] virtual auto GetViewPort() const->ViewPort = 0;
    [[nodiscard]] virtual auto GetScissors() const->Scissors = 0;
  };

}  // namespace oxygen
