//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Platform/Common/Display.h"

namespace oxygen::platform::sdl {

class Display final : public platform::Display
{
  using Base = platform::Display;

 public:
  explicit Display(IdType display_id);
  ~Display() override;

  // Non-copyable
  Display(const Display&) = delete;
  auto operator=(const Display&) -> Display& = delete;

  // Non-Movable
  Display(Display&& other) noexcept = delete;
  auto operator=(Display&& other) noexcept -> Display& = delete;

  [[nodiscard]] auto IsPrimaryDisplay() const -> bool override;
  [[nodiscard]] auto Name() const -> std::string override;
  [[nodiscard]] auto Bounds() const -> PixelBounds override;
  [[nodiscard]] auto UsableBounds() const -> PixelBounds override;
  [[nodiscard]] auto Orientation() const -> DisplayOrientation override;
  [[nodiscard]] auto ContentScale() const -> float override;
};

} // namespace oxygen::platform::sdl
