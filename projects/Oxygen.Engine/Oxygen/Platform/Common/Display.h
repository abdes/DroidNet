//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Types/Geometry.h>

#include <cstdint>
#include <string>

#include "Oxygen/api_export.h"

namespace oxygen::platform {

enum class DisplayOrientation : uint8_t {
  kUnknown, /// The display orientation can't be determined
  kLandscape,
  kLandscapeFlipped,
  kPortrait,
  kPortraitFlipped
};

class Display
{
 public:
  using IdType = std::uint32_t;
  constexpr static IdType kInvalidDisplayId = 0;

  OXYGEN_API explicit Display(IdType display_id);
  OXYGEN_API virtual ~Display();

  OXYGEN_MAKE_NON_COPYABLE(Display);
  OXYGEN_MAKE_NON_MOVEABLE(Display);

  [[nodiscard]] auto Id() const { return display_id_; }

  [[nodiscard]] virtual auto IsPrimaryDisplay() const -> bool = 0;
  [[nodiscard]] virtual auto Name() const -> std::string = 0;
  [[nodiscard]] virtual auto Bounds() const -> PixelBounds = 0;
  [[nodiscard]] virtual auto UsableBounds() const -> PixelBounds = 0;
  [[nodiscard]] virtual auto Orientation() const -> DisplayOrientation = 0;
  [[nodiscard]] virtual auto ContentScale() const -> float = 0;

  friend auto to_string(Display const& self) -> std::string;

 private:
  IdType display_id_ { kInvalidDisplayId };
};

auto to_string(DisplayOrientation const& orientation) -> std::string;
auto to_string(Display const& self) -> std::string;

} // namespace oxygen::platform
