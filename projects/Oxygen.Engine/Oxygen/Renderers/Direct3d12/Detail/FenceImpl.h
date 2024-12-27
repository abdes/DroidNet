//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#include <d3d12.h>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Renderers/Direct3d12/D3DPtr.h"
#include "Oxygen/Renderers/Direct3d12/Types.h"

namespace oxygen::renderer::d3d12::detail {
  class FenceImpl final
  {
  public:
    explicit FenceImpl(ID3D12CommandQueue* command_queue);
    ~FenceImpl();

    OXYGEN_MAKE_NON_COPYABLE(FenceImpl);
    OXYGEN_MAKE_NON_MOVEABLE(FenceImpl);

    void OnInitialize(uint64_t initial_value = 0);
    void OnRelease() noexcept;

    void Signal(uint64_t value) const;
    void Wait(uint64_t value, DWORD milliseconds) const;

    void QueueWaitCommand(uint64_t value) const;
    void QueueSignalCommand(uint64_t value) const;

    [[nodiscard]] auto GetCompletedValue() const->uint64_t;

  private:
    ID3D12CommandQueue* command_queue_;

    D3DDeferredPtr<FenceType> fence_{ nullptr };
    HANDLE fence_event_{ nullptr };
  };
}  // namespace oxygen::renderer::d3d12::detail
