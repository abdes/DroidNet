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
    DLOG_F(4, "DescriptorHandle created: index={}, view_type={}, visibility={}",
        index, nostd::to_string(view_type), nostd::to_string(visibility));
}

DescriptorHandle::~DescriptorHandle()
{
    DLOG_IF_F(4, IsValid(), "DescriptorHandle destroyed: index={}, view_type={}, visibility={}",
        index_, nostd::to_string(view_type_), nostd::to_string(visibility_));
    Release();
}

DescriptorHandle::DescriptorHandle(DescriptorHandle&& other) noexcept
    : allocator_(other.allocator_)
    , index_(other.index_)
    , view_type_(other.view_type_)
    , visibility_(other.visibility_)
{
    other.InvalidateInternal(true);
    DLOG_F(4, "DescriptorHandle move-constructed: index={}, view_type={}, visibility={}",
        index_, nostd::to_string(view_type_), nostd::to_string(visibility_));
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
        DLOG_F(4, "DescriptorHandle move-assigned: index={}, view_type={}, visibility={}",
            index_, nostd::to_string(view_type_), nostd::to_string(visibility_));
    }
    return *this;
}

void DescriptorHandle::Release() noexcept
{
    if (IsValid()) {
        DLOG_F(4, "DescriptorHandle::Release: index={}, view_type={}, visibility={}",
            index_, nostd::to_string(view_type_), nostd::to_string(visibility_));
        allocator_->Release(*this);
        Invalidate();
    }
}
void DescriptorHandle::InvalidateInternal(bool moved) noexcept
{
    allocator_ = nullptr;
    index_ = kInvalidIndex;
    view_type_ = ResourceViewType::kNone;
    visibility_ = DescriptorVisibility::kNone;

    if (!moved) {
        DLOG_F(4, "DescriptorHandle invalidated: index={}, view_type={}, visibility={}",
            index_, nostd::to_string(view_type_), nostd::to_string(visibility_));
    }
}
