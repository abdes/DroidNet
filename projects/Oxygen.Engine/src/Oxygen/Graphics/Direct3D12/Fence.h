//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Graphics/Common/SynchronizationCounter.h"
#include "Oxygen/Graphics/Direct3d12/Forward.h"
#include "Oxygen/Graphics/Direct3d12/api_export.h"

namespace oxygen::graphics::d3d12 {

class Fence final : public SynchronizationCounter {
public:
    explicit Fence(ID3D12CommandQueue* command_queue)
        : SynchronizationCounter("Fence")
        , command_queue_ { command_queue }
    {
    }

    OXYGEN_D3D12_API ~Fence() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Fence);
    OXYGEN_MAKE_NON_MOVEABLE(Fence);

    OXYGEN_D3D12_API void Signal(uint64_t value) const override;
    OXYGEN_D3D12_API [[nodiscard]] uint64_t Signal() const override;
    OXYGEN_D3D12_API void Wait(uint64_t value, std::chrono::milliseconds timeout) const override;
    OXYGEN_D3D12_API void Wait(uint64_t value) const override;
    OXYGEN_D3D12_API void QueueWaitCommand(uint64_t value) const override;
    OXYGEN_D3D12_API void QueueSignalCommand(uint64_t value) override;

    OXYGEN_D3D12_API [[nodiscard]] auto GetCompletedValue() const -> uint64_t override;
    [[nodiscard]] auto GetCurrentValue() const -> uint64_t override { return current_value_; }

protected:
    OXYGEN_D3D12_API void InitializeSynchronizationObject(uint64_t initial_value) override;
    OXYGEN_D3D12_API void ReleaseSynchronizationObject() noexcept override;

private:
    mutable uint64_t current_value_ { 0 };

    ID3DFenceV* fence_ { nullptr };
    ID3D12CommandQueue* command_queue_;
    HANDLE fence_event_ { nullptr };
};

} // namespace oxygen::graphics::d3d12
