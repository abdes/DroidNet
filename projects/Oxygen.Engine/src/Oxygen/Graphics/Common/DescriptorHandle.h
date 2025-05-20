//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cassert>
#include <cstdint>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

class DescriptorAllocator;

//! An allocated descriptor slot with stable index for use by shaders.
/*!
 Represents a descriptor in a descriptor heap/pool, containing backend-specific
 information needed to identify and use the descriptor. In D3D12, this would
 represent a descriptor in a descriptor heap; in Vulkan, a descriptor in a
 descriptor pool.

 Each descriptor is associated with a specific type (CBV/SRV/UAV, Sampler, etc.)
 and exists in a specific memory visibility (shader-visible or CPU-only). The
 type determines which heap it's allocated from in D3D12 and the binding type in
 Vulkan. The visibility determines which heap it's allocated from in D3D12 and
 the memory location in Vulkan.

 Has limited ownership semantics: can release its descriptor back to the
 allocator but doesn't own the underlying resource. Contains a back-reference to
 its allocator for lifetime management.

 This class follows RAII principles and will automatically release its
 descriptor back to the allocator when destroyed, unless it has been moved from
 or explicitly released.

 The handle is non-copyable to enforce proper ownership semantics, but is
 movable to allow transferring ownership of the descriptor slot. This supports
 efficient management of descriptor resources in modern graphics applications.

 Usage:
 - Obtain from a DescriptorAllocator via the Allocate method
 - Store in resource registries or pass to rendering commands
 - Access the stable index via GetIndex() for shader bindings
 - Release explicitly when no longer needed, or let RAII handle cleanup
*/
class DescriptorHandle {
    friend class DescriptorAllocator;

public:
    //! The underlying type for the descriptor index.
    using IndexT = uint32_t;

    //! Represents an invalid descriptor index.
    static constexpr IndexT kInvalidIndex = ~0U;

    //! Default constructor creates an invalid handle.
    DescriptorHandle() noexcept = default;

    //! Destructor that automatically releases the descriptor if still valid.
    OXYGEN_GFX_API ~DescriptorHandle();

    OXYGEN_MAKE_NON_COPYABLE(DescriptorHandle)

    //! Move constructor transfers ownership of the descriptor.
    OXYGEN_GFX_API DescriptorHandle(DescriptorHandle&& other) noexcept;

    //! Move assignment transfers ownership of the descriptor.
    OXYGEN_GFX_API auto operator=(DescriptorHandle&& other) noexcept -> DescriptorHandle&;

    [[nodiscard]] constexpr auto operator==(const DescriptorHandle& other) const noexcept
    {
        return allocator_ == other.allocator_
            && index_ == other.index_
            && view_type_ == other.view_type_
            && visibility_ == other.visibility_;
    }

    [[nodiscard]] constexpr auto operator!=(const DescriptorHandle& other) const noexcept
    {
        return !(*this == other);
    }

    [[nodiscard]] constexpr auto IsValid() const noexcept
    {
        const auto properly_allocated = allocator_ != nullptr && index_ != kInvalidIndex;
        // When properly allocated, the view type and visibility should also be
        // valid.
        assert(!properly_allocated || oxygen::graphics::IsValid(view_type_));
        assert(!properly_allocated || oxygen::graphics::IsValid(visibility_));
        return properly_allocated;
    }

    [[nodiscard]] constexpr auto GetIndex() const noexcept
    {
        return index_;
    }

    //! Gets the resource view type (SRV, UAV, CBV, Sampler, etc.) of this
    //! descriptor.
    [[nodiscard]] constexpr auto GetViewType() const noexcept
    {
        return view_type_;
    }

    //! Gets the visibility of this descriptor (CPU-only, Shaders, etc.).
    [[nodiscard]] constexpr auto GetVisibility() const noexcept
    {
        return visibility_;
    }

    //! Explicitly releases the descriptor back to its allocator, and
    //! invalidates the handle.
    OXYGEN_GFX_API void Release() noexcept;

    //! Invalidates this handle without releasing the descriptor.
    OXYGEN_GFX_API void Invalidate() noexcept;

protected:
    //! No allocator constructor creates an invalid handle. Primarily useful for
    //! unit tests.
    OXYGEN_GFX_API DescriptorHandle(IndexT index,
        ResourceViewType view_type, DescriptorVisibility visibility) noexcept;

    //! Constructor that takes an allocator and index.
    /*!
     Creating a valid handle can only be done by the entity that allocated
     descriptors. In the current design, this is the DescriptorAllocator
     class.

     This constructor is protected to prevent misuse outside the allocator
     context, while still allowing unit tests to create handles for testing
     purposes via derivation.
    */
    OXYGEN_GFX_API DescriptorHandle(
        DescriptorAllocator* allocator, IndexT index,
        ResourceViewType view_type, DescriptorVisibility visibility) noexcept;

private:
    OXYGEN_GFX_API void InvalidateInternal(bool moved) noexcept;

    //! Back-reference to allocator for lifetime management.
    DescriptorAllocator* allocator_ { nullptr };

    //! Stable index for shader reference.
    IndexT index_ { kInvalidIndex };

    //! Resource view type (SRV, UAV, CBV, Sampler, etc.).
    ResourceViewType view_type_ { ResourceViewType::kNone };

    //! Visibility of the memory space where this descriptor resides.
    DescriptorVisibility visibility_ { DescriptorVisibility::kNone };
};

//! Converts a `DescriptorHandle` to a string representation.
auto to_string(const DescriptorHandle& obj) -> std::string;

} // namespace oxygen::graphics
