//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <wrl/client.h>

#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics::d3d12 {

class Graphics;

class CommandQueue final : public graphics::CommandQueue {
  using Base = graphics::CommandQueue;

public:
  OXGN_D3D12_API CommandQueue(
    std::string_view name, QueueRole role, const Graphics* gfx);

  OXGN_D3D12_API ~CommandQueue() noexcept override;

  OXYGEN_MAKE_NON_COPYABLE(CommandQueue)
  OXYGEN_MAKE_NON_MOVABLE(CommandQueue)

  OXGN_D3D12_NDAPI auto GetQueueRole() const -> QueueRole override;

  OXGN_D3D12_API auto Signal(uint64_t value) const -> void override;
  OXGN_D3D12_NDAPI auto Signal() const -> uint64_t override;
  OXGN_D3D12_API auto Wait(
    uint64_t value, std::chrono::milliseconds timeout) const -> void override;
  OXGN_D3D12_API auto Wait(uint64_t value) const -> void override;
  OXGN_D3D12_NDAPI auto GetCompletedValue() const -> uint64_t override;
  OXGN_D3D12_NDAPI auto GetCurrentValue() const -> uint64_t override
  {
    std::lock_guard lock(timeline_mutex_);
    return current_value_;
  }

  OXGN_D3D12_API auto TryGetTimestampFrequency(uint64_t& out_hz) const
    -> bool override;

  OXGN_D3D12_API auto BeginProfilingFrame() const -> void override;
  OXGN_D3D12_API auto Flush() const -> void override;

  OXGN_D3D12_API auto SetName(std::string_view name) noexcept -> void override;

  OXGN_D3D12_NDAPI auto GetCommandQueue() const -> dx::ICommandQueue*
  {
    return command_queue_;
  }

  OXGN_D3D12_NDAPI auto GetFence() const -> dx::IFence* { return fence_; }
  OXGN_D3D12_NDAPI auto GetTracyContextOpaque() const -> void*
  {
    return tracy_context_;
  }

private:
  struct PreparedSubmission;
  auto PrepareNativeSubmission(
    const graphics::internal::NativeSubmissionRequest& request,
    std::unique_ptr<graphics::internal::NativeSubmission> reusable)
    -> std::unique_ptr<graphics::internal::NativeSubmission> override;
  auto QueryPrivateCompletion() const noexcept -> uint64_t override;
  auto WaitPrivateCompletion(uint64_t value) const -> void override;
  auto TearDownUncertainDevice() noexcept -> void override;
  Microsoft::WRL::ComPtr<ID3D12Fence> private_fence_;
  HANDLE private_event_ { nullptr };
  mutable std::mutex timeline_mutex_;
  auto SignalImmediate(uint64_t value) const -> void override;
  auto QueueWaitImmediate(uint64_t value) const -> void;
  auto CurrentDevice() const -> dx::IDevice*;
  auto CreateCommandQueue(QueueRole role, std::string_view queue_name) -> void;
  auto CreateFence(std::string_view fence_name, uint64_t initial_value) -> void;
  auto ReleaseCommandQueue() noexcept -> void;
  auto ReleaseFence() noexcept -> void;

  QueueRole queue_role_; //<! The cached role of the command queue.
  std::shared_ptr<void> native_lifetime_;
  Microsoft::WRL::ComPtr<dx::IDevice> device_;
  dx::ICommandQueue* command_queue_ { nullptr };

  dx::IFence* fence_ { nullptr };
  mutable uint64_t current_value_ { 0 };
  mutable uint64_t last_signaled_value_ { 0 };
  HANDLE fence_event_ { nullptr };
  void* tracy_context_ { nullptr };
};

} // namespace oxygen::graphics::d3d12
