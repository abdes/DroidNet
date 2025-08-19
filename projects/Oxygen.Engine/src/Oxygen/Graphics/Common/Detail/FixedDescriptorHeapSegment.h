//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Detail/DescriptorHeapSegment.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics::detail {

class FixedDescriptorHeapSegment : public DescriptorHeapSegment {
public:
  OXYGEN_GFX_API FixedDescriptorHeapSegment(bindless::Capacity capacity,
    bindless::Handle base_index, ResourceViewType view_type,
    DescriptorVisibility visibility);

  OXYGEN_GFX_API ~FixedDescriptorHeapSegment() noexcept override;

  OXYGEN_MAKE_NON_COPYABLE(FixedDescriptorHeapSegment)
  OXYGEN_DEFAULT_MOVABLE(FixedDescriptorHeapSegment)
  //! Allocates a descriptor index from this segment.
  /*!
   \return The allocated index, or DescriptorHandle::kInvalidIndex if the
           segment is full, or an error occurs. Errors are logged but not
           propagated.
  */
  [[nodiscard]] OXYGEN_GFX_API auto Allocate() noexcept
    -> bindless::Handle override;

  //! Releases a descriptor index back to this segment.
  /*!
   \param index The index to release. Must be within the segment's range.
   \return True if the index was successfully released, false otherwise.

   Validates that the index belongs to this segment before releasing it, then
   adds the released index to the free list for future reuse.
   Ensures the same descriptor cannot be released twice.
  */
  OXYGEN_GFX_API auto Release(bindless::Handle index) noexcept -> bool override;

  //! Returns the number of descriptors currently available in this segment.
  [[nodiscard]] OXYGEN_GFX_API auto GetAvailableCount() const noexcept
    -> bindless::Count override;

  //! Returns the current size (number of allocated descriptors) of this
  //! segment.
  [[nodiscard]] OXYGEN_GFX_API auto GetAllocatedCount() const noexcept
    -> bindless::Count override;

  [[nodiscard]] OXYGEN_GFX_API auto GetShaderVisibleIndex(
    const DescriptorHandle& handle) const noexcept -> bindless::Handle override;

  [[nodiscard]] auto GetCapacity() const noexcept -> bindless::Capacity override
  {
    return capacity_;
  }
  [[nodiscard]] auto GetBaseIndex() const noexcept -> bindless::Handle override
  {
    return base_index_;
  }
  [[nodiscard]] auto GetViewType() const noexcept -> ResourceViewType override
  {
    return view_type_;
  }
  [[nodiscard]] auto GetVisibility() const noexcept { return visibility_; }

protected:
  //! Releases all descriptors in this segment.
  /*!
   Releases all allocated descriptors, resetting the segment to its initial
   state. Use with caution, as this will make all allocated indices, in use
   anywhere, invalid.
   */
  OXYGEN_GFX_API void ReleaseAll();

private:
  [[nodiscard]] auto FreeListSize() const -> bindless::Count;
  [[nodiscard]] auto ToLocalIndex(bindless::Handle global_index) const noexcept
    -> bindless::Handle;
  [[nodiscard]] auto IsAllocated(bindless::Handle local_index) const noexcept
    -> bool;

  bindless::Capacity capacity_;
  ResourceViewType view_type_;
  DescriptorVisibility visibility_;

  bindless::Handle base_index_;
  bindless::Handle next_index_;
  std::vector<bool> released_flags_;
  std::vector<bindless::Handle> free_list_ {};
};

} // namespace oxygen::graphics::detail
