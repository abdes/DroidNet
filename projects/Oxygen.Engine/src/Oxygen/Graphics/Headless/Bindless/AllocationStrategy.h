//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <unordered_map>

#include <Oxygen/Graphics/Common/DescriptorAllocator.h>

namespace oxygen::graphics::headless::bindless {

//! Headless default descriptor allocation strategy.
/*!
 Detailed implementation of DescriptorAllocationStrategy for the headless
 backend. It initializes generous per-view HeapDescription entries and
 computes contiguous, non-overlapping base indices for CPU-only and
 shader-visible heaps so that bindless indices are contiguous across view
 types.

 Determinism: base indices are computed using a stable insertion order
 recorded at construction to ensure repeatable layouts for tests.

 Visibility rules: RTV and DSV view types are treated as CPU-only; their
 shader-visible capacity is forced to zero so no shader-visible heap range
 is created for those types.

 ### Key Features

 - Generous default capacities tuned for headless / testing environments.
 - Deterministic base-index layout using a stable insertion order.
 - RTV/DSV are never shader-visible; shader capacity is forced to zero.

 ### Usage Patterns

 Use this strategy when constructing headless descriptor allocators to ensure
 a non-null, test-friendly allocation policy with stable, repeatable heap
 layouts.

 @note Heap key format: `<ViewType>:(cpu|gpu)` where the suffix indicates
 cpu-only or shader-visible (gpu) visibility.
 @note Deterministic layout is produced by using a stable insertion order
 recorded at construction.
 @note RTV/DSV shader-visible capacity is explicitly zeroed to avoid creating
 illogical shader-visible heaps in tests.
 @throw std::runtime_error if GetHeapDescription is given a malformed or
 unknown heap key.
 @see DescriptorAllocationStrategy
*/
class AllocationStrategy final : public DescriptorAllocationStrategy {
public:
  AllocationStrategy();
  ~AllocationStrategy() override = default;

  OXYGEN_DEFAULT_COPYABLE(AllocationStrategy)
  OXYGEN_DEFAULT_MOVABLE(AllocationStrategy)

  [[nodiscard]] auto GetHeapKey(ResourceViewType view_type,
    DescriptorVisibility visibility) const -> std::string override;

  [[nodiscard]] auto GetHeapDescription(const std::string& heap_key) const
    -> const HeapDescription& override;

  [[nodiscard]] auto GetHeapBaseIndex(ResourceViewType view_type,
    DescriptorVisibility visibility) const -> oxygen::bindless::Handle override;

private:
  // Headless-specific heap table; capacities are intentionally generous to
  // simulate an unlimited, software-only environment suitable for testing.
  std::unordered_map<std::string, HeapDescription> heaps_;
  std::unordered_map<std::string, oxygen::bindless::Handle> heap_base_indices_;
};

} // namespace oxygen::graphics::headless::bindless
