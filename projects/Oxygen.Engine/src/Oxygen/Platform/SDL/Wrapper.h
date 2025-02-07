//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include "Oxygen/Platform/Window.h"
#include <SDL3/SDL.h>

namespace oxygen::platform::sdl {

void SdlCheck(bool status);
auto SdlEventName(uint32_t event_type) -> const char*;

// -- Initialization/Shutdown ------------------------------------------------

inline void Init(uint32_t subsystems)
{
    SdlCheck(SDL_Init(subsystems));
}

inline void Terminate() noexcept
{
    SDL_Quit();
}

inline void SetHint(const char* name, const char* value)
{
    SdlCheck(SDL_SetHint(name, value));
}

// -- Window management ------------------------------------------------------

auto MakeWindow(const char* title,
    int64_t pos_x,
    int64_t pos_y,
    int64_t width,
    int64_t height,
    const Window::InitialFlags& flags) -> SDL_Window*;

inline void DestroyWindow(SDL_Window* window) noexcept
{
    SDL_DestroyWindow(window);
}

inline auto GetWindowId(SDL_Window* window) -> WindowIdType
{
    return SDL_GetWindowID(window);
}

inline auto GetNativeWindow(SDL_Window* window) -> NativeWindowInfo
{
    NativeWindowInfo native_window {};

#if defined(SDL_PLATFORM_WIN32)
    void* native_window_handle = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER,
        nullptr);
    native_window.window_handle = native_window_handle;
#elif defined(SDL_PLATFORM_MACOS)
    void* nswindow = SDL_GetPointerProperty(
        SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,
        NULL);
    native_window.window_handle = nswindow;
#elif defined(SDL_PLATFORM_LINUX)
    if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0) {
        Display* xdisplay = (Display*)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
            SDL_PROP_WINDOW_X11_DISPLAY_POINTER,
            NULL);
        Window* xwindow = (Window*)SDL_GetNumberProperty(SDL_GetWindowProperties(window),
            SDL_PROP_WINDOW_X11_WINDOW_NUMBER,
            0);
        native_window.window_handle = xwindow;
        native_window.extra_handle = xdisplay;
    } else if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
        struct wl_display* display = (struct wl_display*)SDL_GetPointerProperty(
            SDL_GetWindowProperties(window),
            SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER,
            NULL);
        struct wl_surface* surface = (struct wl_surface*)SDL_GetPointerProperty(
            SDL_GetWindowProperties(window),
            SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER,
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

inline void SetWindowFullScreen(SDL_Window* window, bool full_screen)
{
    SdlCheck(SDL_SetWindowFullscreen(window, full_screen));
}

inline void ShowWindow(SDL_Window* window)
{
    SdlCheck(SDL_ShowWindow(window));
}

inline void HideWindow(SDL_Window* window)
{
    SdlCheck(SDL_HideWindow(window));
}

inline void SetWindowAlwaysOnTop(SDL_Window* window, bool always_on_top)
{
    SDL_SetWindowAlwaysOnTop(window, always_on_top);
}

inline void MaximizeWindow(SDL_Window* window)
{
    SdlCheck(SDL_MaximizeWindow(window));
}

inline void MinimizeWindow(SDL_Window* window)
{
    SdlCheck(SDL_MinimizeWindow(window));
}

inline void RestoreWindow(SDL_Window* window)
{
    SdlCheck(SDL_RestoreWindow(window));
}

inline void SetWindowSize(SDL_Window* window, int width, int height)
{
    SdlCheck(SDL_SetWindowSize(window, width, height));
}

inline void GetWindowSize(SDL_Window* window, int* width, int* height)
{
    SdlCheck(SDL_GetWindowSize(window, width, height));
}

inline void GetWindowSizeInPixels(SDL_Window* window, int* width, int* height)
{
    SdlCheck(SDL_GetWindowSizeInPixels(window, width, height));
}

inline void SetWindowMinimumSize(SDL_Window* window, int width, int height)
{
    SdlCheck(SDL_SetWindowMinimumSize(window, width, height));
}

inline void SetWindowMaximumSize(SDL_Window* window, int width, int height)
{
    SdlCheck(SDL_SetWindowMaximumSize(window, width, height));
}

inline void SetWindowResizable(SDL_Window* window, bool resizable)
{
    SdlCheck(SDL_SetWindowResizable(window, resizable));
}

inline void SetWindowPosition(SDL_Window* window, int pos_x, int pos_y)
{
    SdlCheck(SDL_SetWindowPosition(window, pos_x, pos_y));
}

inline void GetWindowPosition(SDL_Window* window, int* pos_x, int* pos_y)
{
    SdlCheck(SDL_GetWindowPosition(window, pos_x, pos_y));
}

inline void SetWindowTitle(SDL_Window* window, const std::string& title)
{
    SdlCheck(SDL_SetWindowTitle(window, title.c_str()));
}

inline auto GetWindowTitle(SDL_Window* window) -> const char*
{
    return SDL_GetWindowTitle(window);
}

inline void RaiseWindow(SDL_Window* window)
{
    SdlCheck(SDL_RaiseWindow(window));
}

// -- Memory Management ------------------------------------------------------

inline void Free(void* ptr) { SDL_free(ptr); }

// ---------------------------------------------------------------------------

inline auto PollEvent(SDL_Event* event) -> bool
{
    return SDL_PollEvent(event);
}

inline void PushEvent(SDL_Event* event)
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

[[nodiscard]] inline auto GetDisplayName(SDL_DisplayID display_id) -> const char*
{
    const auto* display_name = SDL_GetDisplayName(display_id);
    SdlCheck(display_name != nullptr);
    return display_name;
}

inline void GetDisplayBounds(SDL_DisplayID display_id, SDL_Rect* rect)
{
    SdlCheck(SDL_GetDisplayBounds(display_id, rect));
}

inline void GetDisplayUsableBounds(SDL_DisplayID display_id, SDL_Rect* rect)
{
    SdlCheck(SDL_GetDisplayUsableBounds(display_id, rect));
}

[[nodiscard]] inline auto GetDisplayOrientation(SDL_DisplayID display_id) -> SDL_DisplayOrientation
{
    return SDL_GetCurrentDisplayOrientation(display_id);
}

[[nodiscard]] inline auto GetDisplayContentScale(SDL_DisplayID display_id) -> float
{
    const auto value = SDL_GetDisplayContentScale(display_id);
    SdlCheck(value > 0.0F);
    return value;
}

#if defined(OXYGEN_VULKAN)
[[nodiscard]] inline auto GetRequiredVulkanExtensions() -> std::vector<const char*>
{
    uint32_t count { 0 };
    SdlCheck(SDL_Vulkan_GetInstanceExtensions(&count) != nullptr);
    std::vector<const char*> extensions(count);
    SdlCheck(SDL_Vulkan_GetInstanceExtensions(&count) != nullptr);
    return extensions;
}
#endif // OXYGEN_VULKAN

[[nodiscard]] inline auto GetKeyName(SDL_Keycode key) -> std::string_view
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
