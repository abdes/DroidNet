//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include <Oxygen/Graphics/Common/Detail/FixedDescriptorHeapSegment.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics {

class DescriptorHandle;

namespace d3d12 {

    //! D3D12 implementation of descriptor heap segment.
    /*!
     Extends the fixed descriptor heap segment to provide D3D12-specific
     functionality for managing descriptor indices and mapping to D3D12 native
     descriptor handles.

     Each segment is associated with a specific D3D12 descriptor heap,
     corresponding to a particular ResourceViewType and DescriptorVisibility.
    */
    class DescriptorHeapSegment final : public graphics::detail::FixedDescriptorHeapSegment {
    public:
        OXYGEN_D3D12_API DescriptorHeapSegment(
            dx::IDevice* device,
            IndexT capacity,
            IndexT base_index,
            ResourceViewType view_type,
            DescriptorVisibility visibility,
            std::string_view debug_name);

        OXYGEN_D3D12_API DescriptorHeapSegment(
            dx::IDevice* device,
            const IndexT capacity,
            const IndexT base_index,
            const ResourceViewType view_type,
            const DescriptorVisibility visibility)
            : DescriptorHeapSegment(device, capacity, base_index, view_type, visibility, {})
        {
        }

        OXYGEN_D3D12_API ~DescriptorHeapSegment() override;

        OXYGEN_MAKE_NON_COPYABLE(DescriptorHeapSegment)
        OXYGEN_DEFAULT_MOVABLE(DescriptorHeapSegment)

        //! Checks if this segment's heap is shader visible.
        [[nodiscard]] OXYGEN_D3D12_API auto IsShaderVisible() const noexcept -> bool;

        //! Gets the underlying D3D12 descriptor heap.
        [[nodiscard]] OXYGEN_D3D12_API auto GetHeap() const
            -> dx::IDescriptorHeap*;

        //! Gets the D3D12 descriptor heap type for this segment.
        [[nodiscard]] OXYGEN_D3D12_API auto GetHeapType() const
            -> D3D12_DESCRIPTOR_HEAP_TYPE;

        //! Gets the D3D12 CPU descriptor handle for a given descriptor handle.
        //! Available for all descriptor heaps.
        /*!
         CPU handles are used for CPU-side operations via immediate methods on
         the device, such as creating views on resources or copying descriptor
         handles.
        */
        [[nodiscard]] OXYGEN_D3D12_API auto GetCpuHandle(const DescriptorHandle& handle) const
            -> D3D12_CPU_DESCRIPTOR_HANDLE;

        //! Gets the D3D12 GPU descriptor handle for a given descriptor handle.
        //! Available only for shader-visible heaps.
        /*!
         GPU handles are used to access descriptors via methods on the command
         lists, and are only valid for shader-visible heaps.
        */
        [[nodiscard]] OXYGEN_D3D12_API auto GetGpuHandle(const DescriptorHandle& handle) const
            -> D3D12_GPU_DESCRIPTOR_HANDLE;

        //! Gets the GPU descriptor handle that represents the start of the heap.
        /*!
         \throws std::runtime_error if the heap is not shader-visible.
        */
        [[nodiscard]] OXYGEN_D3D12_API auto GetGpuDescriptorTableStart() const
            -> D3D12_GPU_DESCRIPTOR_HANDLE;

        //! Gets the CPU descriptor handle that represents the start of the heap.
        [[nodiscard]] OXYGEN_D3D12_API auto GetCpuDescriptorTableStart() const
            -> D3D12_CPU_DESCRIPTOR_HANDLE;

    private:
        //! Computes a local index from a global index.
        [[nodiscard]] auto GlobalToLocalIndex(IndexT index) const -> IndexT;

        dx::IDevice* device_;

        dx::IDescriptorHeap* heap_ { nullptr };
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_start_ {};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_start_ {};
        UINT handle_increment_size_ { 0 };
        D3D12_DESCRIPTOR_HEAP_TYPE heap_type_;
    };

} // namespace d3d12

} // namespace oxygen::graphics
