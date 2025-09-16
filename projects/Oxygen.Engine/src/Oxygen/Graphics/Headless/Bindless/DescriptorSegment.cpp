//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Headless/Bindless/DescriptorSegment.h>

namespace oxygen::graphics::headless::bindless {

DescriptorSegment::DescriptorSegment(oxygen::bindless::Capacity capacity,
  const oxygen::bindless::HeapIndex base_index,
  const ResourceViewType view_type, const DescriptorVisibility visibility)
  : base_index_(base_index)
  , capacity_(capacity)
  , view_type_(view_type)
  , visibility_(visibility)
{
  LOG_SCOPE_F(INFO, "New Heap Segment");

  DLOG_F(INFO, "view type  : {}", view_type);
  DLOG_F(INFO, "visibility : {}", visibility);
  DLOG_F(INFO, "base index : {}", base_index);
  DLOG_F(INFO, "capacity   : {}", capacity);

  const auto cap = static_cast<size_t>(capacity.get());
  allocation_bitmap_.assign((cap + 7) / 8, 0);
  free_list_.reserve(cap);
}

auto DescriptorSegment::Allocate() noexcept -> oxygen::bindless::HeapIndex
{
  LOG_SCOPE_F(2, "Allocate bindless::HeapIndex");
  DLOG_F(2, "view type  : {}", view_type_);
  DLOG_F(1, "visibility : {}", visibility_);
  DLOG_F(2, "base index : {}", base_index_);

  auto idx = kInvalidBindlessHeapIndex;
  {
    std::lock_guard lock(mutex_);
    const auto cap = capacity_.get();
    // Try freelist first
    if (!free_list_.empty()) {
      idx = free_list_.back();
      free_list_.pop_back();
      const auto local = static_cast<size_t>(idx.get() - base_index_.get());
      allocation_bitmap_[local / 8] |= (static_cast<uint8_t>(1) << (local % 8));
    } else {
      // Bump allocate
      if (bump_cursor_ < cap) {
        const auto local = bump_cursor_++;
        allocation_bitmap_[local / 8]
          |= (static_cast<uint8_t>(1) << (local % 8));
        idx = oxygen::bindless::HeapIndex { base_index_.get() + local };
      }
    }
    ++allocated_count_;
  }
  DLOG_F(2, "remaining  : {}/{}", GetAvailableCount(), GetCapacity());

  return idx;
}

auto DescriptorSegment::Release(oxygen::bindless::HeapIndex index) noexcept
  -> bool
{
  LOG_SCOPE_F(2, "Release bindless::HeapIndex");
  if (index == kInvalidBindlessHeapIndex) {
    DLOG_F(2, "-shady- invalid handle");
    return false;
  }
  DLOG_F(2, "view type  : {}", view_type_);
  DLOG_F(1, "visibility : {}", visibility_);
  DLOG_F(2, "base index : {}", base_index_);

  {
    std::lock_guard lock(mutex_);
    const auto u_index = index.get();
    const auto base = base_index_.get();
    const auto cap = capacity_.get();
    if (u_index < base || u_index >= base + cap) {
      return false;
    }
    const auto local = static_cast<size_t>(u_index - base);
    const auto byte = local / 8;
    const auto bit = local % 8;
    const auto mask = static_cast<uint8_t>(1) << bit;
    if ((allocation_bitmap_[byte] & mask) == 0) {
      // double free or never allocated
      return false;
    }
    allocation_bitmap_[byte] &= ~mask;
    free_list_.emplace_back(
      static_cast<oxygen::bindless::HeapIndex::UnderlyingType>(u_index));
    --allocated_count_;
  }
  DLOG_F(2, "remaining  : {}/{}", GetAvailableCount(), GetCapacity());

  return true;
}

[[nodiscard]] auto DescriptorSegment::GetAvailableCount() const noexcept
  -> oxygen::bindless::Count
{
  std::lock_guard lock(mutex_);
  const auto cap = static_cast<uint32_t>(capacity_.get());
  return oxygen::bindless::Count { cap - allocated_count_ };
}

[[nodiscard]] auto DescriptorSegment::GetViewType() const noexcept
  -> ResourceViewType
{
  return view_type_;
}

[[nodiscard]] auto DescriptorSegment::GetVisibility() const noexcept
  -> DescriptorVisibility
{
  return visibility_;
}

[[nodiscard]] auto DescriptorSegment::GetBaseIndex() const noexcept
  -> oxygen::bindless::HeapIndex
{
  return base_index_;
}

[[nodiscard]] auto DescriptorSegment::GetCapacity() const noexcept
  -> oxygen::bindless::Capacity
{
  return capacity_;
}

[[nodiscard]] auto DescriptorSegment::GetAllocatedCount() const noexcept
  -> oxygen::bindless::Count
{
  std::lock_guard lock(mutex_);
  return oxygen::bindless::Count { allocated_count_ };
}

// [[nodiscard]] auto DescriptorSegment::GetShaderVisibleIndex(
//   const DescriptorHandle& handle) const noexcept ->
//   oxygen::bindless::HeapIndex
// {
//   // For headless, shader-visible index equals the global handle index
//   if (!handle.IsValid()) {
//     return kInvalidBindlessHandle;
//   }
//   const auto idx = handle.GetBindlessHandle().get();
//   const auto base = base_index_.get();
//   const auto cap = static_cast<uint32_t>(capacity_.get());
//   if (idx < base || idx >= base + cap) {
//     return kInvalidBindlessHandle;
//   }
//   return handle.GetBindlessHandle();
// }

} // namespace oxygen::graphics::headless::bindless
