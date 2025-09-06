//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Detail/FixedDescriptorSegment.h>

using oxygen::graphics::detail::FixedDescriptorSegment;

FixedDescriptorSegment::FixedDescriptorSegment(
  const bindless::Capacity capacity, const bindless::Handle base_index,
  const ResourceViewType view_type, const DescriptorVisibility visibility)
  : capacity_(capacity)
  , view_type_(view_type)
  , visibility_(visibility)
  , base_index_(base_index)
  , next_index_ { bindless::Handle { 0 } }
  , released_flags_(capacity.get(), false)
{
  // Log heap segment creation
  DLOG_F(1, "type       : {}", view_type);
  DLOG_F(1, "visibility : {}", visibility);
  DLOG_F(1, "capacity   : {}", capacity);
  DLOG_F(1, "base index : {}", base_index);
}

//! Converts a global descriptor index to a local index within the segment.
auto FixedDescriptorSegment::ToLocalIndex(
  const bindless::Handle global_index) const noexcept -> bindless::Handle
{
  // Ensure the global index belongs to this segment's range.
  if (global_index < base_index_) {
    LOG_F(WARNING, "Descriptor handle, with index {}, is out of my range",
      global_index);
    return kInvalidBindlessHandle;
  }

  const auto local
    = bindless::Handle { (global_index.get() - base_index_.get()) };
  if (local.get() >= capacity_.get()) {
    LOG_F(WARNING, "Descriptor handle, with index {}, is out of my range",
      global_index);
    return kInvalidBindlessHandle;
  }
  return local;
}

//! Checks if a local index is currently allocated in the segment.
auto FixedDescriptorSegment::IsAllocated(
  const bindless::Handle local_index) const noexcept -> bool
{
  const auto idx = local_index.get();
  if (idx >= next_index_.get()) {
    return false;
  }
  return !released_flags_[static_cast<size_t>(idx)];
}

inline auto FixedDescriptorSegment::FreeListSize() const -> bindless::Count
{
  const size_t free_count = free_list_.size();
  DCHECK_F(std::cmp_less(free_count, bindless::kMaxCount.get()),
    "unexpected size of free list ({}), "
    "larger than what bindless::Count can hold",
    free_count);
  return bindless::Count {
    static_cast<bindless::Count::UnderlyingType>(free_count),
  };
}

FixedDescriptorSegment::~FixedDescriptorSegment() noexcept
{
  LOG_SCOPE_F(1, "~FixedDescriptorSegment");
  DLOG_F(1, "view type  : {}", view_type_);
  DLOG_F(1, "visibility : {}", visibility_);
  DLOG_F(1, "base index : {}", base_index_);
  DLOG_F(1, "capacity   : {}", capacity_);
  try {
    // Do not call the virtual method GetSize() in the destructor.
    if (const auto size = next_index_.get() - FreeListSize().get(); size > 0U) {
      LOG_F(WARNING, "  with ({}) descriptors still allocated", size);
    }
  } catch (...) {
    // Nothing to do, but adding a placeholder statement to avoid warnings.
    [[maybe_unused]] auto _ = 0;
  }
}

auto FixedDescriptorSegment::Allocate() noexcept -> bindless::Handle
{
  LOG_SCOPE_F(2, "Allocate bindless::Handle");
  DLOG_F(2, "view type  : {}", view_type_);
  DLOG_F(1, "visibility : {}", visibility_);
  DLOG_F(2, "base index : {}", base_index_);

  auto global_index = kInvalidBindlessHandle;

  // First try to reuse a released descriptor (LIFO for better cache locality)
  if (!free_list_.empty()) {
    const auto local_index = free_list_.back();
    free_list_.pop_back();
    released_flags_[static_cast<size_t>(local_index.get())] = false;
    DLOG_F(2, " -> recycled local index {}", local_index);
    global_index = bindless::Handle { base_index_.get() + local_index.get() };
    DLOG_F(2, " -> global index {}", global_index);
  } else if (next_index_.get() < capacity_.get()) {
    // If no freed descriptors, allocate a new one
    const auto local_index = bindless::Handle { next_index_.get() };
    next_index_ = bindless::Handle { next_index_.get() + 1 };
    DLOG_F(2, " -> allocated new local index {}", local_index);
    global_index = bindless::Handle { base_index_.get() + local_index.get() };
    DLOG_F(2, " -> global index {}", global_index);
  } else {
    // No more descriptors available
    DLOG_F(ERROR, "-failed- segment is full");
  }
  DLOG_F(2, "remaining  : {}/{}", GetAvailableCount(), GetCapacity());

  return global_index;
}

auto FixedDescriptorSegment::Release(const bindless::Handle index) noexcept
  -> bool
{
  LOG_SCOPE_F(2, "Release bindless::Handle");
  if (index == kInvalidBindlessHandle) {
    DLOG_F(2, "-shady- invalid handle");
    return false;
  }
  DLOG_F(2, "view type  : {}", view_type_);
  DLOG_F(1, "visibility : {}", visibility_);
  DLOG_F(2, "base index : {}", base_index_);

  // Convert to local index
  auto local_index = ToLocalIndex(index);
  if (local_index == kInvalidBindlessHandle) {
    DLOG_F(2, "-shady- invalid conversion to local index");
    return false;
  }
  LOG_F(2, "handle     : g:{}/l:{}", index, local_index);

  // Check if this index was never allocated or is beyond the currently
  // allocated range.
  if (!IsAllocated(local_index)) {
    LOG_F(WARNING, " -> already released");
    return false;
  }

  // Add to the free list
  try {
    free_list_.emplace_back(local_index);
  } catch (const std::exception& ex) {
    // The only reason this would fail is due to memory allocation failure.
    LOG_F(ERROR, "-failed- {}", ex.what());
    return false;
  }

  // Mark as released
  released_flags_[local_index.get()] = true;

  DLOG_F(2, "remaining  : {}/{}", GetAvailableCount(), GetCapacity());
  return true;
}

auto FixedDescriptorSegment::GetAvailableCount() const noexcept
  -> bindless::Count
{
  const auto available
    = capacity_.get() - next_index_.get() + FreeListSize().get();
  return bindless::Count { static_cast<uint32_t>(available) };
}

auto FixedDescriptorSegment::GetAllocatedCount() const noexcept
  -> bindless::Count
{
  const auto allocated = next_index_.get() - FreeListSize().get();
  return bindless::Count { static_cast<uint32_t>(allocated) };
}

auto FixedDescriptorSegment::ReleaseAll() -> void
{
  // Clear the free list and reset the released flags
  free_list_.clear();
  released_flags_.assign(capacity_.get(), false);
  // Reset the next index to zero
  next_index_ = bindless::Handle { 0 };
}
