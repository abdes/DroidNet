//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include "oxygen/base/macros.h"
#include "Oxygen/renderer/types.h"
#include "oxygen/renderer-d3d12/detail/dx12_utils.h"

namespace oxygen::renderer::direct3d12 {

  class Command
  {
  public:
    explicit Command(ID3D12Device9* device, D3D12_COMMAND_LIST_TYPE type);
    ~Command();

    OXYGEN_MAKE_NON_COPYABLE(Command);
    OXYGEN_MAKE_NON_MOVEABLE(Command);

    [[nodiscard]] constexpr ID3D12CommandQueue* CommandQueue() const noexcept { return command_queue_; }
    [[nodiscard]] constexpr ID3D12GraphicsCommandList7* CommandList() const noexcept { return command_list_; }
    [[nodiscard]] constexpr size_t FrameIndex() const noexcept { return frame_index_; }

    void Flush()
    {
      for (auto& frame : frames_) frame.Wait(fence_event_, fence_);
      frame_index_ = 0;
    }


    void Release() noexcept
    {
      if (is_released_) return;

      Flush();

      if (command_queue_) {
        command_queue_->Release();
        command_queue_ = nullptr;
      }
      if (command_list_) {
        command_list_->Release();
        command_list_ = nullptr;
      }
      for (auto& frame : frames_) {
        frame.Release();
      }
      if (fence_) {
        fence_->Release();
        fence_ = nullptr;
      }
      fence_value_ = 0;
      if (fence_event_) {
        CloseHandle(fence_event_);
        fence_event_ = nullptr;
      }

      is_released_ = true;
    }

    void BeginFrame() const
    {
      const auto& frame = frames_[frame_index_];
      frame.Wait(fence_event_, fence_);
    }

    void EndFrame()
    {
      CheckResult(command_list_->Close());
      ID3D12CommandList* command_lists[]{ command_list_ };
      command_queue_->ExecuteCommandLists(_countof(command_lists), command_lists);

      const uint64_t fence_value{ fence_value_ };
      ++fence_value_;
      const CommandFrame& frame = frames_[frame_index_];
      CheckResult(command_queue_->Signal(fence_, fence_value));

      frame_index_ = (frame_index_ + 1) % kFrameBufferCount;

      CheckResult(frame.command_allocator->Reset());
      CheckResult(command_list_->Reset(frame.command_allocator, nullptr));
    }

  private:
    struct CommandFrame
    {
      ID3D12CommandAllocator* command_allocator{ nullptr };
      uint64_t fence_value{ 0 };

      void Wait(const HANDLE fence_event, ID3D12Fence1* fence) const
      {
        DCHECK_F(fence && fence_event);

        // If the current fence value is still less than "fence_value", then we
        // know the GPU has not finished executing the command queue since it
        // has not reached the "command_queue_->Signal()". We will wait until
        // the GPU hits this value before continuing, to ensure we do not render
        // new commands until the GPU has finished the ones we've submitted.

        if (fence->GetCompletedValue() < fence_value) {
          // We have the fence create an event which is signaled when the
          // fence's current value reaches the expected "fence_value".
          CheckResult(fence->SetEventOnCompletion(fence_value, fence_event));

          // Wait until the fence triggers the event indicating the command
          // queue has finished executing.
          WaitForSingleObject(fence_event, INFINITE);
        }
      }

      void Release()  noexcept
      {
        if (command_allocator) {
          command_allocator->Release();
          command_allocator = nullptr;
        }
      }
    };

    bool is_released_{ false };

    ID3D12CommandQueue* command_queue_{ nullptr };
    ID3D12GraphicsCommandList7* command_list_{ nullptr };
    CommandFrame frames_[kFrameBufferCount]{};
    size_t frame_index_{ 0 };

    ID3D12Fence1* fence_{ nullptr };
    uint64_t fence_value_{ 0 };
    HANDLE fence_event_{ nullptr };
  };

}  // namespace oxygen::renderer::direct3d12
