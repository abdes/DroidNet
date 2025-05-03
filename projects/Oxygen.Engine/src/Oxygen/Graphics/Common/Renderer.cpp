//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <exception>
#include <memory>
#include <string_view>
#include <type_traits>
#include <unordered_set>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Detail/RenderThread.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/RenderTarget.h>
#include <Oxygen/Graphics/Common/Renderer.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Types/RenderTask.h>

// #include <Oxygen/Graphics/Common/CommandList.h> // Needed to forward the command list ptr

using oxygen::graphics::Renderer;
using oxygen::graphics::detail::RenderThread;

//! Default constructor, sets the object name.
Renderer::Renderer(
    std::string_view name,
    std::weak_ptr<oxygen::Graphics> gfx_weak,
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
        [this]() -> const graphics::RenderTarget& {
            return this->BeginFrame();
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

void Renderer::Stop()
{
    GetComponent<RenderThread>().Stop();
}

auto Renderer::GetGraphics() const noexcept -> std::shared_ptr<Graphics>
{
    CHECK_F(!gfx_weak_.expired(), "Unexpected use of Renderer when the Graphics backend is no longer valid");
    return gfx_weak_.lock();
}

void Renderer::Submit(FrameRenderTask task)
{
    GetComponent<RenderThread>().Submit(std::move(task));
}

auto Renderer::AcquireCommandRecorder(std::string_view queue_name, std::string_view command_list_name)
    -> std::unique_ptr<graphics::CommandRecorder, std::function<void(graphics::CommandRecorder*)>>
{
    auto gfx = GetGraphics();

    auto queue = gfx->GetCommandQueue(queue_name);
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
        [self_weak, queue_name_str, cmd_list = std::move(cmd_list)](graphics::CommandRecorder* rec) mutable {
            if (rec == nullptr) {
                return;
            }
            try {
                // End recording
                auto* completed_cmd = rec->End();
                if (completed_cmd != nullptr) {
                    auto* queue = rec->GetTargetQueue();
                    DCHECK_NOTNULL_F(queue);

                    // Submit the command list
                    queue->Submit(*completed_cmd);
                    completed_cmd->OnSubmitted();

                    // Get timeline value for tracking completion
                    uint64_t timeline_value = queue->Signal();

                    // Store timeline value and command list in the current frame
                    if (auto renderer = self_weak.lock()) {
                        uint32_t frame_idx = renderer->CurrentFrameIndex();
                        Frame& current_frame = renderer->frames_[frame_idx];
                        current_frame.timeline_values[queue_name_str] = timeline_value;
                        current_frame.pending_command_lists.push_back(cmd_list);
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

auto Renderer::BeginFrame() -> const graphics::RenderTarget&
{
    if (surface_weak_.expired()) {
        throw std::runtime_error("Cannot BeginFrame when surface is not valid");
    }

    LOG_SCOPE_F(1, fmt::format("[{}] BeginFrame", CurrentFrameIndex()).c_str());

    // Wait for the GPU to finish executing the previous frame
    Frame& previous_frame = frames_[CurrentFrameIndex()];
    for (const auto& [queue_name, fence_value] : previous_frame.timeline_values) {
        auto gfx = GetGraphics();
        auto queue = gfx->GetCommandQueue(queue_name);
        DCHECK_NOTNULL_F(queue, "Command queue '{}' not found", queue_name);
        if (queue) {
            queue->Wait(fence_value);
        }
    }

    // Release all completed command lists and call OnExecuted
    for (auto& cmd_list : previous_frame.pending_command_lists) {
        cmd_list->OnExecuted();
    }
    previous_frame.pending_command_lists.clear();
    previous_frame.timeline_values.clear();

    DCHECK_F(!surface_weak_.expired());
    auto surface = surface_weak_.lock();
    surface->Prepare();
    HandleSurfaceResize(*surface);

    return static_cast<graphics::RenderTarget&>(*surface);
}

void Renderer::EndFrame()
{
    CHECK_F(!surface_weak_.expired(), "Cannot BeginFrame when surface is not valid");

    LOG_SCOPE_F(1, fmt::format("[{}] EndFrame", CurrentFrameIndex()).c_str());
    auto surface = surface_weak_.lock();
    try {
        surface->Present();
    } catch (const std::exception& e) {
        LOG_F(WARNING, "No surface for id=`{}`; frame discarded: {}", surface->GetName(), e.what());
    }

    current_frame_index_ = (current_frame_index_ + 1) % frame_count_;
}

void Renderer::HandleSurfaceResize(Surface& surface)
{
    if (!surface.ShouldResize()) {
        return;
    }
    // Collect all queues that have pending work across any frame
    std::unordered_set<std::string> active_queues;

    // Check all frames for pending work
    for (uint32_t i = 0; i < frame_count_; ++i) {
        if (i != CurrentFrameIndex()) { // Already processed current frame above
            for (const auto& [queue_name, _] : frames_[i].timeline_values) {
                active_queues.insert(queue_name);
            }
        }
    }

    // Only flush queues with pending work from this renderer
    if (!active_queues.empty()) {
        auto gfx = GetGraphics();
        for (const auto& queue_name : active_queues) {
            auto queue = gfx->GetCommandQueue(queue_name);
            if (queue) {
                DLOG_F(INFO, "Flushing queue '{}' during resize", queue_name);
                queue->Flush();
            }
        }
    }

    surface.Resize();
}
