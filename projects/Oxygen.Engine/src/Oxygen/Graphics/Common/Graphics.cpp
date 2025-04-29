//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/Graphics.h>

#include <type_traits>

#include "Graphics.h"
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Detail/RenderThread.h>
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
    AddComponent<oxygen::graphics::detail::RenderThread>();
}

oxygen::Graphics::~Graphics()
{
    delete render_thread_;
}

auto Graphics::ActivateAsync(co::TaskStarted<> started) -> co::Co<>
{
    DLOG_F(INFO, "Graphics Live Object activating...");
    return OpenNursery(nursery_, std::move(started));
}

void Graphics::Run()
{
    DLOG_F(INFO, "Starting Graphics render thread...");
    render_thread_ = new RenderThread(kFrameBufferCount - 1);
}

auto Graphics::IsRunning() const -> bool
{
    return nursery_ != nullptr;
}

void Graphics::Stop()
{
    if (render_thread_) {
        render_thread_->Stop();
    }

    if (nursery_ == nullptr) {
        nursery_->Cancel();
    }

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
