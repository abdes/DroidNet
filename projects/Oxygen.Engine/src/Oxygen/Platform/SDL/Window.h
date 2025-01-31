//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Types/Geometry.h"
#include "Oxygen/Platform/Common/Types.h"
#include "Oxygen/Platform/Common/Window.h"

#include "Oxygen/Platform/SDL/api_export.h"

struct SDL_Window;
struct SDL_Renderer;

namespace oxygen::platform::sdl {

namespace detail {
    class WrapperInterface;
} // namespace detail

class Window final : public oxygen::platform::Window {
    using Base = oxygen::platform::Window;

public:
    OXYGEN_SDL3_API Window(std::string const& title, PixelExtent const& extent);
    OXYGEN_SDL3_API Window(std::string const& title,
        PixelPosition const& position,
        PixelExtent const& extent);
    OXYGEN_SDL3_API Window(std::string const& title,
        PixelExtent const& extent,
        InitialFlags const& flags);
    OXYGEN_SDL3_API Window(std::string const&,
        PixelPosition const&,
        PixelExtent const&,
        InitialFlags const&);

    OXYGEN_SDL3_API ~Window() override;

    OXYGEN_MAKE_NON_COPYABLE(Window);
    OXYGEN_MAKE_NON_MOVEABLE(Window);

    [[nodiscard]] auto Id() const -> WindowIdType override;
    [[nodiscard]] auto NativeWindow() const -> NativeWindowInfo override;
    [[nodiscard]] auto IsValid() const -> bool override { return Id() != kInvalidWindowId; }

    // Visibility
    auto Show() -> void override;
    auto Hide() -> void override;

    // Size
    auto FullScreen(bool full_screen) -> void override;
    [[nodiscard]] auto IsFullScreen() const -> bool override;
    [[nodiscard]] auto IsMaximized() const -> bool override;
    auto Minimize() -> void override;
    [[nodiscard]] auto IsMinimized() const -> bool override;
    [[nodiscard]] auto Size() const -> PixelExtent override;
    auto MinimumSize(PixelExtent const& extent) -> void override;
    auto MaximumSize(PixelExtent const& extent) -> void override;
    auto Resizable(bool resizable) -> void override;
    [[nodiscard]] auto IsResizable() const -> bool override;
    [[nodiscard]] auto IsBorderLess() const -> bool override;

    // Position
    [[nodiscard]] auto Position() const -> PixelPosition override;

    // Decorations
    auto Title(std::string const& title) -> void override;
    [[nodiscard]] auto Title() const -> std::string override;

    // Input Focus
    auto Activate() -> void override;
    auto AlwaysOnTop(bool always_on_top) -> void override;

    // TODO: This is only temporary for the input-system example
    OXYGEN_SDL3_API [[nodiscard]] auto CreateRenderer() const -> SDL_Renderer*;

protected:
    // Size
    auto DoMaximize() -> void override;
    auto DoRestore() -> void override;
    auto DoResize(PixelExtent const& extent) -> void override;
    // Position
    auto DoPosition(PixelPosition const& position) -> void override;

    auto ProcessCloseRequest(bool force) -> void override;

    [[nodiscard]] auto GetFrameBufferSize() const -> PixelExtent override;

private:
    SDL_Window* sdl_window_ { nullptr };
};

} // namespace oxygen::platform::sdl
