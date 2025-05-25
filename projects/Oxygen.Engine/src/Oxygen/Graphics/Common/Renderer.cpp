//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <exception>
#include <memory>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <unordered_set>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Detail/Bindless.h>
#include <Oxygen/Graphics/Common/Detail/PerFrameResourceManager.h>
#include <Oxygen/Graphics/Common/Detail/RenderThread.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/RenderPass.h>
#include <Oxygen/Graphics/Common/Renderer.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Types/RenderTask.h>
#include <Oxygen/OxCo/Co.h>

using oxygen::graphics::Renderer;
using oxygen::graphics::RenderPass;
using oxygen::graphics::detail::Bindless;
using oxygen::graphics::detail::RenderThread;

//! Default constructor, sets the object name.
Renderer::Renderer(
    std::string_view name,
    std::weak_ptr<Graphics> gfx_weak,
    std::weak_ptr<Surface> surface_weak,
    uint32_t frames_in_flight)
    : gfx_weak_(std::move(gfx_weak))
    , surface_weak_(std::move(surface_weak))
    , frame_count_(frames_in_flight + 1)
    , frames_(std::make_unique<Frame[]>(frame_count_))
{
    CHECK_F(!surface_weak_.expired(), "Renderer cannot be created with a null Surface");
    DCHECK_F(!gfx_weak_.expired(), "Renderer cannot be created with an expired Graphics backend pointer");

    // Initialize all frame values to 0
    for (uint32_t i = 0; i < frame_count_; ++i) {
        frames_[i] = Frame {};
    }

    AddComponent<ObjectMetaData>(name);
    AddComponent<RenderThread>(
        frames_in_flight,
        [this]() {
            this->BeginFrame();
        },
        [this]() {
            this->EndFrame();
        });
}

Renderer::~Renderer()
{
    Stop();
    // TODO: cleanup the resource registry and the descriptor allocator
    DLOG_F(INFO, "Renderer destroyed");
}

auto Renderer::GetGraphics() -> Graphics&
{
    CHECK_F(!gfx_weak_.expired(), "Unexpected use of Renderer when the Graphics backend is no longer valid");
    return *gfx_weak_.lock();
}

auto Renderer::GetGraphics() const -> const Graphics&
{
    return const_cast<Renderer*>(this)->GetGraphics();
}

auto Renderer::GetDescriptorAllocator() -> DescriptorAllocator&
{
    return GetComponent<Bindless>().GetAllocator();
}

auto Renderer::GetDescriptorAllocator() const -> const DescriptorAllocator&
{
    return const_cast<Renderer*>(this)->GetDescriptorAllocator();
}

auto Renderer::GetResourceRegistry() -> ResourceRegistry&
{
    return GetComponent<Bindless>().GetRegistry();
}

auto Renderer::GetResourceRegistry() const -> const ResourceRegistry&
{
    return const_cast<Renderer*>(this)->GetResourceRegistry();
}

// ReSharper disable once CppMemberFunctionMayBeConst
void Renderer::Stop()
{
    GetComponent<RenderThread>().Stop();
    per_frame_resource_manager_.OnRendererShutdown();
}

// ReSharper disable once CppMemberFunctionMayBeConst
void Renderer::Submit(FrameRenderTask task)
{
    GetComponent<RenderThread>().Submit(std::move(task));
}

auto Renderer::AcquireCommandRecorder(
    const std::string_view queue_name,
    const std::string_view command_list_name,
    bool immediate_submission)
    -> std::unique_ptr<CommandRecorder, std::function<void(CommandRecorder*)>>
{
    CHECK_F(!gfx_weak_.expired(), "Unexpected use of Renderer when the Graphics backend is no longer valid");
    const auto gfx = gfx_weak_.lock();

    const auto queue = gfx->GetCommandQueue(queue_name);
    if (!queue) {
        LOG_F(ERROR, "Command queue '{}' not found", queue_name);
        return nullptr;
    }

    // Get a command list with automatic cleanup
    auto cmd_list = gfx->AcquireCommandList(queue->GetQueueRole(), command_list_name);
    if (!cmd_list) {
        return nullptr;
    }

    // Create a command recorder for this command list
    auto recorder = CreateCommandRecorder(cmd_list.get(), queue.get());
    if (!recorder) {
        return nullptr;
    }
    recorder->Begin();

    // Prepare the command list for bindless rendering
    PrepareRecorderForRender(*recorder);

    return {
        recorder.release(),
        [self_weak = weak_from_this(),
            cmd_list = std::move(cmd_list),
            queue = queue.get(), immediate_submission](CommandRecorder* rec) mutable {
            if (rec == nullptr) {
                return;
            }
            if (self_weak.expired()) {
                LOG_F(ERROR, "Renderer is no longer valid");
                delete rec;
                return;
            }
            auto renderer = self_weak.lock();
            try {
                if (immediate_submission) {
                    if (auto* completed_cmd = rec->End(); completed_cmd != nullptr) {
                        auto* target_queue = rec->GetTargetQueue();
                        DCHECK_NOTNULL_F(target_queue);
                        target_queue->Submit(*completed_cmd);
                        rec->OnSubmitted();
                        const uint64_t timeline_value = target_queue->Signal();
                        const uint32_t frame_idx = renderer->CurrentFrameIndex();
                        auto& [timeline_values, pending_command_lists] = renderer->frames_[frame_idx];
                        timeline_values[queue] = timeline_value;
                        pending_command_lists.emplace_back(cmd_list, queue);
                    }
                } else {
                    // Deferred: just end, don't submit. Add to pending_command_lists for later flush.
                    if (auto* completed_cmd = rec->End(); completed_cmd != nullptr) {
                        const uint32_t frame_idx = renderer->CurrentFrameIndex();
                        auto& [timeline_values, pending_command_lists] = renderer->frames_[frame_idx];
                        pending_command_lists.emplace_back(cmd_list, queue);
                    }
                }
            } catch (const std::exception& ex) {
                LOG_F(ERROR, "Exception in command recorder cleanup: {}", ex.what());
            }
            delete rec;
        }
    };
}

void Renderer::FlushPendingCommandLists()
{
    auto& [timeline_values, pending_command_lists] = frames_[CurrentFrameIndex()];
    if (pending_command_lists.empty())
        return;
    if (!gfx_weak_.expired()) {
        auto it = pending_command_lists.begin();
        while (it != pending_command_lists.end()) {
            CommandQueue* current_queue = it->second;
            std::vector<CommandList*> batch;
            auto batch_start = it;
            // Collect contiguous command lists for the same queue, only if closed and not submitted
            while (it != pending_command_lists.end() && it->second == current_queue) {
                if (it->first && it->first->IsClosed() && !it->first->IsSubmitted()) {
                    batch.push_back(it->first.get());
                }
                ++it;
            }
            if (!batch.empty()) {
                current_queue->Submit(std::span<CommandList*> { batch });
                uint64_t timeline_value = current_queue->Signal();
                timeline_values[current_queue] = timeline_value;
                for (auto* cmd_list : batch) {
                    cmd_list->OnSubmitted();
                }
            }
        }
    }
    // Do not clear pending_command_lists or timeline_values here; this is done in BeginFrame().
}

void Renderer::BeginFrame()
{
    CHECK_F(!gfx_weak_.expired(), "Unexpected use of Renderer when the Graphics backend is no longer valid");

    if (surface_weak_.expired()) {
        throw std::runtime_error("Cannot BeginFrame when surface is not valid");
    }

    auto& [timeline_values, pending_command_lists] = frames_[CurrentFrameIndex()];

    LOG_SCOPE_FUNCTION(1);
    DLOG_F(1, "Frame index: {}", CurrentFrameIndex());

    // NB: Must handle surface resize early as it may affect the current frame
    // index.

    if (const auto surface = surface_weak_.lock(); surface->ShouldResize()) {
        // This will flush the command queues and wait for all pending work to
        // finish for all frames, release all deferred resources and resize the
        // swapchain.
        HandleSurfaceResize(*surface);
        DLOG_F(1, "Frame index after resize: {}", CurrentFrameIndex());
    } else {
        // Wait for the GPU to finish executing the previous frame (only for the
        // current frame index).
        for (const auto& [queue, fence_value] : timeline_values) {
            const auto gfx = gfx_weak_.lock();
            DCHECK_NOTNULL_F(queue, "Command queue is null");
            if (queue) {
                queue->Wait(fence_value);
            }
        }

        // Process all deferred releases for the current frame
        per_frame_resource_manager_.OnBeginFrame(CurrentFrameIndex());
    }

    // Release all completed command lists and call OnExecuted
    for (const auto& [cmd_list, queue] : pending_command_lists) {
        cmd_list->OnExecuted();
    }
    pending_command_lists.clear();
    timeline_values.clear();
}

void Renderer::EndFrame()
{
    CHECK_F(!surface_weak_.expired(), "Cannot BeginFrame when surface is not valid");

    LOG_SCOPE_FUNCTION(1);
    DLOG_F(1, "Frame index: {}", CurrentFrameIndex());

    // Always flush before presenting
    FlushPendingCommandLists();

    const auto surface = surface_weak_.lock();
    try {
        surface->Present();
    } catch (const std::exception& e) {
        LOG_F(WARNING, "Present on surface `{}` failed; frame discarded: {}", surface->GetName(), e.what());
    }

    current_frame_index_ = (current_frame_index_ + 1) % frame_count_;
}

void Renderer::HandleSurfaceResize(Surface& surface)
{
    DCHECK_F(!gfx_weak_.expired(), "Unexpected use of Renderer when the Graphics backend is no longer valid");

    // Collect all queues that have pending work across any frame
    std::unordered_set<CommandQueue*> active_queues;

    // Check all frames for pending work
    for (uint32_t i = 0; i < frame_count_; ++i) {
        if (i != CurrentFrameIndex()) { // Already processed current frame above
            for (const auto& queue : frames_[i].timeline_values | std::views::keys) {
                active_queues.insert(queue);
            }
        }
    }

    // Only flush queues with pending work from this renderer
    if (!active_queues.empty()) {
        const auto gfx = gfx_weak_.lock();
        for (const auto& queue : active_queues) {
            DLOG_F(INFO, "Flushing queue '{}' during resize", queue->GetName());
            queue->Flush();
        }
    }

    // Process all deferred releases for all frames since we have flushed all
    // pending work for all frames and we are going to reset the swapchain.
    per_frame_resource_manager_.ProcessAllDeferredReleases();

    surface.Resize();
    current_frame_index_ = surface.GetCurrentBackBufferIndex();
}

namespace oxygen::graphics {

// Generic no-op implementation for any render pass type.
class NullRenderPass : public RenderPass {
public:
    NullRenderPass(std::string_view name = "NullRenderPass")
        : RenderPass(name)
    {
    }

    ~NullRenderPass() noexcept override = default;

    OXYGEN_DEFAULT_COPYABLE(NullRenderPass)
    OXYGEN_DEFAULT_MOVABLE(NullRenderPass)

    co::Co<> PrepareResources(CommandRecorder&) override { co_return; }
    co::Co<> Execute(CommandRecorder&) override { co_return; }
    void SetViewport(const ViewPort&) override { }
    void SetScissors(const Scissors&) override { }
    void SetClearColor(const Color&) override { }
    void SetEnabled(bool) override { }
    bool IsEnabled() const override { return false; }
    std::string_view GetName() const noexcept override { return name_; }
    void SetName(std::string_view name) noexcept override { name_ = std::string(name); }

private:
    std::string name_;
};

} // namespace oxygen::graphics

auto Renderer::CreateNullRenderPass() -> std::shared_ptr<RenderPass>
{
    return std::make_shared<NullRenderPass>();
}
