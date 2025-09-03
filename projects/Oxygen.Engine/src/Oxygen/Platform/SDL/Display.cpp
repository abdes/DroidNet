//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Platform/Display.h>
#include <Oxygen/Platform/SDL/Wrapper.h>

using oxygen::platform::Display;

Display::Display(const IdType display_id)
  : display_id_(display_id)
{
  DCHECK_NE_F(display_id, kInvalidDisplayId);
}

Display::~Display() = default;

auto Display::IsPrimaryDisplay() const -> bool
{
  const IdType primary_display = sdl::GetPrimaryDisplay();
  return primary_display == Id();
}

auto Display::Name() const -> std::string { return sdl::GetDisplayName(Id()); }

auto Display::Bounds() const -> PixelBounds
{
  SDL_Rect bounds;
  sdl::GetDisplayBounds(Id(), &bounds);
  return { { .x = bounds.x, .y = bounds.y },
    { .width = bounds.w, .height = bounds.h } };
}

auto Display::UsableBounds() const -> PixelBounds
{
  SDL_Rect bounds;
  sdl::GetDisplayUsableBounds(Id(), &bounds);
  return { { .x = bounds.x, .y = bounds.y },
    { .width = bounds.w, .height = bounds.h } };
}

auto Display::Orientation() const -> DisplayOrientation
{
  switch (sdl::GetDisplayOrientation(Id())) {
  case SDL_ORIENTATION_UNKNOWN:
    return DisplayOrientation::kUnknown;
  case SDL_ORIENTATION_LANDSCAPE:
    return DisplayOrientation::kLandscape;
  case SDL_ORIENTATION_LANDSCAPE_FLIPPED:
    return DisplayOrientation::kLandscapeFlipped;
  case SDL_ORIENTATION_PORTRAIT:
    return DisplayOrientation::kPortrait;
  case SDL_ORIENTATION_PORTRAIT_FLIPPED:
    return DisplayOrientation::kPortraitFlipped;
  default: // NOLINT(clang-diagnostic-covered-switch-default)
    return DisplayOrientation::kUnknown;
  }
}

auto Display::ContentScale() const -> float
{
  return sdl::GetDisplayContentScale(Id());
}

auto oxygen::platform::to_string(const Display& self) -> std::string
{
  return fmt::format("Display [{}] {}, Bounds({}), UsableBounds({}), "
                     "Orientation({}), ContentScale({})",
    self.Id(), self.Name(), nostd::to_string(self.Bounds()),
    nostd::to_string(self.UsableBounds()), nostd::to_string(self.Orientation()),
    self.ContentScale());
}

auto oxygen::platform::to_string(const DisplayOrientation& orientation)
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
