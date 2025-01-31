//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Platform/Common/platform.h"
#include "Oxygen/Platform/SDL/api_export.h"

// Forward declare SDL_Event
union SDL_Event;

namespace oxygen::platform::sdl {

namespace detail {
    class WrapperInterface;
} // namespace detail

class Platform final
    : public oxygen::Platform,
      public std::enable_shared_from_this<Platform> {
public:
    OXYGEN_SDL3_API explicit Platform(
        std::shared_ptr<detail::WrapperInterface> sdl_wrapper = nullptr);
    OXYGEN_SDL3_API ~Platform() override;

    OXYGEN_MAKE_NON_COPYABLE(Platform);
    OXYGEN_MAKE_NON_MOVEABLE(Platform);

#if defined(OXYGEN_VULKAN)
    [[nodiscard]] auto GetRequiredInstanceExtensions() const
        -> std::vector<const char*> override;
#endif // OXYGEN_VULKAN

    OXYGEN_SDL3_API auto MakeWindow(std::string const& title,
        PixelExtent const& extent)
        -> std::weak_ptr<platform::Window> override;
    OXYGEN_SDL3_API auto MakeWindow(std::string const& title,
        PixelExtent const& extent,
        Window::InitialFlags flags)
        -> std::weak_ptr<platform::Window> override;
    OXYGEN_SDL3_API auto MakeWindow(std::string const& title,
        PixelPosition const& position,
        PixelExtent const& extent)
        -> std::weak_ptr<platform::Window> override;
    OXYGEN_SDL3_API auto MakeWindow(std::string const& title,
        PixelPosition const& position,
        PixelExtent const& extent,
        Window::InitialFlags flags)
        -> std::weak_ptr<platform::Window> override;

    OXYGEN_SDL3_API [[nodiscard]] auto Displays() const
        -> std::vector<std::unique_ptr<platform::Display>> override;

    OXYGEN_SDL3_API [[nodiscard]] auto DisplayFromId(
        const Display::IdType& display_id) const
        -> std::unique_ptr<platform::Display> override;

    OXYGEN_SDL3_API auto PollEvent()
        -> std::unique_ptr<platform::InputEvent> override;

    OXYGEN_SDL3_API [[nodiscard]] auto OnUnhandledEvent() const -> sigslot::signal<const SDL_Event&>&;
    OXYGEN_SDL3_API [[nodiscard]] auto OnPlatformEvent() const -> sigslot::signal<const SDL_Event&, bool&, bool&>&;

    [[nodiscard]] auto CreateImGuiBackend(WindowIdType window_id) const
        -> std::unique_ptr<imgui::ImGuiPlatformBackend> override;

private:
    class PlatformImpl; // Forward declaration of the implementation class
    std::unique_ptr<PlatformImpl> impl_; // Pointer to the implementation
};

} // namespace oxygen::platform::sdl
