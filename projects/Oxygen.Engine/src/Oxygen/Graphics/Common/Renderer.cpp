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

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/ObjectMetaData.h>
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

    // Create a unique_ptr with custom deleter that manages both the recorder and the command list
    return {
        recorder.release(),
        [cmd_list = std::move(cmd_list)](graphics::CommandRecorder* rec) mutable {
            if (rec) {
                try {
                    // End recording
                    auto completed_cmd = rec->End();

                    // Submit to the queue
                    if (auto* queue = rec->GetTargetQueue()) {
                        queue->Submit(*completed_cmd);
                    } else {
                        LOG_F(ERROR, "Command list has no target queue for submission");
                    }

                    // TODO: queue fence management

                } catch (const std::exception& e) {
                    LOG_F(ERROR, "Exception in command recorder cleanup: %s", e.what());
                }

                delete rec;
            }
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
    // DCHECK_NOTNULL_F(command_recorder_);

    // Wait for the GPU to finish executing the previous frame, reset the
    // allocator once the GPU is done with it to free the memory we allocated to
    // store the commands.
    const auto& fence_value = frames_[CurrentFrameIndex()].fence_value;
    // command_queue_->Wait(fence_value);

    auto surface = surface_weak_.lock();
    if (surface->ShouldResize()) {
        // command_queue_->Flush();
        surface->Resize();
    }
    return static_cast<graphics::RenderTarget&>(*surface);
}

void Renderer::EndFrame()
{
    CHECK_F(!surface_weak_.expired(), "Cannot BeginFrame when surface is not valid");

    LOG_SCOPE_F(1, fmt::format("[{}] EndFrame", CurrentFrameIndex()).c_str());
    auto surface = surface_weak_.lock();
    try {
        // TODO: present the surface (need to properly use the fence)
        // surface->Present();
    } catch (const std::exception& e) {
        LOG_F(WARNING, "No surface for id=`{}`; frame discarded: {}", surface->GetName(), e.what());
    }

    // Signal and increment the fence value for the next frame.
    // frames_[CurrentFrameIndex()].fence_value = command_queue_->Signal();
    current_frame_index_ = (current_frame_index_ + 1) % frame_count_;
}
