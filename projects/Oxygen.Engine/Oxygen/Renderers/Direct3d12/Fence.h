//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <d3d12.h>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Renderers/Common/SynchronizationCounter.h"
#include "Oxygen/Renderers/Direct3d12/Types.h"

namespace oxygen::renderer::d3d12 {

  class Fence final : public SynchronizationCounter
  {
  public:
    explicit Fence(ID3D12CommandQueue* command_queue)
      : SynchronizationCounter("Fence"), command_queue_{ command_queue }
    {
    }

    ~Fence() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Fence);
    OXYGEN_MAKE_NON_MOVEABLE(Fence);

    void Signal(uint64_t value) const override;
    [[nodiscard]] uint64_t Signal() const override;
    void Wait(uint64_t value, std::chrono::milliseconds timeout) const override;
    void Wait(uint64_t value) const override;
    void QueueWaitCommand(uint64_t value) const override;
    void QueueSignalCommand(uint64_t value) override;

    [[nodiscard]] auto GetCompletedValue() const->uint64_t override;
    [[nodiscard]] auto GetCurrentValue() const->uint64_t override { return current_value_; }

  protected:
    void InitializeSynchronizationObject(uint64_t initial_value) override;
    void ReleaseSynchronizationObject() noexcept override;

  private:
    mutable uint64_t current_value_{ 0 };

    ID3DFenceV* fence_{ nullptr };
    ID3D12CommandQueue* command_queue_;
    HANDLE fence_event_{ nullptr };
  };

}  // namespace oxygen::renderer::d3d12
