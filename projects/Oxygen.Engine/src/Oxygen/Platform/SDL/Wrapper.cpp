//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>

#include <SDL3/SDL.h>

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
  // zero indicates success
  if (status) {
    return;
  }

  const auto* error_message = SDL_GetError();
  throw std::runtime_error(error_message);
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
