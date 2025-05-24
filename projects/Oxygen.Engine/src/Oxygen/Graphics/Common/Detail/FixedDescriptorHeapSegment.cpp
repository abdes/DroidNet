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
    DLOG_F(1, "constructed: heap segment ({} / {}, base index: {}, capacity: {})",
        nostd::to_string(view_type_), nostd::to_string(visibility_),
        base_index_, GetCapacity());
}

//! Converts a global descriptor index to a local index within the segment.
auto FixedDescriptorHeapSegment::ToLocalIndex(IndexT global_index) const noexcept -> IndexT
{
    auto local_index = global_index - base_index_;
    if (local_index < 0 || local_index >= GetCapacity()) {
        LOG_F(WARNING, "Descriptor handle, with index {}, is out of my range", global_index);
        return DescriptorHandle::kInvalidIndex;
    }
    return local_index;
}

//! Checks if a local index is currently allocated in the segment.
auto FixedDescriptorHeapSegment::IsAllocated(uint32_t local_index) const noexcept -> bool
{
    return local_index < next_index_ && !released_flags_[local_index];
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

    DLOG_F(1, "destroyed: heap segment ({} / {}, base index: {}, capacity: {})",
        nostd::to_string(view_type_), nostd::to_string(visibility_),
        base_index_, GetCapacity());
}

auto FixedDescriptorHeapSegment::Allocate() noexcept -> uint32_t
{
    LOG_SCOPE_F(2, "Allocate descriptor index");
    LOG_F(2, "segment ({} / {}, base index: {}, capacity: {})",
        nostd::to_string(view_type_), nostd::to_string(visibility_),
        base_index_, GetCapacity());

    auto global_index = DescriptorHandle::kInvalidIndex;

    // First try to reuse a released descriptor (LIFO for better cache locality)
    if (!free_list_.empty()) {
        auto local_index = free_list_.back();
        free_list_.pop_back();
        released_flags_[local_index] = false;
        DLOG_F(2, "recycled descriptor with local index {} (remaining: {}/{})",
            local_index, GetAvailableCount(), GetCapacity());
        global_index = base_index_ + local_index;
    } else if (next_index_ < GetCapacity()) {
        // If no freed descriptors, allocate a new one
        auto local_index = next_index_++;
        DLOG_F(2, "allocated new local index {} (remaining: {}/{})",
            local_index, GetAvailableCount(), GetCapacity());
        global_index = base_index_ + local_index;
    } else {
        // No more descriptors available
        DLOG_F(ERROR, "segment is full");
    }

    LOG_F(2, "{}returning global index {}",
        global_index == DescriptorHandle::kInvalidIndex
            ? "failed: "
            : "",
        global_index);

    return global_index;
}

auto FixedDescriptorHeapSegment::Release(const IndexT index) noexcept -> bool
{
    LOG_SCOPE_F(2, "Release descriptor index");
    DLOG_F(2, "segment ({} / {}, base index: {}, capacity: {})",
        nostd::to_string(view_type_), nostd::to_string(visibility_),
        base_index_, GetCapacity());

    // Convert to local index
    auto local_index = ToLocalIndex(index);
    if (local_index == DescriptorHandle::kInvalidIndex) {
        return false;
    }

    // Check if this index was never allocated or is beyond the currently allocated range.
    if (!IsAllocated(local_index)) {
        LOG_F(WARNING, "local index {} is already released", local_index);
        return false;
    }

    // Add to the free list
    try {
        free_list_.emplace_back(local_index);
    } catch (const std::exception& ex) {
        // The only reason this would fail is due to memory allocation failure.
        LOG_F(ERROR, "Failed to add released local index {} to free list: {}", local_index, ex.what());
        return false;
    }
    // Mark as released
    released_flags_[local_index] = true;

    LOG_F(2, "released: descriptor index (l:{}, g:{}) (remaining: {}/{})",
        local_index, index, GetAvailableCount(), GetCapacity());
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

auto FixedDescriptorHeapSegment::GetShaderVisibleIndex(const DescriptorHandle& handle) const noexcept -> IndexT
{
    // Check if the handle is valid
    if (!handle.IsValid()) {
        LOG_F(WARNING, "Invalid descriptor handle");
        return DescriptorHandle::kInvalidIndex;
    }

    // Find the local index from the global index
    auto local_index = ToLocalIndex(handle.GetIndex());
    if (local_index == DescriptorHandle::kInvalidIndex) {
        return DescriptorHandle::kInvalidIndex;
    }

    // Check if the descriptor is allocated
    if (!IsAllocated(local_index)) {
        LOG_F(WARNING, "Descriptor handle {} is not allocated", nostd::to_string(handle));
        return DescriptorHandle::kInvalidIndex;
    }

    // Return the local index as the shader-visible index
    return local_index;
}
