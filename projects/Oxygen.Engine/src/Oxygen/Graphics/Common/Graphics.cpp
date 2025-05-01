//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <type_traits>
#include <unordered_map>

#include "Graphics.h"
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
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

    // Clear command list pools
    std::lock_guard<std::mutex> lock(command_list_pool_mutex_);
    command_list_pool_.clear();
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
    // Stop all valid renderers
    auto it = renderers_.begin();
    while (it != renderers_.end()) {
        if (auto renderer = it->lock()) {
            renderer->Stop();
            ++it;
        }
    }
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

auto Graphics::CreateRenderer(const std::string_view name, std::weak_ptr<graphics::Surface> surface, uint32_t frames_in_flight) -> std::shared_ptr<oxygen::graphics::Renderer>
{
    // Create the Renderer object
    auto renderer = CreateRendererImpl(name, std::move(surface), frames_in_flight);
    CHECK_NOTNULL_F(renderer, "Failed to create renderer");

    // Wrap the Renderer in a shared_ptr with a custom deleter
    auto renderer_with_deleter = std::shared_ptr<oxygen::graphics::Renderer>(
        renderer.release(),
        [this](oxygen::graphics::Renderer* ptr) {
            // Remove the Renderer from the renderers_ collection
            auto it = std::remove_if(renderers_.begin(), renderers_.end(),
                [ptr](const std::weak_ptr<oxygen::graphics::Renderer>& weak_renderer) {
                    auto shared_renderer = weak_renderer.lock();
                    return !shared_renderer || shared_renderer.get() == ptr;
                });
            renderers_.erase(it, renderers_.end());

            // Delete the Renderer
            delete ptr;
        });

    // Add a weak_ptr to the renderers_ collection
    renderers_.emplace_back(renderer_with_deleter);

    return renderer_with_deleter;
}

auto Graphics::AcquireCommandRecorder(std::string_view queue_name, std::string_view command_list_name)
    -> std::unique_ptr<graphics::CommandRecorder, std::function<void(graphics::CommandRecorder*)>>
{
    auto queue = GetCommandQueue(queue_name);
    if (!queue) {
        LOG_F(ERROR, "Command queue '{}' not found", queue_name);
        return nullptr;
    }

    // Acquire or create a command list
    std::shared_ptr<graphics::CommandList> cmd_list;
    {
        std::lock_guard<std::mutex> lock(command_list_pool_mutex_);
        auto role = queue->GetQueueType();
        auto& pool = command_list_pool_[role];

        if (pool.empty()) {
            // Create a new command list if pool is empty
            cmd_list = CreateCommandList(role, command_list_name);
        } else {
            // Take one from the pool
            cmd_list = std::move(pool.back());
            pool.pop_back();
            cmd_list->SetName(command_list_name);
        }
    }

    // Create a command recorder for this command list
    auto recorder = CreateCommandRecorder(cmd_list.get());

    // Start recording
    recorder->Begin();

    // Create a unique_ptr with custom deleter that handles submission and cleanup
    return { recorder.release(), [this, cmd_list = std::move(cmd_list), queue = std::move(queue)](graphics::CommandRecorder* rec) mutable {
                // Skip if recorder is null
                if (!rec)
                    return;

                try {
                    // End recording
                    auto completed_cmd = rec->End();

                    // Submit to specified queue
                    queue->Submit(*completed_cmd);

                    // Return to pool when execution completes
                    std::lock_guard<std::mutex> lock(command_list_pool_mutex_);
                    command_list_pool_[queue->GetQueueType()].push_back(std::move(cmd_list));
                } catch (const std::exception& e) {
                    LOG_F(ERROR, "Exception in command recorder cleanup: %s", e.what());
                }

                // Delete the recorder
                delete rec;
            } };
}
