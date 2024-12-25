//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include "Oxygen/Renderers/Common/CommandList.h"

namespace oxygen::renderer::d3d12 {

  class CommandQueue;
  class CommandRecorder;

  class CommandList final : public renderer::CommandList
  {
    using Base = renderer::CommandList;
  public:
    enum class State : int8_t
    {
      kInvalid = -1,        //<! Invalid state

      kFree = 0,            //<! Free command list.
      kRecording = 1,       //<! Command list is being recorded.
      kRecorded = 2,        //<! Command list is recorded and ready to be submitted.
      kExecuting = 3,       //<! Command list is being executed.
    };

    CommandList() = default;
    ~CommandList() override = default;

    OXYGEN_MAKE_NON_COPYABLE(CommandList);
    OXYGEN_DEFAULT_MOVABLE(CommandList);

    void OnInitialize(CommandListType type) override;
    void OnRelease() override;

    ID3D12GraphicsCommandList* GetCommandList() const { return command_list_; }
    State GetState() const { return state_; }

  private:
    friend class CommandRecorder;
    friend class CommandQueue;
    void OnBeginRecording();
    void OnEndRecording();
    void OnSubmitted();
    void OnExecuted();

    ID3D12GraphicsCommandList* command_list_{};
    ID3D12CommandAllocator* command_allocator_{};
    State state_{ State::kInvalid };
  };

} // namespace oxygen::renderer::d3d12
