//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include <Oxygen/Platform/Window.h>
#include <SDL3/SDL.h>

// ReSharper disable CppClangTidyBugproneNarrowingConversions

namespace oxygen::platform::sdl {

auto SdlCheck(bool status) -> void;
auto SdlEventName(uint32_t event_type) -> const char*;

// -- Initialization/Shutdown ------------------------------------------------

inline auto Init(const uint32_t subsystems) -> void
{
  SdlCheck(SDL_Init(subsystems));
}

inline auto Terminate() noexcept -> void { SDL_Quit(); }

inline auto SetHint(const char* name, const char* value) -> void
{
  SdlCheck(SDL_SetHint(name, value));
}

// -- Window management ------------------------------------------------------

auto MakeWindow(const char* title, uint32_t pos_x, uint32_t pos_y,
  uint32_t width, uint32_t height, const window::InitialFlags& flags)
  -> SDL_Window*;

inline auto DestroyWindow(SDL_Window* window) noexcept -> void
{
  SDL_DestroyWindow(window);
}

inline auto GetWindowId(SDL_Window* window) -> WindowIdType
{
  return SDL_GetWindowID(window);
}

inline auto GetNativeWindow(SDL_Window* window) -> window::NativeHandles
{
  window::NativeHandles native_window {};

#if defined(SDL_PLATFORM_WIN32)
  void* native_window_handle
    = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
      SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
  native_window.window_handle = native_window_handle;
#elif defined(SDL_PLATFORM_MACOS)
  void* nswindow = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
    SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
  native_window.window_handle = nswindow;
#elif defined(SDL_PLATFORM_LINUX)
  if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0) {
    Display* xdisplay
      = (Display*)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
    Window* xwindow = (Window*)SDL_GetNumberProperty(
      SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    native_window.window_handle = xwindow;
    native_window.extra_handle = xdisplay;
  } else if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
    struct wl_display* display = (struct wl_display*)SDL_GetPointerProperty(
      SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER,
      NULL);
    struct wl_surface* surface = (struct wl_surface*)SDL_GetPointerProperty(
      SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER,
      NULL);
    native_window.window_handle = surface;
    native_window.extra_handle = display;
  }
#endif
  return native_window;
}

inline auto GetWindowFlags(SDL_Window* window) -> uint64_t
{
  return SDL_GetWindowFlags(window);
}

inline auto SetWindowFullScreen(SDL_Window* window, const bool full_screen)
  -> void
{
  SdlCheck(SDL_SetWindowFullscreen(window, full_screen));
}

inline auto ShowWindow(SDL_Window* window) -> void
{
  SdlCheck(SDL_ShowWindow(window));
}

inline auto HideWindow(SDL_Window* window) -> void
{
  SdlCheck(SDL_HideWindow(window));
}

inline auto SetWindowAlwaysOnTop(SDL_Window* window, const bool always_on_top)
  -> void
{
  SDL_SetWindowAlwaysOnTop(window, always_on_top);
}

inline auto MaximizeWindow(SDL_Window* window) -> void
{
  SdlCheck(SDL_MaximizeWindow(window));
}

inline auto MinimizeWindow(SDL_Window* window) -> void
{
  SdlCheck(SDL_MinimizeWindow(window));
}

inline auto RestoreWindow(SDL_Window* window) -> void
{
  SdlCheck(SDL_RestoreWindow(window));
}

inline auto GetWindowSize(SDL_Window* window, uint32_t* width, uint32_t* height)
  -> void
{
  SdlCheck(SDL_GetWindowSize(
    window, reinterpret_cast<int*>(width), reinterpret_cast<int*>(height)));
}

inline auto GetWindowSizeInPixels(
  SDL_Window* window, uint32_t* width, uint32_t* height) -> void
{
  SdlCheck(SDL_GetWindowSizeInPixels(
    window, reinterpret_cast<int*>(width), reinterpret_cast<int*>(height)));
}

inline auto SetWindowSize(
  SDL_Window* window, const uint32_t width, const uint32_t height) -> void
{
  SdlCheck(SDL_SetWindowSize(window, width, height));
}

inline auto SetWindowMinimumSize(
  SDL_Window* window, const uint32_t width, const uint32_t height) -> void
{
  SdlCheck(SDL_SetWindowMinimumSize(window, width, height));
}

inline auto SetWindowMaximumSize(
  SDL_Window* window, const uint32_t width, const uint32_t height) -> void
{
  SdlCheck(SDL_SetWindowMaximumSize(window, width, height));
}

inline auto SetWindowResizable(SDL_Window* window, const bool resizable) -> void
{
  SdlCheck(SDL_SetWindowResizable(window, resizable));
}

inline auto SetWindowPosition(
  SDL_Window* window, const uint32_t pos_x, const uint32_t pos_y) -> void
{
  SdlCheck(SDL_SetWindowPosition(window, pos_x, pos_y));
}

inline auto GetWindowPosition(
  SDL_Window* window, uint32_t* pos_x, uint32_t* pos_y) -> void
{
  SdlCheck(SDL_GetWindowPosition(
    window, reinterpret_cast<int*>(pos_x), reinterpret_cast<int*>(pos_y)));
}

inline auto SetWindowTitle(SDL_Window* window, const std::string& title) -> void
{
  SdlCheck(SDL_SetWindowTitle(window, title.c_str()));
}

inline auto GetWindowTitle(SDL_Window* window) -> const char*
{
  return SDL_GetWindowTitle(window);
}

inline auto RaiseWindow(SDL_Window* window) -> void
{
  SdlCheck(SDL_RaiseWindow(window));
}

// -- Memory Management ------------------------------------------------------

inline auto Free(void* ptr) -> void { SDL_free(ptr); }

// ---------------------------------------------------------------------------

inline auto PollEvent(SDL_Event* event) -> bool { return SDL_PollEvent(event); }

inline auto PushEvent(SDL_Event* event) -> void
{
  SdlCheck(SDL_PushEvent(event));
}

inline auto GetDisplays(int* count) -> SDL_DisplayID*
{
  auto* displays = SDL_GetDisplays(count);
  SdlCheck(displays != nullptr);
  return displays;
}

[[nodiscard]] inline auto GetPrimaryDisplay() -> SDL_DisplayID
{
  const auto display_id = SDL_GetPrimaryDisplay();
  SdlCheck(display_id != 0);
  return display_id;
}

[[nodiscard]] inline auto GetDisplayName(const SDL_DisplayID display_id)
  -> const char*
{
  const auto* display_name = SDL_GetDisplayName(display_id);
  SdlCheck(display_name != nullptr);
  return display_name;
}

inline auto GetDisplayBounds(const SDL_DisplayID display_id, SDL_Rect* rect)
  -> void
{
  SdlCheck(SDL_GetDisplayBounds(display_id, rect));
}

inline auto GetDisplayUsableBounds(
  const SDL_DisplayID display_id, SDL_Rect* rect) -> void
{
  SdlCheck(SDL_GetDisplayUsableBounds(display_id, rect));
}

[[nodiscard]] inline auto GetDisplayOrientation(const SDL_DisplayID display_id)
  -> SDL_DisplayOrientation
{
  return SDL_GetCurrentDisplayOrientation(display_id);
}

[[nodiscard]] inline auto GetDisplayContentScale(const SDL_DisplayID display_id)
  -> float
{
  const auto value = SDL_GetDisplayContentScale(display_id);
  SdlCheck(value > 0.0F);
  return value;
}

#if defined(OXYGEN_VULKAN)
[[nodiscard]] inline auto GetRequiredVulkanExtensions()
  -> std::vector<const char*>
{
  uint32_t count { 0 };
  SdlCheck(SDL_Vulkan_GetInstanceExtensions(&count) != nullptr);
  std::vector<const char*> extensions(count);
  SdlCheck(SDL_Vulkan_GetInstanceExtensions(&count) != nullptr);
  return extensions;
}
#endif // OXYGEN_VULKAN

[[nodiscard]] inline auto GetKeyName(const SDL_Keycode key) -> std::string_view
{
  const auto* name = SDL_GetKeyName(key);
  return { name };
}

[[nodiscard]] inline auto GetActiveKeyboardModifiers() -> SDL_Keymod
{
  return SDL_GetModState();
}

[[nodiscard]] inline auto CreateRenderer(SDL_Window* sdl_window)
{
  return SDL_CreateRenderer(sdl_window, nullptr);
}

} // namespace oxygen::platform::sdl
