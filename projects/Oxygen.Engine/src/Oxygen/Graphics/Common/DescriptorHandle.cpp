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

DescriptorHandle::DescriptorHandle(
    DescriptorAllocator* allocator,
    const IndexT index,
    const ResourceViewType view_type,
    const DescriptorVisibility visibility) noexcept
    : allocator_(allocator)
    , index_(index)
    , view_type_(view_type)
    , visibility_(visibility)
{
    DCHECK_F(allocator != nullptr, "Allocator must not be null");
    DCHECK_F(index != kInvalidIndex, "Invalid index");
    DLOG_F(4, "constructed {}", nostd::to_string(*this));
}

DescriptorHandle::~DescriptorHandle()
{
    Release();
}

DescriptorHandle::DescriptorHandle(DescriptorHandle&& other) noexcept
    : allocator_(other.allocator_)
    , index_(other.index_)
    , view_type_(other.view_type_)
    , visibility_(other.visibility_)
{
    other.InvalidateInternal(true);
    DLOG_F(5, "move-constructed {}", nostd::to_string(*this));
}

auto DescriptorHandle::operator=(DescriptorHandle&& other) noexcept -> DescriptorHandle&
{
    if (this != &other) {
        Release();
        allocator_ = other.allocator_;
        index_ = other.index_;
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
        DLOG_F(4, "release {}", nostd::to_string(*this));
        allocator_->Release(*this);
        DCHECK_F(!IsValid(), "Allocator should invalidate descriptor after release");
    }
}
void DescriptorHandle::InvalidateInternal(bool moved) noexcept
{
    if (!moved) {
        DLOG_F(4, "invalidated: {}", nostd::to_string(*this));
    }

    allocator_ = nullptr;
    index_ = kInvalidIndex;
    view_type_ = ResourceViewType::kNone;
    visibility_ = DescriptorVisibility::kNone;
}
