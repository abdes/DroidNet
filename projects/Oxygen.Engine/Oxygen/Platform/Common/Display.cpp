//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Platform/Common/Display.h"

#include <cassert>

#include <fmt/core.h>

using oxygen::platform::Display;

Display::Display(const Display::IdType display_id)
  : display_id_(display_id)
{
  assert(display_id != kInvalidDisplayId);
}

Display::~Display() = default;

auto oxygen::platform::to_string(Display const& self) -> std::string
{
  return fmt::format(
    "Display [{}] {}, Bounds({}), UsableBounds({}), "
    "Orientation({}), ContentScale({})",
    self.Id(),
    self.Name(),
    nostd::to_string(self.Bounds()),
    nostd::to_string(self.UsableBounds()),
    nostd::to_string(self.Orientation()),
    self.ContentScale());
}

auto oxygen::platform::to_string(DisplayOrientation const& orientation)
  -> std::string
{
  switch (orientation) {
  case DisplayOrientation::kUnknown:
    return "Unknown";
  case DisplayOrientation::kLandscape:
    return "Landscape";
  case DisplayOrientation::kLandscapeFlipped:
    return "Landscape-Flipped";
  case DisplayOrientation::kPortrait:
    return "Portrait";
  case DisplayOrientation::kPortraitFlipped:
    return "Portrait-Flipped";
  default: // NOLINT(clang-diagnostic-covered-switch-default)
    return "Unknown";
  }
}
