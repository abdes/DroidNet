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

class DescriptorSegment;

//! D3D12 implementation of descriptor allocator.
/*!
 Provides a Direct3D12-specific implementation of the descriptor allocator
 interface, creating and managing D3D12 descriptor heaps to fulfill allocation
 requests.

 This class:
 - Creates D3D12 descriptor heaps for different resource view types and
   visibilities
 - Translates between abstract descriptor handles and D3D12 native handles
 - Efficiently copies descriptors between heaps when needed
 - Prepares shader-visible descriptor heaps for rendering
*/
class DescriptorAllocator final
  : public graphics::detail::BaseDescriptorAllocator {
public:
  //! Creates a new D3D12 descriptor allocator.
  /*!
   \param heap_strategy The strategy for allocating descriptor heaps. If
          nullptr, a default strategy will be used.
   \param device The D3D12 device used for heap creation.

   Initializes the allocator with the provided D3D12 device and heap strategy.
   The device must remain valid for the lifetime of the allocator.
  */
  OXGN_D3D12_API explicit DescriptorAllocator(
    std::shared_ptr<const DescriptorAllocationStrategy> heap_strategy,
    dx::IDevice* device);

  //! Destructor.
  OXGN_D3D12_API ~DescriptorAllocator() override;

  OXYGEN_MAKE_NON_COPYABLE(DescriptorAllocator)
  OXYGEN_DEFAULT_MOVABLE(DescriptorAllocator)

  //! Gets the D3D12 CPU descriptor handle for a given descriptor handle.
  OXGN_D3D12_NDAPI auto GetCpuHandle(const DescriptorHandle& handle) const
    -> D3D12_CPU_DESCRIPTOR_HANDLE;

  //! Gets the D3D12 GPU descriptor handle for a given descriptor handle.
  OXGN_D3D12_NDAPI auto GetGpuHandle(const DescriptorHandle& handle) const
    -> D3D12_GPU_DESCRIPTOR_HANDLE;

  //! Copies a descriptor from source to destination.
  /*!
   \param dst The destination descriptor handle.
   \param src The source descriptor handle.

   Copies the descriptor from source to destination using the appropriate
   D3D12 copying mechanism depending on the descriptor types.
  */
  OXGN_D3D12_API auto CopyDescriptor(
    const DescriptorHandle& dst, const DescriptorHandle& src) -> void override;

  OXGN_D3D12_API auto GetShaderVisibleHeaps()
    -> std::span<const detail::ShaderVisibleHeapInfo>;

  OXGN_D3D12_NDAPI auto GetShaderVisibleIndex(
    const DescriptorHandle& handle) const noexcept
    -> bindless::ShaderVisibleIndex override;

protected:
  //! Creates a D3D12-specific descriptor heap segment.
  /*!
   \param capacity The capacity of the new segment.
   \param base_index The base index for the new segment.
   \param view_type The resource view type for the new segment.
   \param visibility The memory visibility for the new segment.
   \return A unique_ptr to the created segment, or nullptr on failure.
  */
  OXGN_D3D12_API auto CreateHeapSegment(bindless::Capacity capacity,
    bindless::Handle base_index, ResourceViewType view_type,
    DescriptorVisibility visibility)
    -> std::unique_ptr<graphics::detail::DescriptorSegment> override;

private:
  //! Gets the D3D12DescriptorSegment from a handle.
  [[nodiscard]] auto GetD3D12Segment(const DescriptorHandle& handle) const
    -> const DescriptorSegment*;

  //! Updates the set of shader visible heaps.
  /*!
   This method is called to refresh the list of shader visible heaps whenever
   a new segment is created or an existing one is modified. To avoid
   deadlocks, it is not called immediately when a segment is created. Instead,
   we mark the needs_update_shader_visible_heaps_ flag and call this method
   when the Heaps() mutex is not held.
  */
  auto UpdateShaderVisibleHeapsSet() const -> void;

  //! The D3D12 descriptor heaps that are shader visible.
  mutable std::vector<detail::ShaderVisibleHeapInfo> shader_visible_heaps_;

  //! Flag to indicate if the collection of shader visible heaps needs to be
  //! updated.
  bool needs_update_shader_visible_heaps_ { false };

  dx::IDevice* device_; //!< The D3D12 device used for heap creation.
};

} // namespace oxygen::graphics::d3d12
