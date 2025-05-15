//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

class DescriptorHandle;
class NativeObject;
class IndexRange;

//! Abstract interface for descriptor allocation from heaps/pools.
/*!
 Manages descriptor heaps/pools of different types and visibility. Each descriptor
 type typically requires a separate heap/pool allocation, and each heap/pool has
 an associated visibility (shader-visible or non-shader-visible).

 In D3D12, this maps to descriptor heaps of different types (CBV_SRV_UAV, SAMPLER,
 RTV, DSV) each with a visibility flag. In Vulkan, this maps to descriptor pools
 that can contain mixed descriptor types.

 The allocator owns the descriptor heaps/pools and is responsible for allocating,
 releasing, and optionally copying descriptors. It provides methods for obtaining
 platform-specific handles for descriptors and preparing resources for rendering.

 The allocator is responsible for managing the lifecycle of descriptors but not
 the resources they describe (textures, buffers, etc.).
*/
class DescriptorAllocator {
public:
    virtual ~DescriptorAllocator() = default;
    //! Allocates a descriptor of the specified view type from the specified space.
    /*!
     \param view_type The resource view type to allocate (SRV, UAV, CBV, Sampler, RTV, DSV).
     \param space The memory space to allocate from (shader-visible or non-shader-visible).
     \return A handle to the allocated descriptor.
    */
    virtual DescriptorHandle Allocate(ResourceViewType view_type, DescriptorVisibility visibility) = 0;

    //! Releases a previously allocated descriptor.
    /*!
     \param handle The handle to release. After this call, the handle will be invalidated.
    */
    virtual void Release(DescriptorHandle& handle) = 0;

    //! Copies a descriptor from one space to another.
    /*!
     \param source The source descriptor handle.
     \param destination The destination descriptor handle.

     Source and destination must be of the same descriptor type but can be in
     different spaces. Typically used to copy from non-shader-visible to shader-visible.
    */
    virtual void CopyDescriptor(const DescriptorHandle& source, const DescriptorHandle& destination) = 0;

    //! Gets a native platform-specific handle from a descriptor handle.
    /*!
     \param handle The descriptor handle.
     \return A NativeObject representing the platform-specific handle.

     The returned handle might be a D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE,
     or VkDescriptorSet, depending on the platform and the handle's space.
    */
    virtual NativeObject GetNativeHandle(const DescriptorHandle& handle) const = 0;

    //! Prepares descriptors for rendering, binding necessary heaps or sets.
    /*!
     \param command_list The command list to bind to (passed as NativeObject).

     This method is called before rendering to ensure all necessary descriptor
     heaps or sets are properly bound to the command list or command buffer.
    */
    virtual void PrepareForRendering(const NativeObject& command_list) = 0;
    //! Returns the number of descriptors remaining of a specific view type in a specific space.
    /*!
     \param view_type The resource view type.
     \param space The memory space.
     \return The number of descriptors remaining.
    */
    virtual uint32_t GetRemainingDescriptors(ResourceViewType view_type, DescriptorVisibility visibility) const = 0;
};

} // namespace oxygen::graphics
