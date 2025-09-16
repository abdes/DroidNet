//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Detail/DescriptorSegment.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics::detail {

class FixedDescriptorSegment : public DescriptorSegment {
public:
  OXGN_GFX_API FixedDescriptorSegment(bindless::Capacity capacity,
    bindless::HeapIndex base_index, ResourceViewType view_type,
    DescriptorVisibility visibility);

  OXGN_GFX_API ~FixedDescriptorSegment() noexcept override;

  OXYGEN_MAKE_NON_COPYABLE(FixedDescriptorSegment)
  OXYGEN_DEFAULT_MOVABLE(FixedDescriptorSegment)
  //! Allocates a descriptor index from this segment.
  /*!
   \return The allocated index, or DescriptorHandle::kInvalidIndex if the
           segment is full, or an error occurs. Errors are logged but not
           propagated.
  */
  OXGN_GFX_NDAPI auto Allocate() noexcept -> bindless::HeapIndex override;

  //! Releases a descriptor index back to this segment.
  /*!
   \param index The index to release. Must be within the segment's range.
   \return True if the index was successfully released, false otherwise.

   Validates that the index belongs to this segment before releasing it, then
   adds the released index to the free list for future reuse.
   Ensures the same descriptor cannot be released twice.
  */
  OXGN_GFX_API auto Release(bindless::HeapIndex index) noexcept
    -> bool override;

  //! Returns the number of descriptors currently available in this segment.
  OXGN_GFX_NDAPI auto GetAvailableCount() const noexcept
    -> bindless::Count override;

  //! Returns the current size (number of allocated descriptors) of this
  //! segment.
  OXGN_GFX_NDAPI auto GetAllocatedCount() const noexcept
    -> bindless::Count override;

  // OXGN_GFX_NDAPI auto GetShaderVisibleIndex(
  //   const DescriptorHandle& handle) const noexcept -> bindless::HeapIndex
  //   override;

  [[nodiscard]] auto GetCapacity() const noexcept -> bindless::Capacity override
  {
    return capacity_;
  }
  [[nodiscard]] auto GetBaseIndex() const noexcept
    -> bindless::HeapIndex override
  {
    return base_index_;
  }
  [[nodiscard]] auto GetViewType() const noexcept -> ResourceViewType override
  {
    return view_type_;
  }
  [[nodiscard]] auto GetVisibility() const noexcept
    -> DescriptorVisibility override
  {
    return visibility_;
  }

protected:
  //! Releases all descriptors in this segment.
  /*!
   Releases all allocated descriptors, resetting the segment to its initial
   state. Use with caution, as this will make all allocated indices, in use
   anywhere, invalid.
   */
  OXGN_GFX_API auto ReleaseAll() -> void;

private:
  [[nodiscard]] auto FreeListSize() const -> bindless::Count;
  [[nodiscard]] auto ToLocalIndex(
    bindless::HeapIndex global_index) const noexcept -> bindless::HeapIndex;
  [[nodiscard]] auto IsAllocated(bindless::HeapIndex local_index) const noexcept
    -> bool;

  bindless::Capacity capacity_;
  ResourceViewType view_type_;
  DescriptorVisibility visibility_;

  bindless::HeapIndex base_index_;
  bindless::HeapIndex next_index_;
  std::vector<bool> released_flags_;
  std::vector<bindless::HeapIndex> free_list_ {};
};

} // namespace oxygen::graphics::detail
