//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "wrapper.h"

#include <cassert>
#include <stdexcept>

#include "oxygen/platform/window.h"

namespace {
template <typename... Flags>
auto CheckMutuallyExclusiveFlags(std::convertible_to<bool> auto... flags)
    -> bool {
  return ((0 + ... + flags) <= 1);
}

void TranslateFlagsToProperties(
    SDL_PropertiesID props,
    const oxygen::platform::Window::InitialFlags &flags) {
  // Check for mutually exclusive flags
  assert(CheckMutuallyExclusiveFlags(
             flags.full_screen, flags.maximized, flags.minimized)
         && "some flags are mutually exclusive");
  assert(CheckMutuallyExclusiveFlags(flags.resizable, flags.borderless)
         && "some flags are mutually exclusive");
  assert(CheckMutuallyExclusiveFlags(flags.full_screen, flags.borderless)
         && "some flags are mutually exclusive");

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
}  // namespace

void oxygen::platform::sdl::detail::SdlCheck(const bool status) {
  // zero indicates success
  if (status) {
    return;
  }

  const auto *error_message = SDL_GetError();
  throw std::runtime_error(error_message);
}

auto oxygen::platform::sdl::detail::Wrapper::MakeWindow(
    const char *title,
    const int pos_x,
    const int pos_y,
    const int width,
    const int height,
    const Window::InitialFlags &flags) const -> SDL_Window * {
  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
  SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, pos_x);
  SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, pos_y);
  SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
  SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
  TranslateFlagsToProperties(props, flags);
  auto *sdl_window = SDL_CreateWindowWithProperties(props);
  SDL_DestroyProperties(props);
  SdlCheck(sdl_window != nullptr);
  return sdl_window;
}
