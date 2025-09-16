//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>

using oxygen::graphics::DescriptorAllocator;
using oxygen::graphics::DescriptorHandle;

DescriptorHandle::DescriptorHandle(DescriptorAllocator* allocator,
  const bindless::HeapIndex index, const ResourceViewType view_type,
  const DescriptorVisibility visibility) noexcept
  : allocator_(allocator)
  , handle_(index)
  , view_type_(view_type)
  , visibility_(visibility)
{
  DCHECK_F(allocator != nullptr, "Allocator must not be null");
  DCHECK_F(index != kInvalidBindlessHeapIndex, "Invalid index");
  DLOG_F(4, "constructed {}", *this);
}

DescriptorHandle::DescriptorHandle(const bindless::HeapIndex index,
  const ResourceViewType view_type,
  const DescriptorVisibility visibility) noexcept
  : handle_(index)
  , view_type_(view_type)
  , visibility_(visibility)
{
  DLOG_F(4, "constructed(invalid) {}", *this);
}

DescriptorHandle::~DescriptorHandle() { Release(); }

DescriptorHandle::DescriptorHandle(DescriptorHandle&& other) noexcept
  : allocator_(other.allocator_)
  , handle_(other.handle_)
  , view_type_(other.view_type_)
  , visibility_(other.visibility_)
{
  other.InvalidateInternal(true);
  DLOG_F(5, "move-constructed {}", *this);
}

auto DescriptorHandle::operator=(DescriptorHandle&& other) noexcept
  -> DescriptorHandle&
{
  if (this != &other) {
    Release();
    allocator_ = other.allocator_;
    handle_ = other.handle_;
    view_type_ = other.view_type_;
    visibility_ = other.visibility_;
    other.InvalidateInternal(true);
  }
  return *this;
}

void DescriptorHandle::Invalidate() noexcept
{
  if (!IsValid()) {
    return;
  }
  InvalidateInternal(false);
}

void DescriptorHandle::Release() noexcept
{
  if (IsValid()) {
    DLOG_F(4, "release {}", *this);
    allocator_->Release(*this);
    DCHECK_F(
      !IsValid(), "Allocator should invalidate descriptor after release");
  }
}
void DescriptorHandle::InvalidateInternal(bool moved) noexcept
{
  if (!moved) {
    DLOG_F(4, "invalidated: {}", *this);
  }

  allocator_ = nullptr;
  handle_ = kInvalidBindlessHeapIndex;
  view_type_ = ResourceViewType::kNone;
  visibility_ = DescriptorVisibility::kNone;
}
