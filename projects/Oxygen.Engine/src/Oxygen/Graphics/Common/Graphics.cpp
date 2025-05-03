//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/Detail/RenderThread.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Renderer.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>

using oxygen::Graphics;

using oxygen::graphics::detail::RenderThread;

Graphics::Graphics(const std::string_view name)
{
    AddComponent<ObjectMetaData>(name);
}

Graphics::~Graphics()
{
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
    // Flush all command queues
    FlushCommandQueues();

    // Stop all valid renderers
    auto it = renderers_.begin();
    while (it != renderers_.end()) {
        if (auto renderer = it->lock()) {
            renderer->Stop();
            ++it;
        }
    }
    renderers_.clear();

    if (nursery_ != nullptr) {
        nursery_->Cancel();
    }

    // Clear command list pool
    {
        std::lock_guard<std::mutex> lock(command_list_pool_mutex_);
        command_list_pool_.clear();
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
            auto queue = CreateCommandQueue(spec.name, spec.role, spec.allocation_preference);
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

void Graphics::FlushCommandQueues()
{
    LOG_SCOPE_F(1, "Flushing all command queues");
    for (const auto& [name, queue] : command_queues_) {
        DCHECK_NOTNULL_F(queue);
        queue->Flush();
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

auto Graphics::AcquireCommandList(graphics::QueueRole queue_role, std::string_view command_list_name)
    -> std::shared_ptr<graphics::CommandList>
{
    // Acquire or create a command list
    std::unique_ptr<graphics::CommandList> cmd_list;
    {
        std::lock_guard<std::mutex> lock(command_list_pool_mutex_);
        auto& pool = command_list_pool_[queue_role];

        if (pool.empty()) {
            // Create a new command list if pool is empty
            cmd_list = CreateCommandListImpl(queue_role, command_list_name);
        } else {
            // Take one from the pool
            cmd_list = std::move(pool.back());
            pool.pop_back();
            cmd_list->SetName(command_list_name);
        }
    }

    // Create a shared_ptr with custom deleter that returns the command list to the pool
    return {
        cmd_list.get(),
        [this, queue_role, cmd_list_raw = cmd_list.release()](graphics::CommandList*) mutable {
            cmd_list_raw->SetName("Recycled Command List");
            // Create a new unique_ptr that owns the command list
            auto recycled_cmd_list = std::unique_ptr<graphics::CommandList>(cmd_list_raw);

            // Return to pool
            std::lock_guard<std::mutex> lock(command_list_pool_mutex_);
            command_list_pool_[queue_role].push_back(std::move(recycled_cmd_list));
        }
    };

    // The Original shared_ptr will be destroyed, but the command list is now
    // managed by the custom deleter and will be returned to the pool when the
    // returned shared_ptr is destroyed
}
