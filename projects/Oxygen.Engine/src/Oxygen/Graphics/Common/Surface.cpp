//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/Surface.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Types/EngineResources.h>
#include <Oxygen/Platform/Window.h>

using oxygen::graphics::Surface;
using oxygen::graphics::detail::WindowComponent;
using oxygen::graphics::detail::WindowSurface;
using oxygen::graphics::resources::kSurface;

Surface::~Surface()
{
    DLOG_F(INFO, "Surface `{}` destroyed", GetName());
}

auto WindowComponent::Width() const -> uint32_t
{
    if (const auto window = window_.lock()) {
        return window->FrameBufferSize().width;
    }
    throw std::runtime_error("Window is no longer valid");
}

auto WindowComponent::Height() const -> uint32_t
{
    if (const auto window = window_.lock()) {
        return window->FrameBufferSize().height;
    }
    throw std::runtime_error("Window is no longer valid");
}

auto WindowComponent::FrameBufferSize() const -> platform::window::ExtentT
{
    if (const auto window = window_.lock()) {
        return window_.lock()->FrameBufferSize();
    }
    throw std::runtime_error("Window is no longer valid");
}

auto WindowComponent::Native() const -> platform::window::NativeHandles
{
    if (const auto window = window_.lock()) {
        return window->Native();
    }
    throw std::runtime_error("Window is no longer valid");
}
auto WindowComponent::GetWindowTitle() const -> std::string
{
    return window_.expired() ? "" : window_.lock()->Title();
}
