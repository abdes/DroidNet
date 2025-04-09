//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/Graphics.h>

#include <type_traits>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/PerFrameResourceManager.h>
#include <Oxygen/Graphics/Common/Renderer.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Platform/Types.h>

using oxygen::Graphics;

auto Graphics::StartAsync(co::TaskStarted<> started) -> co::Co<>
{
    return OpenNursery(nursery_, std::move(started));
}

void Graphics::Run()
{
    // TODO: run the async tasks for graphics
}

auto Graphics::GetRenderer() const noexcept -> const graphics::Renderer*
{
    CHECK_F(!is_renderer_less_, "we're running renderer-less, but some code is requesting a renderer from the graphics backend");

    return renderer_.get();
}

auto Graphics::GetRenderer() noexcept -> graphics::Renderer*
{
    CHECK_F(!is_renderer_less_, "we're running renderer-less, but some code is requesting a renderer from the graphics backend");

    return renderer_.get();
}

// void Graphics::OnInitialize(const SerializedBackendConfig& props)
// {
//     InitializeGraphicsBackend(props);

//     // Create and initialize the renderer instance if we are not running renderer-less.
//     // TODO(abdes): This is a temporary solution until we have a proper way to handle
//     // if (!props.headless) {
//     //     is_renderer_less_ = false;
//     //     renderer_ = CreateRenderer();
//     //     if (renderer_) {
//     //         renderer_->Initialize(platform_, props);
//     //     }
//     // }
// }

// void Graphics::OnShutdown()
// {
//     if (renderer_) {
//         renderer_->Shutdown();
//         renderer_.reset();
//     }
//     ShutdownGraphicsBackend();
// }
