//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>

using oxygen::graphics::DescriptorAllocator;
using oxygen::graphics::DescriptorHandle;

DescriptorHandle::DescriptorHandle(
    DescriptorAllocator* allocator,
    const uint32_t index,
    const ResourceViewType view_type,
    const DescriptorVisibility visibility) noexcept
    : allocator_(allocator)
    , index_(index)
    , view_type_(view_type)
    , visibility_(visibility)
{
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
    other.Invalidate();
}

auto DescriptorHandle::operator=(DescriptorHandle&& other) noexcept -> DescriptorHandle&
{
    if (this != &other) {
        // Release our current descriptor if we have one
        Release();
        // Transfer ownership from other
        allocator_ = other.allocator_;
        index_ = other.index_;
        view_type_ = other.view_type_;
        visibility_ = other.visibility_;

        // Invalidate other
        other.Invalidate();
    }
    return *this;
}

void DescriptorHandle::Release() noexcept
{
    if (IsValid()) {
        allocator_->Release(*this);
        Invalidate();
    }
}
