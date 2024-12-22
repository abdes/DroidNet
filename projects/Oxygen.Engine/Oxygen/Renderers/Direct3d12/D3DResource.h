//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include "oxygen/base/macros.h"
#include "Oxygen/Renderers/Common/Types.h"
#include "oxygen/Renderers/Direct3d12/D3DPtr.h"

namespace D3D12MA {
  class Allocation;
}

namespace oxygen::renderer::d3d12 {

  class D3DResource
  {
  public:
    virtual ~D3DResource() = default;

    OXYGEN_DEFAULT_COPYABLE(D3DResource);
    OXYGEN_DEFAULT_MOVABLE(D3DResource);

    [[nodiscard]] virtual ID3D12Resource* GetD3DResource() const
    {
      return resource_.get();
    }

    [[nodiscard]] virtual D3D12MA::Allocation* GetAllocation() const
    {
      return allocation_.get();
    }

    [[nodiscard]] virtual MemoryBlockPtr GetMemoryBlock() const
    {
      return memory_block_;
    }

  protected:
    explicit D3DResource() = default;

    D3DPtr<ID3D12Resource> resource_;
    MemoryBlockPtr memory_block_;
    D3DPtr<D3D12MA::Allocation> allocation_;
  };

}  // namespace oxygen::renderer::d3d12
