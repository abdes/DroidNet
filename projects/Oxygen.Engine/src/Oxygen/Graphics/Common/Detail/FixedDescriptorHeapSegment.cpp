//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/Detail/FixedDescriptorHeapSegment.h>

using oxygen::graphics::detail::FixedDescriptorHeapSegment;

FixedDescriptorHeapSegment::FixedDescriptorHeapSegment(
    IndexT capacity, IndexT base_index,
    ResourceViewType view_type,
    DescriptorVisibility visibility)
    : capacity_(capacity)
    , view_type_(view_type)
    , visibility_(visibility)
    , base_index_(base_index)
    , next_index_(0)
    , released_flags_(capacity, false)
{
    DLOG_F(1, "Descriptor heap segment created ({} / {}, base index: {}, capacity: {})",
        nostd::to_string(view_type_), nostd::to_string(visibility_),
        base_index_, GetCapacity());
}

inline auto FixedDescriptorHeapSegment::FreeListSize() const -> IndexT
{
    size_t free_count = free_list_.size();
    DCHECK_LE_F(free_count, std::numeric_limits<IndexT>::max(),
        "unexpected size of free list ({}), larger than what IndexT can hold", free_count);
    return static_cast<IndexT>(free_count);
}

FixedDescriptorHeapSegment::~FixedDescriptorHeapSegment() noexcept
{
    try {
        // Do not call the virtual method GetSize() in the destructor.
        if (const auto size = next_index_ - FreeListSize(); size > 0U) {
            LOG_F(WARNING, "Destroying segment with allocated descriptors ({})", size);
        }
    } catch (...) {
        // Nothing to do, but adding a placeholder statement to avoid warnings.
        [[maybe_unused]] auto _ = 0;
    }

    DLOG_F(1, "Descriptor heap segment destroyed ({} / {}, base index: {}, capacity: {})",
        nostd::to_string(view_type_), nostd::to_string(visibility_),
        base_index_, GetCapacity());
}

auto FixedDescriptorHeapSegment::Allocate() noexcept -> uint32_t
{
    // First try to reuse a released descriptor (LIFO for better cache locality)
    if (!free_list_.empty()) {
        auto local_index = free_list_.back();
        free_list_.pop_back();
        released_flags_[local_index] = false;
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

auto FixedDescriptorHeapSegment::Release(const IndexT index) noexcept -> bool
{
    // Check if the index belongs to this segment
    if (index < base_index_ || index >= base_index_ + GetCapacity()) {
        return false;
    }

    // Convert to local index
    auto local_index = index - base_index_;
    DCHECK_GE_F(local_index, 0U); // local_index should never be negative

    // Check if this index was never allocated or is beyond the currently allocated range.
    // An index can only be released if it's < next_index_ (meaning it was allocated),
    // AND it's not already in the free_list_ (checked by released_flags_).
    if (local_index >= next_index_ || released_flags_[local_index]) {
        return false;
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
    released_flags_[local_index] = true;

    DLOG_F(2, "Released descriptor index {} (remaining: {}/{})",
        local_index, GetAvailableCount(), GetCapacity());
    return true;
}

auto FixedDescriptorHeapSegment::GetAvailableCount() const noexcept -> IndexT
{
    return GetCapacity() - next_index_ + FreeListSize();
}

auto FixedDescriptorHeapSegment::GetAllocatedCount() const noexcept -> IndexT
{
    return next_index_ - FreeListSize();
}

void FixedDescriptorHeapSegment::ReleaseAll()
{
    // Clear the free list and reset the released flags
    free_list_.clear();
    released_flags_.assign(capacity_, false);
    // Reset the next index to the base index
    next_index_ = 0;
}
