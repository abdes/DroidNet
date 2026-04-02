//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/DescriptorAllocationHandle.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>

using oxygen::graphics::DescriptorAllocationHandle;
using oxygen::graphics::DescriptorAllocationKind;
using oxygen::graphics::DescriptorAllocator;

DescriptorAllocationHandle::DescriptorAllocationHandle(
  DescriptorAllocator* allocator, const bindless::HeapIndex index,
  const ResourceViewType view_type, const DescriptorVisibility visibility,
  const DescriptorAllocationKind kind,
  const bindless::DomainToken domain) noexcept
  : allocator_(allocator)
  , handle_(index)
  , view_type_(view_type)
  , visibility_(visibility)
  , kind_(kind)
  , domain_(domain)
{
  DCHECK_F(allocator != nullptr, "Allocator must not be null");
  DCHECK_F(index != kInvalidBindlessHeapIndex, "Invalid index");
  DLOG_F(4, "constructed {}", *this);
}

DescriptorAllocationHandle::DescriptorAllocationHandle(
  const bindless::HeapIndex index, const ResourceViewType view_type,
  const DescriptorVisibility visibility, const DescriptorAllocationKind kind,
  const bindless::DomainToken domain) noexcept
  : handle_(index)
  , view_type_(view_type)
  , visibility_(visibility)
  , kind_(kind)
  , domain_(domain)
{
  DLOG_F(4, "constructed(invalid) {}", *this);
}

DescriptorAllocationHandle::~DescriptorAllocationHandle() { Release(); }

DescriptorAllocationHandle::DescriptorAllocationHandle(
  DescriptorAllocationHandle&& other) noexcept
  : allocator_(other.allocator_)
  , handle_(other.handle_)
  , view_type_(other.view_type_)
  , visibility_(other.visibility_)
  , kind_(other.kind_)
  , domain_(other.domain_)
{
  other.InvalidateInternal(true);
  DLOG_F(5, "move-constructed {}", *this);
}

auto DescriptorAllocationHandle::operator=(
  DescriptorAllocationHandle&& other) noexcept -> DescriptorAllocationHandle&
{
  if (this != &other) {
    Release();
    allocator_ = other.allocator_;
    handle_ = other.handle_;
    view_type_ = other.view_type_;
    visibility_ = other.visibility_;
    kind_ = other.kind_;
    domain_ = other.domain_;
    other.InvalidateInternal(true);
  }
  return *this;
}

void DescriptorAllocationHandle::Invalidate() noexcept
{
  if (!IsValid()) {
    return;
  }
  InvalidateInternal(false);
}

void DescriptorAllocationHandle::Release() noexcept
{
  if (IsValid()) {
    DLOG_F(4, "release {}", *this);
    allocator_->Release(*this);
    DCHECK_F(
      !IsValid(), "Allocator should invalidate descriptor after release");
  }
}

void DescriptorAllocationHandle::InvalidateInternal(bool moved) noexcept
{
  if (!moved) {
    DLOG_F(4, "invalidated: {}", *this);
  }

  allocator_ = nullptr;
  handle_ = kInvalidBindlessHeapIndex;
  view_type_ = ResourceViewType::kNone;
  visibility_ = DescriptorVisibility::kNone;
  kind_ = DescriptorAllocationKind::kInvalid;
  domain_ = bindless::generated::kInvalidDomainToken;
}
