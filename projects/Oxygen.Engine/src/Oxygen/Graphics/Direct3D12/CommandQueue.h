//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics::d3d12 {

class CommandQueue final : public graphics::CommandQueue {
  using Base = graphics::CommandQueue;

public:
  OXYGEN_D3D12_API CommandQueue(
    std::string_view name, QueueRole role, const Graphics* gfx);

  OXYGEN_D3D12_API ~CommandQueue() noexcept override;

  OXYGEN_MAKE_NON_COPYABLE(CommandQueue)
  OXYGEN_MAKE_NON_MOVABLE(CommandQueue)

  [[nodiscard]] OXYGEN_D3D12_API auto GetQueueRole() const
    -> QueueRole override;

  OXYGEN_D3D12_API void Signal(uint64_t value) const override;
  [[nodiscard]] OXYGEN_D3D12_API auto Signal() const -> uint64_t override;
  OXYGEN_D3D12_API void Wait(
    uint64_t value, std::chrono::milliseconds timeout) const override;
  OXYGEN_D3D12_API void Wait(uint64_t value) const override;
  OXYGEN_D3D12_API void QueueSignalCommand(uint64_t value) override;
  OXYGEN_D3D12_API void QueueWaitCommand(uint64_t value) const override;
  [[nodiscard]] OXYGEN_D3D12_API auto GetCompletedValue() const
    -> uint64_t override;
  [[nodiscard]] OXYGEN_D3D12_API auto GetCurrentValue() const
    -> uint64_t override
  {
    return current_value_;
  }

  OXYGEN_D3D12_API void Submit(graphics::CommandList& command_list) override;
  OXYGEN_D3D12_API void Submit(
    std::span<graphics::CommandList*> command_lists) override;

  OXYGEN_D3D12_API void SetName(std::string_view name) noexcept override;

  [[nodiscard]] OXYGEN_D3D12_API auto GetCommandQueue() const
    -> dx::ICommandQueue*
  {
    return command_queue_;
  }

  [[nodiscard]] OXYGEN_D3D12_API auto GetFence() const -> dx::IFence*
  {
    return fence_;
  }

private:
  auto CurrentDevice() const -> dx::IDevice*;
  void CreateCommandQueue(QueueRole role, std::string_view queue_name);
  void CreateFence(std::string_view fence_name, uint64_t initial_value);
  void ReleaseCommandQueue() noexcept;
  void ReleaseFence() noexcept;

  QueueRole queue_role_; //<! The cached role of the command queue.
  const Graphics* gfx_ {
    nullptr
  }; //<! The graphics context this command queue belongs to.
  dx::ICommandQueue* command_queue_ { nullptr };

  dx::IFence* fence_ { nullptr };
  mutable uint64_t current_value_ { 0 };
  HANDLE fence_event_ { nullptr };
};

} // namespace oxygen::graphics::d3d12
