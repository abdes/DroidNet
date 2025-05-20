//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

// ReSharper disable once CppUnusedIncludeDirective - OXYGEN_UNREACHABLE_RETURN
#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

namespace oxygen::graphics::detail {

//! Interface for descriptor heap segments.
/*!
 Defines the common interface for managing a dedicated section, or "segment,"
 within a larger descriptor heap. Each segment is responsible for a contiguous
 range of descriptor handles, all intended for a specific `ResourceViewType` and
 `DescriptorVisibility`.

 Implementations of this interface are expected to provide robust mechanisms for
 allocating and releasing descriptor indices within their managed range. The
 core responsibilities and expected behaviors include:

 - **Lifecycle Management**: Upon allocation, a segment provides a unique
   descriptor index. This index remains "owned" or "in-use" until it is
   explicitly released. Once released, an index should become available for
   subsequent allocations, promoting descriptor reuse.

 - **Boundary Adherence**: Allocations must only return indices within the
   segment's defined range: [`GetBaseIndex()`, `GetBaseIndex() + GetCapacity() -
   1`]. Attempts to release an index outside this range must fail.

 - **State Integrity**:
    - `Allocate()`: If no descriptors are available (i.e., the segment is full),
      it must return a sentinel value (`DescriptorHandle::kInvalidIndex`).
    - `Release(index)`: Must return `true` if the given `index` was valid
      (within segment bounds, currently allocated) and successfully made
      available. It must return `false` if the index is out of bounds, was not
      currently allocated (e.g., already free or never allocated by this segment
      instance), or if the release otherwise fails. Releasing the same index
      multiple times without an intervening allocation must fail on subsequent
      attempts.

 - **Consistent Properties**: The values returned by `GetViewType()`,
   `GetVisibility()`, `GetBaseIndex()`, and `GetCapacity()` must remain constant
   throughout the lifetime of the segment instance after its construction.

 - **Accurate Counts**:
    - `GetSize()`: Must accurately reflect the number of currently allocated
      (in-use) descriptors.
    - `GetAvailableCount()`: Must accurately reflect how many more descriptors
      can be allocated. This is typically `GetCapacity() - GetSize()`.

 While the specific strategy for recycling descriptors (e.g., LIFO, FIFO) can
 vary between implementations, the fundamental ability to reuse released
 descriptors is a key expectation.
*/
class DescriptorHeapSegment {
public:
    //! Alias the descriptor handle index type for convenience.
    using IndexT = DescriptorHandle::IndexT;

    DescriptorHeapSegment() noexcept = default;
    virtual ~DescriptorHeapSegment() noexcept = default;

    OXYGEN_MAKE_NON_COPYABLE(DescriptorHeapSegment)
    OXYGEN_DEFAULT_MOVABLE(DescriptorHeapSegment)

    //! Allocates a descriptor index from this segment.
    /*!
     \return The allocated index, or std::numeric_limits<uint32_t>::max() if the segment is full.
    */
    [[nodiscard]] virtual auto Allocate() noexcept -> IndexT = 0;

    //! Releases a descriptor index back to this segment.
    /*!
     \param index The global index to release.
     \return True if the index was successfully released, false otherwise.
    */
    virtual auto Release(IndexT index) noexcept -> bool = 0;

    //! Returns the number of descriptors currently available in this segment.
    [[nodiscard]] virtual auto GetAvailableCount() const noexcept -> IndexT = 0;

    //! Returns the resource view type of this segment.
    [[nodiscard]] virtual auto GetViewType() const noexcept -> ResourceViewType = 0;

    //! Returns the visibility of this segment.
    [[nodiscard]] virtual auto GetVisibility() const noexcept -> DescriptorVisibility = 0;

    //! Returns the base index of this segment.
    [[nodiscard]] virtual auto GetBaseIndex() const noexcept -> IndexT = 0;

    //! Returns the capacity of this segment.
    [[nodiscard]] virtual auto GetCapacity() const noexcept -> IndexT = 0;

    //! Returns the current size (number of allocated descriptors) of this segment.
    [[nodiscard]] virtual auto GetAllocatedCount() const noexcept -> IndexT = 0;

    //! Checks if the segment is empty (i.e., no allocated descriptors).
    [[nodiscard]] auto IsEmpty() const noexcept { return GetAllocatedCount() == 0; }

    //! Checks if the segment is full (i.e., all capacity is used for allocated
    //! descriptors).
    [[nodiscard]] auto IsFull() const noexcept { return GetAllocatedCount() == GetCapacity(); }
};

} // namespace oxygen::graphics::detail
