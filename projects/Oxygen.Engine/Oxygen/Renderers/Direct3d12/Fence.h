//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Renderers/Common/IFence.h"
#include "Oxygen/Renderers/Direct3d12/D3DPtr.h"

namespace oxygen::renderer::d3d12 {

  class Fence final : public IFence
  {
  public:
    Fence() = default;
    ~Fence() override;

    OXYGEN_MAKE_NON_COPYABLE(Fence);
    OXYGEN_MAKE_NON_MOVEABLE(Fence);

    void Initialize(uint64_t initial_value = 0) override;
    void Release() noexcept override;
    void Signal(uint64_t value) override;
    void Wait(uint64_t value) const override;
    void Reset(uint64_t value) override;
    [[nodiscard]] auto GetCompletedValue() const->uint64_t override;

    /// Needed to access the fence object from the command queue for GPU side
    /// signaling.
    [[nodiscard]] FenceType* GetFenceObject() const { return fence_.get(); }

  private:
    D3DPtr<FenceType> fence_{ nullptr };
    HANDLE fence_event_{ nullptr };
    uint64_t current_value_{ 0 };

    bool should_release_{ false };
  };

}  // namespace oxygen::renderer::d3d12
