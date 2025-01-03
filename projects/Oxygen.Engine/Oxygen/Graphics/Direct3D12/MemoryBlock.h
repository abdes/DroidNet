//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <wrl/client.h>

#include "Oxygen/Graphics/Common/MemoryBlock.h"

namespace D3D12MA {
  class Allocation;
}

namespace oxygen::renderer::d3d12 {

  class MemoryBlock final : public IMemoryBlock
  {
  public:
    MemoryBlock();
    ~MemoryBlock() override;

    OXYGEN_MAKE_NON_COPYABLE(MemoryBlock);
    OXYGEN_MAKE_NON_MOVEABLE(MemoryBlock);

    void Init(const MemoryBlockDesc& desc);

    [[nodiscard]] uint64_t GetSize() const { return size_; }
    [[nodiscard]] uint32_t GetAlignment() const { return alignment_; }
    [[nodiscard]] D3D12MA::Allocation* GetAllocation() const { return allocation_.Get(); }

  private:
    uint64_t size_;
    uint32_t alignment_;
    Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation_;
  };

}  // namespace oxygen::renderer::d3d12
