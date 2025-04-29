//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <type_traits>
#include <unordered_map>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Detail/RenderThread.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PerFrameResourceManager.h>
#include <Oxygen/Graphics/Common/Renderer.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Platform/Types.h>

using oxygen::Graphics;

using oxygen::graphics::detail::RenderThread;

Graphics::Graphics(const std::string_view name)
{
    AddComponent<ObjectMetaData>(name);
}

oxygen::Graphics::~Graphics()
{
    // Ensure we have no active renderers
    renderers_.clear();
}

auto Graphics::ActivateAsync(co::TaskStarted<> started) -> co::Co<>
{
    DLOG_F(INFO, "Graphics Live Object activating...");
    return OpenNursery(nursery_, std::move(started));
}

void Graphics::Run()
{
    DLOG_F(INFO, "Starting Graphics backend async tasks...");
}

auto Graphics::IsRunning() const -> bool
{
    return nursery_ != nullptr;
}

void Graphics::Stop()
{
    // Destroy all renderers, stopping any background tasks they may have
    // started and releasing any resources they may have created.
    renderers_.clear();

    if (nursery_ == nullptr) {
        nursery_->Cancel();
    }

    command_queues_.clear();

    DLOG_F(INFO, "Graphics Live Object stopped");
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

auto Graphics::CreateRenderer(const std::string_view name, std::shared_ptr<graphics::Surface> surface, uint32_t frames_in_flight) -> std::shared_ptr<oxygen::graphics::Renderer>
{
    auto renderer = CreateRendererImpl(name, std::move(surface), frames_in_flight);
    CHECK_NOTNULL_F(renderer, "Failed to create renderer");
    renderers_.emplace_back(std::move(renderer));
    return renderer;
}
