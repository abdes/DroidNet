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
    OXYGEN_GFX_API FixedDescriptorHeapSegment(
        IndexT capacity, IndexT base_index,
        ResourceViewType view_type,
        DescriptorVisibility visibility);

    OXYGEN_GFX_API virtual ~FixedDescriptorHeapSegment() noexcept override;

    OXYGEN_MAKE_NON_COPYABLE(FixedDescriptorHeapSegment)
    OXYGEN_DEFAULT_MOVABLE(FixedDescriptorHeapSegment)

    //! Allocates a descriptor index from this segment.
    /*!
     \return The allocated index, or DescriptorHandle::kInvalidIndex if the
             segment is full, or an error occurs. Errors are logged but not
             propagated.
    */
    [[nodiscard]] OXYGEN_GFX_API auto Allocate() noexcept -> IndexT override;

    //! Releases a descriptor index back to this segment.
    /*!
     \param index The index to release. Must be within the segment's range.
     \return True if the index was successfully released, false otherwise.

     Validates that the index belongs to this segment before releasing it, then
     adds the released index to the free list for future reuse.
     Ensures the same descriptor cannot be released twice.
    */
    OXYGEN_GFX_API auto Release(const IndexT index) noexcept -> bool override;

    //! Returns the number of descriptors currently available in this segment.
    [[nodiscard]] OXYGEN_GFX_API auto GetAvailableCount() const noexcept -> IndexT override;

    //! Returns the current size (number of allocated descriptors) of this segment.
    [[nodiscard]] OXYGEN_GFX_API auto GetAllocatedCount() const noexcept -> IndexT override;

    [[nodiscard]] auto GetCapacity() const noexcept -> IndexT override { return capacity_; }
    [[nodiscard]] auto GetBaseIndex() const noexcept -> IndexT override { return base_index_; }
    [[nodiscard]] auto GetViewType() const noexcept -> ResourceViewType override { return view_type_; }
    [[nodiscard]] auto GetVisibility() const noexcept -> DescriptorVisibility override { return visibility_; }

protected:
    //! Releases all descriptors in this segment.
    /*!
     Releases all allocated descriptors, resetting the segment to its initial
     state. Use with caution, as this will make all allocated indices, in use
     anywhere, invalid.
     */
    OXYGEN_GFX_API void ReleaseAll();

private:
    auto FreeListSize() const -> IndexT;

    IndexT capacity_;
    ResourceViewType view_type_;
    DescriptorVisibility visibility_;

    IndexT base_index_;
    IndexT next_index_;
    std::vector<bool> released_flags_;
    std::vector<IndexT> free_list_ {};
};

} // namespace oxygen::graphics::detail
