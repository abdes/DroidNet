//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "display.h"

#include "SDL3/SDL.h"
#include "detail/wrapper.h"

using oxygen::platform::sdl::Display;

namespace {

constexpr oxygen::platform::sdl::detail::Wrapper kSdl;

} // namespace

Display::Display(const IdType display_id)
    : Base(display_id)
{
}

Display::~Display() = default;

auto Display::IsPrimaryDisplay() const -> bool
{
    const Display::IdType primary_display = kSdl.GetPrimaryDisplay();
    return primary_display == Id();
}

auto Display::Name() const -> std::string
{
    return kSdl.GetDisplayName(Id());
}

auto Display::Bounds() const -> PixelBounds
{
    SDL_Rect bounds;
    kSdl.GetDisplayBounds(Id(), &bounds);
    return { { .x = bounds.x, .y = bounds.y },
        { .width = bounds.w, .height = bounds.h } };
}

auto Display::UsableBounds() const -> PixelBounds
{
    SDL_Rect bounds;
    kSdl.GetDisplayUsableBounds(Id(), &bounds);
    return { { .x = bounds.x, .y = bounds.y },
        { .width = bounds.w, .height = bounds.h } };
}

auto Display::Orientation() const -> DisplayOrientation
{
    switch (kSdl.GetDisplayOrientation(Id())) {
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
    return kSdl.GetDisplayContentScale(Id());
}
