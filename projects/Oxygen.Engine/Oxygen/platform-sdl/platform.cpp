//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "platform.h"

#include <memory>
#include <vector>

#include "detail/platform_impl.h"
#include "ImGui/ImGuiSdl3Backend.h"

namespace oxygen::platform::sdl {

  Platform::Platform(std::shared_ptr<detail::WrapperInterface> sdl_wrapper)
    : impl_(std::make_unique<PlatformImpl>(this, std::move(sdl_wrapper))) {
  }

  Platform::~Platform() = default;

  auto Platform::GetRequiredInstanceExtensions() const
    -> std::vector<const char*> {
    return impl_->GetRequiredInstanceExtensions();
  }

  auto Platform::MakeWindow(std::string const& title, PixelExtent const& extent)
    -> std::weak_ptr<platform::Window> {
    return impl_->MakeWindow(title, extent);
  }

  auto Platform::MakeWindow(std::string const& title,
                            PixelExtent const& extent,
                            platform::Window::InitialFlags flags)
    -> std::weak_ptr<platform::Window> {
    return impl_->MakeWindow(title, extent, flags);
  }

  auto Platform::MakeWindow(std::string const& title,
                            PixelPosition const& position,
                            PixelExtent const& extent)
    -> std::weak_ptr<platform::Window> {
    return impl_->MakeWindow(title, position, extent);
  }

  auto Platform::MakeWindow(std::string const& title,
                            PixelPosition const& position,
                            PixelExtent const& extent,
                            platform::Window::InitialFlags flags)
    -> std::weak_ptr<platform::Window> {
    return impl_->MakeWindow(title, position, extent, flags);
  }

  auto Platform::Displays() const
    -> std::vector<std::unique_ptr<platform::Display>> {
    return impl_->Displays();
  }

  auto Platform::DisplayFromId(const platform::Display::IdType& display_id) const
    -> std::unique_ptr<platform::Display> {
    return impl_->DisplayFromId(display_id);
  }

  auto Platform::PollEvent() -> std::unique_ptr<platform::InputEvent> {
    return impl_->PollEvent();
  }

  auto Platform::CreateImGuiBackend(WindowIdType window_id) const
    ->std::unique_ptr<imgui::ImGuiPlatformBackend>
  {
    return std::make_unique<imgui::sdl3::ImGuiSdl3Backend>(shared_from_this(), window_id);
  }

  auto Platform::OnPlatformEvent() const -> sigslot::signal<const SDL_Event&, bool&, bool&>&
  {
    return impl_->OnPlatformEvent();
  }

  auto Platform::OnUnhandledEvent() const -> sigslot::signal<const SDL_Event&>& {
    return impl_->OnUnhandledEvent();
  }

}  // namespace oxygen::platform::sdl
