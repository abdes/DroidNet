//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>

#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Direct3D12/CommandRecorder.h>
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

    //! Prepares and binds all necessary descriptor resources for rendering.
    /*!
     This method must be called before issuing any draw or dispatch commands
     that use descriptors allocated by this allocator. It ensures that all
     required descriptor resources (such as descriptor heaps in Direct3D 12 or
     descriptor sets in Vulkan) are properly bound to the provided command
     recorder's underlying command list or command buffer.

     \note This method does not allocate or update descriptors; it only ensures
           that the correct resources are bound for GPU access during rendering.

     Only graphics or compute command lists/buffers are valid for binding
     descriptor resources; copy command lists/buffers are not supported and must
     not be used. Descriptor bindings are local to each command list or buffer
     and must be set on every command list or buffer that will use bindless or
     descriptor-based resources. Bindings do not persist across command lists,
     command buffers, or frames.

     Good practice is to call this method once per frame for each command list
     or buffer that will issue rendering or compute work using descriptors
     managed by this allocator.

     \throws std::runtime_error if the command list or buffer is invalid
             or of an unsupported type.
    */
    OXYGEN_D3D12_API auto GetShaderVisibleHeaps()
        -> std::span<const detail::ShaderVisibleHeapInfo>;

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
    [[nodiscard]] auto GetD3D12Segment(const DescriptorHandle& handle) const
        -> const DescriptorHeapSegment*;

    //! Updates the set of shader visible heaps.
    /*!
     This method is called to refresh the list of shader visible heaps whenever
     a new segment is created or an existing one is modified. To avoid
     deadlocks, it is not called immediately when a segment is created. Instead,
     we mark the needs_update_shader_visible_heaps_ flag and call this method
     from the PrepareForRender() when the Heaps() mutex is not held.
    */
    void UpdateShaderVisibleHeapsSet() const;

    //! The D3D12 descriptor heaps that are shader visible.
    mutable std::vector<detail::ShaderVisibleHeapInfo> shader_visible_heaps_;

    //! Flag to indicate if the collection of shader visible heaps needs to be
    //! updated.
    bool needs_update_shader_visible_heaps_ { false };

    dx::IDevice* device_; //!< The D3D12 device used for heap creation.
};

} // namespace oxygen::graphics::d3d12
