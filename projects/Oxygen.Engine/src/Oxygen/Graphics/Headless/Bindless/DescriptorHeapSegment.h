//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <mutex>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/BindlessHandle.h>
#include <Oxygen/Graphics/Common/Detail/DescriptorHeapSegment.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

namespace oxygen::graphics::headless::bindless {

class DescriptorHeapSegment final : public detail::DescriptorHeapSegment {
public:
  DescriptorHeapSegment(oxygen::bindless::Capacity capacity,
    oxygen::bindless::Handle base_index, ResourceViewType view_type,
    DescriptorVisibility visibility);

  OXYGEN_MAKE_NON_COPYABLE(DescriptorHeapSegment)
  OXYGEN_MAKE_NON_MOVABLE(DescriptorHeapSegment)

  ~DescriptorHeapSegment() override = default;

  auto Allocate() noexcept -> oxygen::bindless::Handle override;
  auto Release(oxygen::bindless::Handle index) noexcept -> bool override;
  [[nodiscard]] auto GetAvailableCount() const noexcept
    -> oxygen::bindless::Count override;
  [[nodiscard]] auto GetViewType() const noexcept -> ResourceViewType override;
  [[nodiscard]] auto GetVisibility() const noexcept
    -> DescriptorVisibility override;
  [[nodiscard]] auto GetBaseIndex() const noexcept
    -> oxygen::bindless::Handle override;
  [[nodiscard]] auto GetCapacity() const noexcept
    -> oxygen::bindless::Capacity override;
  [[nodiscard]] auto GetAllocatedCount() const noexcept
    -> oxygen::bindless::Count override;
  [[nodiscard]] auto GetShaderVisibleIndex(
    const DescriptorHandle& handle) const noexcept
    -> oxygen::bindless::Handle override;

private:
  oxygen::bindless::Handle base_index_;
  oxygen::bindless::Capacity capacity_;
  ResourceViewType view_type_;
  DescriptorVisibility visibility_;

  // allocation bitmap and freelist
  std::vector<uint8_t> allocation_bitmap_;
  std::vector<oxygen::bindless::Handle> free_list_;
  uint32_t allocated_count_ = 0;
  uint32_t bump_cursor_ = 0; // local index

  mutable std::mutex mutex_;
};

} // namespace oxygen::graphics::headless::bindless
