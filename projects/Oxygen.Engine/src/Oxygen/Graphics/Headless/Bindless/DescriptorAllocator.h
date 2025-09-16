//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>
#include <Oxygen/Graphics/Headless/api_export.h>

namespace oxygen::graphics::headless::bindless {

class DescriptorAllocator final : public detail::BaseDescriptorAllocator {
public:
  OXGN_HDLS_API explicit DescriptorAllocator(
    std::shared_ptr<const DescriptorAllocationStrategy> strategy = nullptr);

  OXYGEN_MAKE_NON_COPYABLE(DescriptorAllocator)
  OXYGEN_MAKE_NON_MOVABLE(DescriptorAllocator)

  OXGN_HDLS_API ~DescriptorAllocator() override = default;

  OXGN_HDLS_NDAPI auto GetShaderVisibleIndex(
    const DescriptorHandle& handle) const noexcept
    -> oxygen::bindless::ShaderVisibleIndex override;

protected:
  auto CreateHeapSegment(oxygen::bindless::Capacity capacity,
    oxygen::bindless::HeapIndex base_index, ResourceViewType view_type,
    DescriptorVisibility visibility)
    -> std::unique_ptr<detail::DescriptorSegment> override;

  // Copy a descriptor from one handle to another. Headless backend has no
  // native descriptor objects, so this is a validation-only no-op.
  auto CopyDescriptor(const DescriptorHandle& source,
    const DescriptorHandle& destination) -> void override;
};

} // namespace oxygen::graphics::headless::bindless
