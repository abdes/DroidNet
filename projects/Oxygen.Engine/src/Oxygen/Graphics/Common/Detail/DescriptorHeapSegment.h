//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <bitset>
#include <cstdint>
#include <limits>

// ReSharper disable once CppUnusedIncludeDirective - OXYGEN_UNREACHABLE_RETURN
#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/StaticVector.h>
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
      it must return a sentinel value (typically
      `std::numeric_limits<uint32_t>::max()`).
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
    DescriptorHeapSegment() = default;
    virtual ~DescriptorHeapSegment() = default;

    OXYGEN_MAKE_NON_COPYABLE(DescriptorHeapSegment)
    OXYGEN_DEFAULT_MOVABLE(DescriptorHeapSegment)

    //! Allocates a descriptor index from this segment.
    /*!
     \return The allocated index, or std::numeric_limits<uint32_t>::max() if the segment is full.
    */
    [[nodiscard]] virtual auto Allocate() -> uint32_t = 0;

    //! Releases a descriptor index back to this segment.
    /*!
     \param index The global index to release.
     \return True if the index was successfully released, false otherwise.
    */
    virtual auto Release(uint32_t index) -> bool = 0;

    //! Returns the number of descriptors currently available in this segment.
    [[nodiscard]] virtual auto GetAvailableCount() const -> uint32_t = 0;

    //! Returns the resource view type of this segment.
    [[nodiscard]] virtual auto GetViewType() const -> ResourceViewType = 0;

    //! Returns the visibility of this segment.
    [[nodiscard]] virtual auto GetVisibility() const -> DescriptorVisibility = 0;

    //! Returns the base index of this segment.
    [[nodiscard]] virtual auto GetBaseIndex() const -> uint32_t = 0;

    //! Returns the capacity of this segment.
    [[nodiscard]] virtual auto GetCapacity() const -> uint32_t = 0;

    //! Returns the current size (number of allocated descriptors) of this segment.
    [[nodiscard]] virtual auto GetSize() const -> uint32_t = 0;

    //! Checks if the segment is empty (i.e. no allocated descriptors).
    [[nodiscard]] auto IsEmpty() const { return GetSize() == 0; }

    //! Checks if the segment is full (i.e. all capacity is used for allocated
    //! descriptors).
    [[nodiscard]] auto IsFull() const { return GetSize() == GetCapacity(); }
};

//! Specialized implementation of a descriptor heap segment with compile-time
//! optimized storage.
/*!
 Represents a range of descriptors of a specific type and visibility within a
 descriptor heap. Manages allocation state and recycling of descriptors.

 The segment tracks which descriptor indices are allocated or free, allowing for
 efficient reuse of released descriptors. It's designed to work with
 implementations that use fixed-size heaps as well as heaps that can grow.

 This particular implementation, `StaticDescriptorHeapSegment`, offers a
 compile-time optimized approach to managing descriptors. Its capacity for each
 `ResourceViewType` is determined at compile time via the `GetOptimalCapacity()`
 static method. This allows for internal storage, such as the `released_flags_`
 bitset and the `free_list_` static vector, to be sized precisely, potentially
 offering performance benefits by avoiding dynamic allocations for its own
 management structures.

 Key specialized behaviors and characteristics of `StaticDescriptorHeapSegment`
 include:

 - **Compile-Time Capacity**: The maximum number of descriptors this segment can
   hold is fixed at compile time based on the `ViewType`. This is in contrast to
   a dynamically sized segment whose capacity might be determined at runtime.

 - **LIFO Recycling**: When `Allocate()` is called, it first attempts to reuse a
   descriptor from its `free_list_`. This list is managed in a Last-In,
   First-Out (LIFO) manner. This means the most recently released descriptor
   will be the next one to be reallocated. This strategy can be beneficial for
   cache locality, as recently used data might still be warm in the cache.

 - **Sequential Allocation Fallback**: If the `free_list_` is empty,
   `Allocate()` will then attempt to provide a new descriptor by incrementing an
   internal `next_index_` counter. Allocations proceed sequentially from
   `base_index_` up to `base_index_ + GetOptimalCapacity() - 1` until all
   descriptors are exhausted.

 - **Internal State Tracking**: It uses a `std::bitset` (`released_flags_`) to
   keep track of which specific local indices within its range have been
   released and are currently in the `free_list_`. This allows for efficient
   checking to prevent double-releasing an already free descriptor.

 - **Statically Defined Capacity**: Once created, the segment cannot change its
   capacity. It manages a fixed-size block of descriptors as defined by
   `GetOptimalCapacity()`.

 These characteristics make `StaticDescriptorHeapSegment` suitable for scenarios
 where descriptor usage patterns for a given type are predictable and where the
 overhead of dynamic memory management for the segment itself is undesirable.

 \tparam ViewType The resource view type this segment manages
*/
template <ResourceViewType ViewType>
class StaticDescriptorHeapSegment final : public DescriptorHeapSegment {
    static_assert(ViewType < ResourceViewType::kMax && ViewType != ResourceViewType::kNone,
        "StaticDescriptorHeapSegment: ViewType must be a valid ResourceViewType (not kMax or greater)");

public:
    StaticDescriptorHeapSegment(
        const DescriptorVisibility visibility,
        const uint32_t base_index)
        : visibility_(visibility)
        , base_index_(base_index)
        , next_index_(0)
    {
    }

    ~StaticDescriptorHeapSegment() override = default;

    OXYGEN_MAKE_NON_COPYABLE(StaticDescriptorHeapSegment)
    OXYGEN_DEFAULT_MOVABLE(StaticDescriptorHeapSegment)

    //! Allocates a descriptor index from this segment.
    /*!
     \return The allocated index, or std::numeric_limits<uint32_t>::max() if the segment is full.

     First tries to reuse a previously freed index. If none are available,
     allocates the next available index. Returns std::numeric_limits<uint32_t>::max() if the segment is full.
    */
    [[nodiscard]] auto Allocate() -> uint32_t override
    {
        // First try to reuse a released descriptor (LIFO for better cache locality)
        if (!free_list_.empty()) {
            uint32_t local_index = free_list_.back();
            free_list_.pop_back();
            released_flags_.reset(local_index);
            return base_index_ + local_index;
        }

        // If no freed descriptors, allocate a new one
        if (next_index_ < GetCapacity()) {
            return base_index_ + next_index_++;
        }

        // Segment is full
        return std::numeric_limits<uint32_t>::max();
    }

    //! Releases a descriptor index back to this segment.
    /*!
     \param index The global index to release.
     \return True if the index was successfully released, false otherwise.

     Validates that the index belongs to this segment before releasing it.
     Adds the released index to the free list for future reuse.
     Ensures the same descriptor cannot be released twice.
    */
    auto Release(const uint32_t index) -> bool override
    {
        // Check if the index belongs to this segment
        if (index < base_index_ || index >= base_index_ + GetCapacity()) {
            return false;
        }

        // Convert to local index
        uint32_t local_index = index - base_index_;

        // Check if this index was never allocated or is beyond the currently allocated range
        // An index can only be released if it's < next_index_ (meaning it was allocated)
        // AND it's not already in the free_list_ (checked by released_flags_).
        if (local_index >= next_index_ && !released_flags_.test(local_index)) {
            return false;
        }

        // Check if this index is already released
        if (released_flags_.test(local_index)) {
            return false; // Already released
        }

        // Mark as released and add to free list
        released_flags_.set(local_index);
        free_list_.push_back(local_index);

        return true;
    }

    //! Returns the number of descriptors currently available in this segment.
    [[nodiscard]] auto GetAvailableCount() const -> uint32_t override
    {
        return GetCapacity() - next_index_ + static_cast<uint32_t>(free_list_.size());
    }

    //! Returns the resource view type of this segment.
    [[nodiscard]] auto GetViewType() const -> ResourceViewType override
    {
        return ViewType;
    }

    //! Returns the visibility of this segment.
    [[nodiscard]] auto GetVisibility() const -> DescriptorVisibility override
    {
        return visibility_;
    }

    //! Returns the base index of this segment.
    [[nodiscard]] auto GetBaseIndex() const -> uint32_t override
    {
        return base_index_;
    }

    //! Returns the capacity of this segment.
    [[nodiscard]] constexpr auto GetCapacity() const -> uint32_t override
    {
        return GetOptimalCapacity();
    }

    //! Returns the current size (number of allocated descriptors) of this segment.
    [[nodiscard]] auto GetSize() const -> uint32_t override
    {
        return next_index_ - static_cast<uint32_t>(free_list_.size());
    }

private:
    //! Returns the optimal capacity for this specific resource view type.
    /*!
     Different resource types benefit from different segment sizes based on
     typical usage patterns and hardware considerations. The intent is to
     minimize the number of segments used by an allocator while also minimizing
     the wasted space in the segments.
    */
    [[nodiscard]] static constexpr auto GetOptimalCapacity() -> uint32_t
    {
        switch (ViewType) {
        case ResourceViewType::kConstantBuffer:
            return 64; // CBVs are typically used in smaller groups

        case ResourceViewType::kTexture_SRV:
            return 256; // Texture SRVs have high/medium frequency of use

        case ResourceViewType::kTypedBuffer_SRV: // NOLINT(bugprone-branch-clone)
        case ResourceViewType::kStructuredBuffer_SRV:
        case ResourceViewType::kRawBuffer_SRV:
            return 64; // Buffer SRVs used in smaller groups

        case ResourceViewType::kTexture_UAV:
        case ResourceViewType::kTypedBuffer_UAV:
        case ResourceViewType::kStructuredBuffer_UAV:
        case ResourceViewType::kRawBuffer_UAV:
        case ResourceViewType::kSamplerFeedbackTexture_UAV:
            return 64; // UAVs typically used in smaller groups

        case ResourceViewType::kSampler:
            return 32; // Samplers are reused frequently

        case ResourceViewType::kTexture_RTV:
        case ResourceViewType::kTexture_DSV:
        case ResourceViewType::kRayTracingAccelStructure:
            return 16; // RT/DS/RT-AS views are used in small numbers

        case ResourceViewType::kNone:
        case ResourceViewType::kMax:
        default: // NOLINT(clang-diagnostic-covered-switch-default)
            OXYGEN_UNREACHABLE_RETURN(128);
        }
    }

    DescriptorVisibility visibility_;
    uint32_t base_index_;
    uint32_t next_index_;
    std::bitset<GetOptimalCapacity()> released_flags_ {};
    StaticVector<uint32_t, GetOptimalCapacity()> free_list_ {};
};

} // namespace oxygen::graphics::detail
