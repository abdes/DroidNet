//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/Constants.h>
#include <Oxygen/Graphics/Common/Detail/RenderThread.h>
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
    std::shared_ptr<Surface> surface,
    uint32_t frames_in_flight)
    : surface_(std::move(surface))
{
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
    GetComponent<RenderThread>().Stop();
}

void Renderer::Submit(FrameRenderTask task)
{
    GetComponent<RenderThread>().Submit(std::move(task));
}

auto Renderer::BeginFrame() -> const graphics::RenderTarget&
{
    // DCHECK_NOTNULL_F(command_recorder_);

    // Wait for the GPU to finish executing the previous frame, reset the
    // allocator once the GPU is done with it to free the memory we allocated to
    // store the commands.
    const auto& fence_value = frames_[CurrentFrameIndex()].fence_value;
    // command_queue_->Wait(fence_value);

    if (surface_->ShouldResize()) {
        // command_queue_->Flush();
        surface_->Resize();
    }
    return static_cast<graphics::RenderTarget&>(*surface_);
}

void Renderer::EndFrame()
{
    try {
        surface_->Present();
    } catch (const std::exception& e) {
        LOG_F(WARNING, "No surface for id=`{}`; frame discarded: {}", surface_->GetName(), e.what());
    }

    // Signal and increment the fence value for the next frame.
    // frames_[CurrentFrameIndex()].fence_value = command_queue_->Signal();
    current_frame_index_ = (current_frame_index_ + 1) % kFrameBufferCount;
}
