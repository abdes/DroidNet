//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>

#include <SDL3/SDL.h>
#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Platform/SDL/Wrapper.h>
#include <Oxygen/Platform/Window.h>

using oxygen::platform::Window;

namespace {

template <typename... Flags>
auto CheckMutuallyExclusiveFlags(std::convertible_to<bool> auto... flags)
{
  return ((0 + ... + flags) <= 1);
}

auto TranslateFlagsToProperties(const SDL_PropertiesID props,
  const oxygen::platform::window::InitialFlags& flags) -> void
{
  // Check for mutually exclusive flags
  DCHECK_F(CheckMutuallyExclusiveFlags(
             flags.full_screen, flags.maximized, flags.minimized),
    "some flags are mutually exclusive");
  DCHECK_F(CheckMutuallyExclusiveFlags(flags.resizable, flags.borderless),
    "some flags are mutually exclusive");
  DCHECK_F(CheckMutuallyExclusiveFlags(flags.full_screen, flags.borderless),
    "some flags are mutually exclusive");

  // Set always-on flags
  // TODO: Vulkan support in SDL should be made configurable
  // SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);

  // Translate provided flags into SDL flags
  SDL_SetBooleanProperty(
    props, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, flags.hidden);
  SDL_SetBooleanProperty(
    props, SDL_PROP_WINDOW_CREATE_ALWAYS_ON_TOP_BOOLEAN, flags.always_on_top);
  SDL_SetBooleanProperty(
    props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, flags.full_screen);
  SDL_SetBooleanProperty(
    props, SDL_PROP_WINDOW_CREATE_MAXIMIZED_BOOLEAN, flags.maximized);
  SDL_SetBooleanProperty(
    props, SDL_PROP_WINDOW_CREATE_MINIMIZED_BOOLEAN, flags.minimized);
  SDL_SetBooleanProperty(
    props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, flags.resizable);
  SDL_SetBooleanProperty(
    props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, flags.borderless);
}

} // namespace

auto oxygen::platform::sdl::SdlCheck(const bool status) -> void
{
  // 'status' is true on success, false on failure. Throw on failure.
  if (status) {
    return;
  }

  const auto* error_message = SDL_GetError();
  throw std::runtime_error(error_message ? error_message : "SDL error");
}

auto oxygen::platform::sdl::MakeWindow(const char* title, const uint32_t pos_x,
  const uint32_t pos_y, const uint32_t width, const uint32_t height,
  const window::InitialFlags& flags) -> SDL_Window*
{
  const SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
  SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, pos_x);
  SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, pos_y);
  SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
  SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
  SDL_SetBooleanProperty(
    props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
  TranslateFlagsToProperties(props, flags);
  auto* sdl_window = SDL_CreateWindowWithProperties(props);
  SDL_DestroyProperties(props);
  SdlCheck(sdl_window != nullptr);
  return sdl_window;
}

auto oxygen::platform::sdl::SetWindowFramebufferExtent(SDL_Window* window,
  const window::ExtentT& extent, const bool fullscreen) -> void
{
  constexpr auto maximum
    = static_cast<uint32_t>(std::numeric_limits<int>::max());
  if (extent.width == 0U || extent.height == 0U
    || extent.width > maximum || extent.height > maximum) {
    throw std::invalid_argument(
      "Framebuffer dimensions must be positive SDL pixel sizes");
  }
  if (fullscreen) {
    const auto display = SDL_GetDisplayForWindow(window);
    SdlCheck(display != 0U);
    int count = 0;
    auto modes = std::unique_ptr<SDL_DisplayMode*, decltype(&SDL_free)>(
      SDL_GetFullscreenDisplayModes(display, &count), SDL_free);
    SdlCheck(modes != nullptr);
    const SDL_DisplayMode* selected = nullptr;
    for (const auto* mode :
      std::span(modes.get(), static_cast<std::size_t>(count))) {
      const auto width
        = std::llround(static_cast<double>(mode->w) * mode->pixel_density);
      const auto height
        = std::llround(static_cast<double>(mode->h) * mode->pixel_density);
      if (width == extent.width && height == extent.height
        && (selected == nullptr || mode->refresh_rate > selected->refresh_rate)) {
        selected = mode;
      }
    }
    if (selected == nullptr) {
      throw std::runtime_error(fmt::format(
        "Fullscreen resolution {}x{} pixels is unavailable on display {}",
        extent.width, extent.height, display));
    }
    SdlCheck(SDL_SetWindowFullscreenMode(window, selected));
    SdlCheck(SDL_SetWindowFullscreen(window, true));
  } else {
    int width = 0, height = 0, pixels_w = 0, pixels_h = 0;
    SdlCheck(SDL_GetWindowSize(window, &width, &height));
    SdlCheck(SDL_GetWindowSizeInPixels(window, &pixels_w, &pixels_h));
    if (pixels_w <= 0 || pixels_h <= 0 || width <= 0 || height <= 0) {
      throw std::runtime_error("Window has no usable framebuffer extent");
    }
    const auto logical_w
      = std::llround(static_cast<double>(extent.width) * width / pixels_w);
    const auto logical_h
      = std::llround(static_cast<double>(extent.height) * height / pixels_h);
    if (logical_w <= 0 || logical_h <= 0 || logical_w > maximum
      || logical_h > maximum) {
      throw std::invalid_argument(
        "Framebuffer resolution cannot be represented at this DPI");
    }
    SdlCheck(SDL_SetWindowSize(
      window, static_cast<int>(logical_w), static_cast<int>(logical_h)));
  }
  SdlCheck(SDL_SyncWindow(window));
  int actual_w = 0, actual_h = 0;
  SdlCheck(SDL_GetWindowSizeInPixels(window, &actual_w, &actual_h));
  const bool actual_fullscreen
    = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0U;
  if (actual_w != static_cast<int>(extent.width)
    || actual_h != static_cast<int>(extent.height)
    || actual_fullscreen != fullscreen) {
    throw std::runtime_error(fmt::format(
      "Requested {}x{} pixels (fullscreen={}), got {}x{} (fullscreen={})",
      extent.width, extent.height, fullscreen, actual_w, actual_h,
      actual_fullscreen));
  }
  LOG_F(INFO, "Framebuffer resolution verified: {}x{} pixels, fullscreen={}",
    actual_w, actual_h, fullscreen);
}
