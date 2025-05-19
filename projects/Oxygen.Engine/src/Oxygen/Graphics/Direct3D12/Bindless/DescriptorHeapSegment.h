//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <Oxygen/Graphics/Common/Detail/FixedDescriptorHeapSegment.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics::d3d12 {

//! D3D12 implementation of descriptor heap segment.
/*!
 Extends the fixed descriptor heap segment to provide D3D12-specific
 functionality for managing descriptor indices and mapping to D3D12 native
 descriptor handles.

 Each segment is associated with a specific D3D12 descriptor heap and range
 within that heap, corresponding to a particular ResourceViewType and
 DescriptorVisibility.
*/
class DescriptorHeapSegment final : public detail::FixedDescriptorHeapSegment {
public:
    //! Creates a new D3D12 descriptor heap segment.
    /*!
     \param device The D3D12 device.
     \param capacity The capacity of the segment.
     \param base_index The base index of the segment.
     \param view_type The resource view type.
     \param visibility The descriptor visibility.
     \param debug_name Optional debug name for the created heap.

     Following RAII principles, this constructor handles all heap creation
     internally. The segment owns and manages its D3D12 descriptor heap for
     its entire lifetime.
    */
    OXYGEN_D3D12_API DescriptorHeapSegment(
        dx::IDevice* device,
        IndexT capacity,
        IndexT base_index,
        ResourceViewType view_type,
        DescriptorVisibility visibility,
        const char* debug_name = nullptr);

    OXYGEN_D3D12_API ~DescriptorHeapSegment() override;

    OXYGEN_MAKE_NON_COPYABLE(DescriptorHeapSegment)
    OXYGEN_DEFAULT_MOVABLE(DescriptorHeapSegment)

    //! Gets the D3D12 CPU descriptor handle for a given index.
    /*!
     \param global_index The global descriptor index.
     \return The D3D12 CPU descriptor handle.
    */
    OXYGEN_D3D12_API auto GetCpuHandle(IndexT global_index) const -> D3D12_CPU_DESCRIPTOR_HANDLE;

    //! Gets the D3D12 GPU descriptor handle for a given index.
    /*!
     \param global_index The global descriptor index.
     \return The D3D12 GPU descriptor handle, or an invalid handle for CPU-only heaps.
    */
    OXYGEN_D3D12_API auto GetGpuHandle(IndexT global_index) const -> D3D12_GPU_DESCRIPTOR_HANDLE;

    //! Checks if this segment's heap is shader visible.
    [[nodiscard]] OXYGEN_D3D12_API auto IsShaderVisible() const noexcept -> bool;

    //! Gets the underlying D3D12 descriptor heap.
    [[nodiscard]] OXYGEN_D3D12_API auto GetHeap() const -> ID3D12DescriptorHeap*;

    //! Gets the D3D12 descriptor heap type for this segment.
    [[nodiscard]] OXYGEN_D3D12_API auto GetD3D12HeapType() const -> D3D12_DESCRIPTOR_HEAP_TYPE;

private:
    //! Computes a local index from a global index.
    [[nodiscard]] auto GlobalToLocalIndex(IndexT global_index) const -> IndexT;

    dx::IDevice* device_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_start_;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_start_;
    UINT handle_increment_size_;
    D3D12_DESCRIPTOR_HEAP_TYPE heap_type_;
};

} // namespace oxygen::graphics::d3d12
