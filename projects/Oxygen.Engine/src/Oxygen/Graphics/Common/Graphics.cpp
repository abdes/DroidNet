//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/Graphics.h>

#include <type_traits>

#include "Graphics.h"
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

void Graphics::CreateCommandQueues(const graphics::QueueStrategy& queue_strategy)
{
    LOG_IF_F(INFO, !command_queues_.empty(), "Re-creating command queues for the graphics backend");
    command_queues_.clear();

    auto queue_specs = queue_strategy.Specifications();
    std::unordered_map<std::string, std::shared_ptr<graphics::CommandQueue>> temp_queues;

    try {
        for (const auto& spec : queue_specs) {
            auto queue = CreateCommandQueue(spec.role, spec.allocation_preference);
            // If CreateCommandQueue does not throw, queue is guaranteed to be not null.
            temp_queues.emplace(spec.name, std::move(queue));
        }
        command_queues_ = std::move(temp_queues);
    } catch (...) {
        // Destroy all previously created queues
        temp_queues.clear();
        throw;
    }
}

auto Graphics::GetCommandQueue(std::string_view name) const -> std::shared_ptr<graphics::CommandQueue>
{
    auto it = std::ranges::find(command_queues_, name, [](const auto& pair) { return pair.first; });
    if (it != command_queues_.end()) {
        return it->second;
    }

    LOG_F(WARNING, "Command queue '{}' not found", name);
    return {};
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
