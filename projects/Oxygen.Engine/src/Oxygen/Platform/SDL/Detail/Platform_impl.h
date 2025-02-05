//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include "Oxygen/Base/Signals.h"
#include "Oxygen/Platform/SDL/Platform.h"

// ReSharper disable once CppInconsistentNaming (external library)
union SDL_Event;

namespace oxygen::platform::sdl {

namespace detail {
    class WrapperInterface;
} // namespace detail

class Platform::PlatformImpl {
public:
    explicit PlatformImpl(Platform* platform,
        std::shared_ptr<detail::WrapperInterface> sdl_wrapper);
    ~PlatformImpl();

#if defined(OXYGEN_VULKAN)
    [[nodiscard]] auto GetRequiredInstanceExtensions() const
        -> std::vector<const char*>;
#endif // OXYGEN_VULKAN

    auto MakeWindow(std::string const& title, PixelExtent const& extent)
        -> std::weak_ptr<Window>;
    auto MakeWindow(std::string const& title,
        PixelExtent const& extent,
        Window::InitialFlags flags)
        -> std::weak_ptr<Window>;
    auto MakeWindow(std::string const& title,
        PixelPosition const& position,
        PixelExtent const& extent)
        -> std::weak_ptr<Window>;
    auto MakeWindow(std::string const& title,
        PixelPosition const& position,
        PixelExtent const& extent,
        Window::InitialFlags flags)
        -> std::weak_ptr<Window>;

    [[nodiscard]] auto Displays() const
        -> std::vector<std::unique_ptr<Display>>;
    [[nodiscard]] auto DisplayFromId(const Display::IdType& display_id) const
        -> std::unique_ptr<Display>;

    auto PollEvent() -> std::unique_ptr<InputEvent>;

    [[nodiscard]] auto OnUnhandledEvent() const -> sigslot::signal<const SDL_Event&>&
    {
        return on_unhandled_event_;
    }

    //! Get the signal for direct handling of platform events.
    //! \return A signal that can be used to register a callback for handling
    //! platform events before the platform processes them.
    /*!
     The callback takes the following parameters:
        - The SDL event that was received.
        - A boolean that can be set to true to indicate that the handler wants
          to capture any mouse event, and the platform should not process such
          events.
        - A boolean that can be set to true to indicate that the handler wants
          to capture any keyboard event, and the platform should not process
          such events.
    */
    [[nodiscard]] auto OnPlatformEvent() const -> sigslot::signal<const SDL_Event&, bool&, bool&>&
    {
        return on_platform_event_;
    }

private:
    [[nodiscard]] auto WindowFromId(WindowIdType window_id) const -> Window&;
    void DispatchDisplayEvent(SDL_Event const& event) const;
    void DispatchWindowEvent(SDL_Event const& event);

    Platform* platform_;
    std::shared_ptr<detail::WrapperInterface> sdl_;
    std::vector<std::shared_ptr<Window>> windows_;

    mutable sigslot::signal<const SDL_Event&> on_unhandled_event_;
    mutable sigslot::signal<const SDL_Event&, bool&, bool&> on_platform_event_;
};

} // namespace oxygen::platform::sdl
