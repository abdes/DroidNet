//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Graphics/Common/DescriptorAllocationHandle.h>

namespace oxygen::graphics {
class DescriptorAllocator;
} // namespace oxygen::graphics

namespace oxygen::graphics::bindless::testing {

class TestDescriptorHandle : public DescriptorAllocationHandle {
public:
  TestDescriptorHandle() = default;

  //! Exposes the constructor for testing purposes.
  TestDescriptorHandle(DescriptorAllocator* allocator,
    const oxygen::bindless::HeapIndex index, const ResourceViewType view_type,
    const DescriptorVisibility visibility)
    : DescriptorAllocationHandle(
        allocator, index, view_type, visibility, DescriptorAllocationKind::kRaw)
  {
  }
};

} // namespace oxygen::graphics::bindless::testing
