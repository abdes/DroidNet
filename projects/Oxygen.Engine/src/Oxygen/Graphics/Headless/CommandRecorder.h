//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <unordered_map>

#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Detail/Barriers.h>
#include <Oxygen/Graphics/Headless/Command.h>
#include <Oxygen/Graphics/Headless/api_export.h>

namespace oxygen::graphics::headless {

class CommandRecorder final : public graphics::CommandRecorder {
public:
  CommandRecorder(std::shared_ptr<graphics::CommandList> cmd_list,
    observer_ptr<graphics::CommandQueue> queue)
    : graphics::CommandRecorder(cmd_list, queue)
  {
  }

  OXGN_HDLS_API ~CommandRecorder() override;

  // Pipeline state
  auto SetPipelineState(GraphicsPipelineDesc /*desc*/) -> void override { }
  auto SetPipelineState(ComputePipelineDesc /*desc*/) -> void override { }

  // Root constants / 32-bit constants
  auto SetGraphicsRootConstantBufferView(uint32_t, uint64_t) -> void override {
  }
  auto SetComputeRootConstantBufferView(uint32_t, uint64_t) -> void override { }
  auto SetGraphicsRoot32BitConstant(uint32_t, uint32_t, uint32_t)
    -> void override
  {
  }
  auto SetComputeRoot32BitConstant(uint32_t, uint32_t, uint32_t)
    -> void override
  {
  }

  // Render state
  auto SetRenderTargets(std::span<NativeObject> /*rtvs*/,
    std::optional<NativeObject> /*dsv*/) -> void override
  {
  }
  auto SetViewport(const ViewPort& /*viewport*/) -> void override { }
  auto SetScissors(const Scissors& /*scissors*/) -> void override { }

  // Draw / dispatch
  auto Draw(uint32_t, uint32_t, uint32_t, uint32_t) -> void override { }

  auto Dispatch(uint32_t, uint32_t, uint32_t) -> void override { }
  auto SetVertexBuffers(uint32_t, const std::shared_ptr<graphics::Buffer>*,
    const uint32_t*) const -> void override
  {
  }
  auto BindIndexBuffer(const graphics::Buffer&, Format) -> void override { }

  // Framebuffer / resource ops
  auto BindFrameBuffer(const Framebuffer&) -> void override { }
  OXGN_HDLS_API auto ClearDepthStencilView(const Texture&, const NativeObject&,
    ClearFlags, float, uint8_t) -> void override;
  OXGN_HDLS_API auto ClearFramebuffer(const Framebuffer&,
    std::optional<std::vector<std::optional<Color>>> /*color_clear_values*/,
    std::optional<float> /*depth_clear_value*/,
    std::optional<uint8_t> /*stencil_clear_value*/) -> void override;
  OXGN_HDLS_API auto CopyBuffer(graphics::Buffer&, size_t,
    const graphics::Buffer&, size_t, size_t) -> void override;
  OXGN_HDLS_API auto CopyBufferToTexture(const graphics::Buffer& src,
    const TextureUploadRegion& region, Texture& dst) -> void override;

  OXGN_HDLS_API auto CopyBufferToTexture(const graphics::Buffer& src,
    std::span<const TextureUploadRegion> regions, Texture& dst)
    -> void override;

  // Record a GPU-side queue signal into the command stream. When the
  // recorded command executes in OnSubmitted(), it will call
  // `target_queue->QueueSignalCommand(value)` and thus advance the queue's
  // completed value at the point of execution (GPU-like semantics).
  OXGN_HDLS_API auto RecordQueueSignal(uint64_t value) -> void;

  // Record a GPU-side queue wait into the command stream. When executed in
  // OnSubmitted(), the recorded command will call
  // `target_queue->QueueWaitCommand(value)` so the wait occurs at the recorded
  // point in the stream (GPU-like semantics).
  OXGN_HDLS_API auto RecordQueueWait(uint64_t value) -> void;

protected:
  OXGN_HDLS_API auto ExecuteBarriers(
    std::span<const detail::Barrier> /*barriers*/) -> void override;

private:
  auto QueueCommand(std::shared_ptr<Command> cmd) -> void;

  // Observed resource states for this recorder. ResourceStateTracker inside
  // the base CommandRecorder produces a list of pending barriers which is
  // passed into ExecuteBarriers; this member keeps the last-known state per
  // native resource for validation and simulation of transitions.
  std::unordered_map<NativeObject, ResourceStates> observed_states_;

  // Helper to perform a single copy immediately (kept for utility).
  auto PerformCopy(graphics::Buffer& dst, size_t dst_offset,
    const graphics::Buffer& src, size_t src_offset, size_t size) -> void;
};

} // namespace oxygen::graphics::headless
