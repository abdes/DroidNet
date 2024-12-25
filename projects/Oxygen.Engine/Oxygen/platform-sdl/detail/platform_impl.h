//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include "SDL3/SDL_events.h"
#include "Oxygen/Base/Signals.h"
#include "oxygen/platform-sdl/platform.h"

namespace oxygen::platform::sdl {

  namespace detail {
    class WrapperInterface;
  }  // namespace detail

  class Platform::PlatformImpl
  {
  public:
    explicit PlatformImpl(Platform* platform,
                          std::shared_ptr<detail::WrapperInterface> sdl_wrapper);
    ~PlatformImpl();

    [[nodiscard]] auto GetRequiredInstanceExtensions() const
      ->std::vector<const char*>;

    auto MakeWindow(std::string const& title, PixelExtent const& extent)
      -> std::weak_ptr<platform::Window>;
    auto MakeWindow(std::string const& title,
                    PixelExtent const& extent,
                    platform::Window::InitialFlags flags)
      -> std::weak_ptr<platform::Window>;
    auto MakeWindow(std::string const& title,
                    PixelPosition const& position,
                    PixelExtent const& extent) -> std::weak_ptr<platform::Window>;
    auto MakeWindow(std::string const& title,
                    PixelPosition const& position,
                    PixelExtent const& extent,
                    platform::Window::InitialFlags flags)
      -> std::weak_ptr<platform::Window>;

    [[nodiscard]] auto Displays() const
      ->std::vector<std::unique_ptr<platform::Display>>;
    [[nodiscard]] auto DisplayFromId(const Display::IdType& display_id) const
      ->std::unique_ptr<platform::Display>;

    auto PollEvent() -> std::unique_ptr<platform::InputEvent>;

    [[nodiscard]] auto OnUnhandledEvent() -> auto& {
      return on_unhandled_event_;
    }

  private:
    [[nodiscard]] auto WindowFromId(WindowIdType window_id) const
      ->platform::Window&;
    void DispatchDisplayEvent(SDL_Event const& event) const;
    void DispatchWindowEvent(SDL_Event const& event);

    SDL_Event event_{};

    Platform* platform_;
    std::shared_ptr<detail::WrapperInterface> sdl_;
    std::vector<std::shared_ptr<Window>> windows_;

    sigslot::signal<const SDL_Event&> on_unhandled_event_;
  };

}  // namespace oxygen::platform::sdl
