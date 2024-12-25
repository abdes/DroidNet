//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include "Oxygen/Renderers/Common/CommandQueue.h"

namespace oxygen::renderer::d3d12 {

  class CommandQueue final : public renderer::CommandQueue
  {
    using Base = renderer::CommandQueue;
  public:
    explicit CommandQueue(const CommandListType type)
      : Base(type)
    {

    }
    ~CommandQueue() override = default;

    OXYGEN_MAKE_NON_COPYABLE(CommandQueue);
    OXYGEN_MAKE_NON_MOVEABLE(CommandQueue);

    void Submit(const CommandListPtr& command_list) override;
    void Flush() override;

    [[nodiscard]] ID3D12CommandQueue* GetCommandQueue() const { return command_queue_; }

  protected:
    void OnInitialize() override;
    void OnRelease() override;
    auto CreateSynchronizationCounter() -> std::unique_ptr<ISynchronizationCounter> override;

  private:
    ID3D12CommandQueue* command_queue_{};
  };

} // namespace oxygen::renderer::d3d12
