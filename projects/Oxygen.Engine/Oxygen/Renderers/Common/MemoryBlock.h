//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Types.h"

namespace oxygen::renderer {
  //! Description of a GPU memory block.
  struct MemoryBlockDesc
  {
    uint64_t size;
    uint32_t alignment;

    explicit MemoryBlockDesc(const uint64_t size = 0u)
      : size(size)
      , alignment(0u)
    {
    }
  };

  //! GPU memory block.
  class IMemoryBlock
  {
  public:
    IMemoryBlock() = default;
    virtual ~IMemoryBlock() = default;

    OXYGEN_MAKE_NON_COPYABLE(IMemoryBlock);
    OXYGEN_MAKE_NON_MOVEABLE(IMemoryBlock);
  };

}  // namespace oxygen::renderer