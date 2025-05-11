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
#include <Oxygen/Graphics/Common/Detail/PerFrameResourceManager.h>
#include <Oxygen/Graphics/Common/Detail/RenderThread.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Renderer.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Types/RenderTask.h>

using oxygen::graphics::Renderer;
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
    , per_frame_resource_manager_(std::make_shared<detail::PerFrameResourceManager>())
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
    DLOG_F(INFO, "Renderer destroyed");
}

// ReSharper disable once CppMemberFunctionMayBeConst
void Renderer::Stop()
{
    GetComponent<RenderThread>().Stop();
    per_frame_resource_manager_->OnRendererShutdown();
}

// ReSharper disable once CppMemberFunctionMayBeConst
void Renderer::Submit(FrameRenderTask task)
{
    GetComponent<RenderThread>().Submit(std::move(task));
}

auto Renderer::AcquireCommandRecorder(const std::string_view queue_name, const std::string_view command_list_name)
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

    // Start recording
    recorder->Begin();

    // Capture a weak_ptr to this renderer to avoid circular references
    std::weak_ptr<Renderer> self_weak = weak_from_this();
    std::string queue_name_str(queue_name);

    // Create a unique_ptr with custom deleter that manages the recorder's lifetime
    // queue and command list when the recording is done.
    return {
        recorder.release(),
        [self_weak, queue_name_str, cmd_list = std::move(cmd_list)](CommandRecorder* rec) mutable {
            if (rec == nullptr) {
                return;
            }
            try {
                // End recording
                if (auto* completed_cmd = rec->End(); completed_cmd != nullptr) {
                    auto* target_queue = rec->GetTargetQueue();
                    DCHECK_NOTNULL_F(target_queue);

                    // Submit the command list
                    target_queue->Submit(*completed_cmd);
                    rec->OnSubmitted();

                    // Get timeline value for tracking completion
                    const uint64_t timeline_value = target_queue->Signal();

                    // Store timeline value and command list in the current frame
                    if (const auto renderer = self_weak.lock()) {
                        const uint32_t frame_idx = renderer->CurrentFrameIndex();
                        auto& [timeline_values, pending_command_lists] = renderer->frames_[frame_idx];
                        timeline_values[queue_name_str] = timeline_value;
                        pending_command_lists.push_back(cmd_list);
                    }
                }
            } catch (const std::exception& ex) {
                LOG_F(ERROR, "Exception in command recorder cleanup: {}", ex.what());
            }

            delete rec;
            // cmd_list will be automatically released and returned to the pool here
        }
    };
}

void Renderer::BeginFrame()
{
    CHECK_F(!gfx_weak_.expired(), "Unexpected use of Renderer when the Graphics backend is no longer valid");

    if (surface_weak_.expired()) {
        throw std::runtime_error("Cannot BeginFrame when surface is not valid");
    }

    LOG_SCOPE_FUNCTION(1);
    DLOG_F(1, "Frame index: {}", CurrentFrameIndex());

    // Wait for the GPU to finish executing the previous frame
    auto& [timeline_values, pending_command_lists] = frames_[CurrentFrameIndex()];
    for (const auto& [queue_name, fence_value] : timeline_values) {
        const auto gfx = gfx_weak_.lock();
        auto queue = gfx->GetCommandQueue(queue_name);
        DCHECK_NOTNULL_F(queue, "Command queue '{}' not found", queue_name);
        if (queue) {
            queue->Wait(fence_value);
        }
    }

    // Process all deferred releases for the current frame
    per_frame_resource_manager_->OnBeginFrame(CurrentFrameIndex());

    // Release all completed command lists and call OnExecuted
    for (const auto& cmd_list : pending_command_lists) {
        cmd_list->OnExecuted();
    }
    pending_command_lists.clear();
    timeline_values.clear();

    DCHECK_F(!surface_weak_.expired());
    const auto surface = surface_weak_.lock();
    HandleSurfaceResize(*surface);
}

void Renderer::EndFrame()
{
    CHECK_F(!surface_weak_.expired(), "Cannot BeginFrame when surface is not valid");

    LOG_SCOPE_FUNCTION(1);
    DLOG_F(1, "Frame index: {}", CurrentFrameIndex());

    const auto surface = surface_weak_.lock();
    try {
        surface->Present();
    } catch (const std::exception& e) {
        LOG_F(WARNING, "No surface for id=`{}`; frame discarded: {}", surface->GetName(), e.what());
    }

    current_frame_index_ = (current_frame_index_ + 1) % frame_count_;
}

void Renderer::HandleSurfaceResize(Surface& surface)
{
    DCHECK_F(!gfx_weak_.expired(), "Unexpected use of Renderer when the Graphics backend is no longer valid");

    if (!surface.ShouldResize()) {
        return;
    }

    // Collect all queues that have pending work across any frame
    std::unordered_set<std::string> active_queues;

    // Check all frames for pending work
    for (uint32_t i = 0; i < frame_count_; ++i) {
        if (i != CurrentFrameIndex()) { // Already processed current frame above
            for (const auto& queue_name : frames_[i].timeline_values | std::views::keys) {
                active_queues.insert(queue_name);
            }
        }
    }

    // Only flush queues with pending work from this renderer
    if (!active_queues.empty()) {
        const auto gfx = gfx_weak_.lock();
        for (const auto& queue_name : active_queues) {
            if (const auto queue = gfx->GetCommandQueue(queue_name)) {
                DLOG_F(INFO, "Flushing queue '{}' during resize", queue_name);
                queue->Flush();
            }
        }
    }

    surface.Resize();
}
