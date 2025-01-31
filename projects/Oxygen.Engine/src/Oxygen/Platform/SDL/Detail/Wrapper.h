//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include "Oxygen/Platform/Common/window.h"
#include "Oxygen/Platform/SDL/window.h"
#include "SDL3/SDL.h"

namespace oxygen::platform::sdl::detail {
// NOTE: Design for testability.
//
// Having the ability to mock the SDL library functions is important for unit
// testing, but that can be done only for virtual class methods. We use a
// wrapper around SDL with an abstract interface having only virtual methods,
// and we implement it in a concrete wrapper class that forwards the calls to
// the SDL functions. We only use the wrapper and never call SDL directly so
// that we can easily mock any calls to SDL.
//
// NOTE2:
//
// This wrapper is an implementation detail for the SDL3 platform, but is still
// exported as a header that can be used outside the platform implementation.
// A typical use case is obviously mocking SDL3 for unit testing, but another
// scenario is implementing some additional features from SDL3 that are not
// provided by the platform implementation.

class WrapperInterface {
protected:
    ~WrapperInterface() = default;

public:
    WrapperInterface() = default;

    // Non-copyable
    WrapperInterface(const WrapperInterface&) = delete;
    auto operator=(const WrapperInterface&) -> WrapperInterface& = delete;

    // Non-Movable
    WrapperInterface(WrapperInterface&& other) noexcept = delete;
    auto operator=(WrapperInterface&& other) noexcept
        -> WrapperInterface& = delete;

    // -- Initialization/Shutdown ------------------------------------------------

    virtual void Init(uint32_t subsystems) const = 0;
    virtual void Terminate() const noexcept = 0;
    virtual void SetHint(const char* name, const char* value) const = 0;

    // -- Window management
    // ------------------------------------------------------

    virtual auto MakeWindow(const char* title,
        int pos_x,
        int pos_y,
        int width,
        int height,
        const Window::InitialFlags& flags) const
        -> SDL_Window* = 0;
    virtual void DestroyWindow(SDL_Window* window) const noexcept = 0;

    virtual auto GetWindowFlags(SDL_Window* window) const -> uint64_t = 0;
    virtual auto GetWindowId(SDL_Window* window) const
        -> platform::WindowIdType
        = 0;
    virtual auto GetNativeWindow(SDL_Window* window) const
        -> NativeWindowInfo
        = 0;
    virtual void SetWindowFullScreen(SDL_Window* window,
        bool full_screen) const
        = 0;
    virtual void ShowWindow(SDL_Window* window) const = 0;
    virtual void HideWindow(SDL_Window* window) const = 0;
    virtual void SetWindowAlwaysOnTop(SDL_Window* window,
        bool always_on_top) const
        = 0;
    virtual void MaximizeWindow(SDL_Window* window) const = 0;
    virtual void MinimizeWindow(SDL_Window* window) const = 0;
    virtual void RestoreWindow(SDL_Window* window) const = 0;
    virtual void SetWindowSize(SDL_Window* window,
        int width,
        int height) const
        = 0;
    virtual void GetWindowSize(SDL_Window* window,
        int* width,
        int* height) const
        = 0;
    virtual void GetWindowSizeInPixels(SDL_Window* window,
        int* width,
        int* height) const
        = 0;
    virtual void SetWindowMinimumSize(SDL_Window* window,
        int width,
        int height) const
        = 0;
    virtual void SetWindowMaximumSize(SDL_Window* window,
        int width,
        int height) const
        = 0;
    virtual void SetWindowResizable(SDL_Window* window, bool resizable) const = 0;

    virtual void SetWindowPosition(SDL_Window* window,
        int pos_x,
        int pos_y) const
        = 0;
    virtual void GetWindowPosition(SDL_Window* window,
        int* pos_x,
        int* pos_y) const
        = 0;

    virtual void SetWindowTitle(SDL_Window* window,
        const std::string& title) const
        = 0;
    virtual auto GetWindowTitle(SDL_Window* window) const -> const char* = 0;

    virtual void RaiseWindow(SDL_Window* window) const = 0;

    // -- Memory Management
    // ------------------------------------------------------

    virtual void Free(void* ptr) const = 0;

    // ---------------------------------------------------------------------------

    virtual auto PollEvent(SDL_Event* event) const -> bool = 0;
    virtual void PushEvent(SDL_Event* event) const = 0;

    // -- Display management
    // -----------------------------------------------------

    [[nodiscard]] virtual auto GetDisplays(int* count) const
        -> SDL_DisplayID* = 0;
    [[nodiscard]] virtual auto GetPrimaryDisplay() const -> SDL_DisplayID = 0;
    [[nodiscard]] virtual auto GetDisplayName(SDL_DisplayID display_id) const
        -> const char* = 0;
    virtual void GetDisplayBounds(SDL_DisplayID display_id,
        SDL_Rect* rect) const
        = 0;
    virtual void GetDisplayUsableBounds(SDL_DisplayID display_id,
        SDL_Rect* rect) const
        = 0;
    [[nodiscard]] virtual auto GetDisplayOrientation(
        SDL_DisplayID display_id) const -> SDL_DisplayOrientation
        = 0;
    [[nodiscard]] virtual auto GetDisplayContentScale(
        SDL_DisplayID display_id) const -> float
        = 0;

    // -- Keyboard
    // ---------------------------------------------------------------

    [[nodiscard]] virtual auto GetKeyName(SDL_Keycode key) const
        -> std::string_view
        = 0;
    [[nodiscard]] virtual auto GetActiveKeyboardModifiers() const
        -> SDL_Keymod
        = 0;

    // ---------------------------------------------------------------------------

#if defined(OXYGEN_VULKAN)
    [[nodiscard]] virtual auto GetRequiredVulkanExtensions() const
        -> std::vector<const char*>
        = 0;
#endif // OXYGEN_VULKAN
};

void SdlCheck(bool status);
auto SdlEventName(uint32_t event_type) -> const char*;

class Wrapper final : public WrapperInterface {
public:
    // -- Initialization/Shutdown ------------------------------------------------

    void Init(const uint32_t subsystems) const override
    {
        SdlCheck(SDL_Init(subsystems));
    }

    void Terminate() const noexcept override { SDL_Quit(); }

    void SetHint(const char* name, const char* value) const override
    {
        SdlCheck(SDL_SetHint(name, value));
    }

    // -- Window management
    // ------------------------------------------------------

    auto MakeWindow(const char* title,
        int pos_x,
        int pos_y,
        int width,
        int height,
        const Window::InitialFlags& flags) const
        -> SDL_Window* override;

    void DestroyWindow(SDL_Window* window) const noexcept override
    {
        SDL_DestroyWindow(window);
    }

    auto GetWindowId(SDL_Window* window) const -> WindowIdType override
    {
        return SDL_GetWindowID(window);
    }

    auto GetNativeWindow(SDL_Window* window) const -> NativeWindowInfo override
    {
        NativeWindowInfo native_window {};

#if defined(SDL_PLATFORM_WIN32)
        void* native_window_handle = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER,
            nullptr);
        native_window.window_handle = native_window_handle;
#elif defined(SDL_PLATFORM_MACOS)
        NSWindow* nswindow = (__bridge NSWindow*)SDL_GetPointerProperty(
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

    auto GetWindowFlags(SDL_Window* window) const -> uint64_t override
    {
        return SDL_GetWindowFlags(window);
    }

    void SetWindowFullScreen(SDL_Window* window,
        const bool full_screen) const override
    {
        SdlCheck(SDL_SetWindowFullscreen(window, full_screen));
    }

    void ShowWindow(SDL_Window* window) const override
    {
        SdlCheck(SDL_ShowWindow(window));
    }

    void HideWindow(SDL_Window* window) const override
    {
        SdlCheck(SDL_HideWindow(window));
    }

    void SetWindowAlwaysOnTop(SDL_Window* window,
        const bool always_on_top) const override
    {
        SDL_SetWindowAlwaysOnTop(window, always_on_top);
    }

    void MaximizeWindow(SDL_Window* window) const override
    {
        SdlCheck(SDL_MaximizeWindow(window));
    }

    void MinimizeWindow(SDL_Window* window) const override
    {
        SdlCheck(SDL_MinimizeWindow(window));
    }

    void RestoreWindow(SDL_Window* window) const override
    {
        SdlCheck(SDL_RestoreWindow(window));
    }

    void SetWindowSize(SDL_Window* window,
        const int width,
        const int height) const override
    {
        SdlCheck(SDL_SetWindowSize(window, width, height));
    }

    void GetWindowSize(SDL_Window* window,
        int* width,
        int* height) const override
    {
        SdlCheck(SDL_GetWindowSize(window, width, height));
    }

    void GetWindowSizeInPixels(SDL_Window* window,
        int* width,
        int* height) const override
    {
        SdlCheck(SDL_GetWindowSizeInPixels(window, width, height));
    }

    void SetWindowMinimumSize(SDL_Window* window,
        const int width,
        const int height) const override
    {
        SdlCheck(SDL_SetWindowMinimumSize(window, width, height));
    }

    void SetWindowMaximumSize(SDL_Window* window,
        const int width,
        const int height) const override
    {
        SdlCheck(SDL_SetWindowMaximumSize(window, width, height));
    }

    void SetWindowResizable(SDL_Window* window,
        const bool resizable) const override
    {
        SdlCheck(SDL_SetWindowResizable(window, resizable));
    }

    void SetWindowPosition(SDL_Window* window,
        const int pos_x,
        const int pos_y) const override
    {
        SdlCheck(SDL_SetWindowPosition(window, pos_x, pos_y));
    }

    void GetWindowPosition(SDL_Window* window,
        int* pos_x,
        int* pos_y) const override
    {
        SdlCheck(SDL_GetWindowPosition(window, pos_x, pos_y));
    }

    void SetWindowTitle(SDL_Window* window,
        const std::string& title) const override
    {
        SdlCheck(SDL_SetWindowTitle(window, title.c_str()));
    }

    auto GetWindowTitle(SDL_Window* window) const -> const char* override
    {
        return SDL_GetWindowTitle(window);
    }

    void RaiseWindow(SDL_Window* window) const override
    {
        SdlCheck(SDL_RaiseWindow(window));
    }

    // -- Memory Management ------------------------------------------------------

    void Free(void* ptr) const override { SDL_free(ptr); }

    // ---------------------------------------------------------------------------

    auto PollEvent(SDL_Event* event) const -> bool override
    {
        return SDL_PollEvent(event);
    }

    void PushEvent(SDL_Event* event) const override
    {
        SdlCheck(SDL_PushEvent(event));
    }

    auto GetDisplays(int* count) const -> SDL_DisplayID* override
    {
        auto* displays = SDL_GetDisplays(count);
        SdlCheck(displays != nullptr);
        return displays;
    }

    [[nodiscard]] auto GetPrimaryDisplay() const -> SDL_DisplayID override
    {
        const auto display_id = SDL_GetPrimaryDisplay();
        SdlCheck(display_id != 0);
        return display_id;
    }

    [[nodiscard]] auto GetDisplayName(const SDL_DisplayID display_id) const
        -> const char* override
    {
        const auto* display_name = SDL_GetDisplayName(display_id);
        SdlCheck(display_name != nullptr);
        return display_name;
    }

    void GetDisplayBounds(const SDL_DisplayID display_id,
        SDL_Rect* rect) const override
    {
        SdlCheck(SDL_GetDisplayBounds(display_id, rect));
    }

    void GetDisplayUsableBounds(const SDL_DisplayID display_id,
        SDL_Rect* rect) const override
    {
        SdlCheck(SDL_GetDisplayUsableBounds(display_id, rect));
    }

    [[nodiscard]] auto GetDisplayOrientation(const SDL_DisplayID display_id) const
        -> SDL_DisplayOrientation override
    {
        return SDL_GetCurrentDisplayOrientation(display_id);
    }

    [[nodiscard]] auto GetDisplayContentScale(
        const SDL_DisplayID display_id) const -> float override
    {
        const auto value = SDL_GetDisplayContentScale(display_id);
        SdlCheck(value > 0.0F);
        return value;
    }

#if defined(OXYGEN_VULKAN)
    [[nodiscard]] auto GetRequiredVulkanExtensions() const
        -> std::vector<const char*> override
    {
        uint32_t count { 0 };
        SdlCheck(SDL_Vulkan_GetInstanceExtensions(&count) != nullptr);
        std::vector<const char*> extensions(count);
        SdlCheck(SDL_Vulkan_GetInstanceExtensions(&count) != nullptr);
        return extensions;
    }
#endif // OXYGEN_VULKAN

    [[nodiscard]] auto GetKeyName(const SDL_Keycode key) const
        -> std::string_view override
    {
        const auto* name = SDL_GetKeyName(key);
        // ReSharper disable once CppDFALocalValueEscapesFunction
        // SDL returns a pointer that is never freed or to be freed.
        return { name };
    }

    [[nodiscard]] auto GetActiveKeyboardModifiers() const -> SDL_Keymod override
    {
        return SDL_GetModState();
    }

    [[nodiscard]] auto CreateRenderer(SDL_Window* sdl_window) const
    {
        return SDL_CreateRenderer(sdl_window, nullptr);
    }
};
} // namespace oxygen::platform::sdl::detail
