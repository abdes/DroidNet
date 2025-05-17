//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <bitset>
#include <cstdint>

// ReSharper disable once CppUnusedIncludeDirective - OXYGEN_UNREACHABLE_RETURN
#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/StaticVector.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Detail/DescriptorHeapSegment.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

namespace oxygen::graphics::detail {

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
class StaticDescriptorHeapSegment : public DescriptorHeapSegment {
    static_assert(ViewType < ResourceViewType::kMaxResourceViewType && ViewType != ResourceViewType::kNone,
        "StaticDescriptorHeapSegment: ViewType must be a valid ResourceViewType (not kMax or greater)");

public:
    StaticDescriptorHeapSegment(
        const DescriptorVisibility visibility,
        const uint32_t base_index) noexcept
        : visibility_(visibility)
        , base_index_(base_index)
        , next_index_(0)
    {
    }

    //! Destructor.
    /*!
     This destructor does not own any resources that require explicit cleanup.
     However, it is not a good practice to destroy a segment while it still has
     allocated descriptors. When this class is extended, the derived class
     should ensure that all descriptors are released before destruction.
    */
    ~StaticDescriptorHeapSegment() noexcept override
    {
        try {
            // Do not call the virtual method GetSize() in the destructor.
            if (const auto size = next_index_ - static_cast<uint32_t>(free_list_.size()); size > 0U) {
                LOG_F(WARNING, "Destroying segment with allocated descriptors ({})", size);
            }
        } catch (...) {
            // Nothing to do, but adding a placeholder statement to avoid warnings.
            [[maybe_unused]] auto _ = 0;
        }
    }

    OXYGEN_MAKE_NON_COPYABLE(StaticDescriptorHeapSegment)
    OXYGEN_DEFAULT_MOVABLE(StaticDescriptorHeapSegment)

    //! Allocates a descriptor index from this segment.
    /*!
     \return The allocated index, or DescriptorHandle::kInvalidIndex if the
             segment is full, or an error occurs. Errors are logged but not
             propagated.
    */
    [[nodiscard]] auto Allocate() noexcept -> uint32_t override
    {
        // First try to reuse a released descriptor (LIFO for better cache locality)
        if (!free_list_.empty()) {
            uint32_t local_index = free_list_.back();
            free_list_.pop_back();
            released_flags_.reset(local_index);
            DLOG_F(2, "Recycled descriptor index {} (remaining: {}/{})",
                local_index, GetAvailableCount(), GetCapacity());
            return base_index_ + local_index;
        }

        // If no freed descriptors, allocate a new one
        if (next_index_ < GetCapacity()) {
            DLOG_F(2, "Allocated new descriptor index {} (remaining: {}/{})",
                next_index_, GetAvailableCount(), GetCapacity());
            return base_index_ + next_index_++;
        }

        return DescriptorHandle::kInvalidIndex;
    }

    //! Releases a descriptor index back to this segment.
    /*!
     \param index The index to release. Must be within the segment's range.
     \return True if the index was successfully released, false otherwise.

     Validates that the index belongs to this segment before releasing it, then
     adds the released index to the free list for future reuse.
     Ensures the same descriptor cannot be released twice.
    */
    auto Release(const uint32_t index) noexcept -> bool override
    {
        // Check if the index belongs to this segment
        if (index < base_index_ || index >= base_index_ + GetCapacity()) {
            return false;
        }

        // Convert to local index
        uint32_t local_index = index - base_index_;

        // Check if this index was never allocated or is beyond the currently allocated range.
        // An index can only be released if it's < next_index_ (meaning it was allocated),
        // AND it's not already in the free_list_ (checked by released_flags_).
        if (local_index >= next_index_ && !released_flags_.test(local_index)) {
            return false;
        }

        // Check if this index is already released
        if (released_flags_.test(local_index)) {
            return false; // Already released
        }

        // Add to the free list
        try {
            free_list_.emplace_back(local_index);
        } catch (const std::exception& ex) {
            // The only reason this would fail is due to memory allocation failure.
            LOG_F(ERROR, "Failed to add released index {} to free list: {}", local_index, ex.what());
            return false;
        }
        // Mark as released
        released_flags_.set(local_index);

        DLOG_F(2, "Released descriptor index {} (remaining: {}/{})",
            local_index, GetAvailableCount(), GetCapacity());
        return true;
    }

    //! Returns the number of descriptors currently available in this segment.
    [[nodiscard]] auto GetAvailableCount() const noexcept -> uint32_t override
    {
        return GetCapacity() - next_index_ + static_cast<uint32_t>(free_list_.size());
    }

    //! Returns the resource view type of this segment.
    [[nodiscard]] auto GetViewType() const noexcept -> ResourceViewType override
    {
        return ViewType;
    }

    //! Returns the visibility of this segment.
    [[nodiscard]] auto GetVisibility() const noexcept -> DescriptorVisibility override
    {
        return visibility_;
    }

    //! Returns the base index of this segment.
    [[nodiscard]] auto GetBaseIndex() const noexcept -> uint32_t override
    {
        return base_index_;
    }

    //! Returns the capacity of this segment.
    [[nodiscard]] constexpr auto GetCapacity() const noexcept -> uint32_t override
    {
        return GetOptimalCapacity();
    }

    //! Returns the current size (number of allocated descriptors) of this segment.
    [[nodiscard]] auto GetAllocatedCount() const noexcept -> uint32_t override
    {
        return next_index_ - static_cast<uint32_t>(free_list_.size());
    }

protected:
    //! Releases all descriptors in this segment.
    /*!
     Releases all allocated descriptors, resetting the segment to its initial
     state. Use with caution, as this will make all allocated indices, in use
     anywhere, invalid.
     */
    void ReleaseAll()
    {
        // Clear the free list and reset the released flags
        free_list_.clear();
        released_flags_.reset();
        // Reset the next index to the base index
        next_index_ = 0;
    }

private:
    //! Returns the optimal capacity for this specific resource view type.
    /*!
     Different resource types benefit from different segment sizes based on
     typical usage patterns and hardware considerations. The intent is to
     minimize the number of segments used by an allocator while also minimizing
     the wasted space in the segments.
    */
    [[nodiscard]] static constexpr auto GetOptimalCapacity() noexcept -> uint32_t
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
        case ResourceViewType::kMaxResourceViewType:
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
