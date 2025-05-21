//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics::d3d12 {

class DescriptorHeapSegment;

//! D3D12 implementation of descriptor allocator.
/*!
 Provides a Direct3D12-specific implementation of the descriptor allocator
 interface, creating and managing D3D12 descriptor heaps to fulfill allocation
 requests.

 This class:
 - Creates D3D12 descriptor heaps for different resource view types and visibilities
 - Translates between abstract descriptor handles and D3D12 native handles
 - Efficiently copies descriptors between heaps when needed
 - Prepares shader-visible descriptor heaps for rendering
*/
class DescriptorAllocator final : public graphics::detail::BaseDescriptorAllocator {
public:
    //! Creates a new D3D12 descriptor allocator.
    /*!
     \param heap_strategy The strategy for allocating descriptor heaps. If
            nullptr, a default strategy will be used.
     \param device The D3D12 device used for heap creation.

     Initializes the allocator with the provided D3D12 device and heap strategy.
     The device must remain valid for the lifetime of the allocator.
    */
    OXYGEN_D3D12_API explicit DescriptorAllocator(
        std::shared_ptr<const DescriptorAllocationStrategy> heap_strategy,
        dx::IDevice* device);

    //! Destructor.
    OXYGEN_D3D12_API ~DescriptorAllocator() override;

    OXYGEN_MAKE_NON_COPYABLE(DescriptorAllocator)
    OXYGEN_DEFAULT_MOVABLE(DescriptorAllocator)

    //! Gets the D3D12 CPU descriptor handle for a given descriptor handle.
    [[nodiscard]] OXYGEN_D3D12_API auto GetCpuHandle(const DescriptorHandle& handle) const
        -> D3D12_CPU_DESCRIPTOR_HANDLE;

    //! Gets the D3D12 GPU descriptor handle for a given descriptor handle.
    [[nodiscard]] OXYGEN_D3D12_API auto GetGpuHandle(const DescriptorHandle& handle) const
        -> D3D12_GPU_DESCRIPTOR_HANDLE;

    //! Copies a descriptor from source to destination.
    /*!
     \param dst The destination descriptor handle.
     \param src The source descriptor handle.

     Copies the descriptor from source to destination using the appropriate
     D3D12 copying mechanism depending on the descriptor types.
    */
    OXYGEN_D3D12_API void CopyDescriptor(
        const DescriptorHandle& dst,
        const DescriptorHandle& src) override;

    //! Prepares descriptor heaps for rendering.
    /*!
     \param command_list_obj A native object containing the D3D12 command list.

     Binds all necessary shader-visible descriptor heaps to the command list
     for rendering.
    */
    OXYGEN_D3D12_API void PrepareForRendering(const NativeObject& command_list_obj) override;

protected:
    //! Creates a D3D12-specific descriptor heap segment.
    /*!
     \param capacity The capacity of the new segment.
     \param base_index The base index for the new segment.
     \param view_type The resource view type for the new segment.
     \param visibility The memory visibility for the new segment.
     \return A unique_ptr to the created segment, or nullptr on failure.
    */
    OXYGEN_D3D12_API auto CreateHeapSegment(
        uint32_t capacity,
        uint32_t base_index,
        ResourceViewType view_type,
        DescriptorVisibility visibility)
        -> std::unique_ptr<graphics::detail::DescriptorHeapSegment> override;

private:
    //! Gets the D3D12DescriptorHeapSegment from a handle.
    auto GetD3D12Segment(const DescriptorHandle& handle) const
        -> const DescriptorHeapSegment*;

    dx::IDevice* device_; //!< The D3D12 device used for heap creation.
};

} // namespace oxygen::graphics::d3d12
