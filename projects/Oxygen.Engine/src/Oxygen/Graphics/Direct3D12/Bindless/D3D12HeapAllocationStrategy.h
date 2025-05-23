//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>
#include <string>
#include <unordered_map>

// ReSharper disable once CppUnusedIncludeDirective
#include <Oxygen/Base/Compilers.h> // needed for OXYGEN_UNREACHABLE_RETURN

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics::d3d12 {

//! D3D12-specific allocation strategy for descriptor heaps.
/*!
 Provides an optimal allocation strategy for D3D12 descriptor heaps, respecting
 the platform's constraints and capabilities:

 - Disables growth and creates a single segment per heap type (CBV_SRV_UAV,
   SAMPLER, RTV, DSV), in line with D3D12's design.
 - Sets appropriate conservative capacities based on D3D12 limits.
 - Enforces appropriate visibility constraints (e.g., RTV/DSV are always
   CPU-only).

 - Only one shader-visible heap of each type can be bound at a time.
 - Only CBV_SRV_UAV and SAMPLER heaps can be shader-visible.
*/
class D3D12HeapAllocationStrategy final : public DescriptorAllocationStrategy {
public:
    //! Constructor that initializes the strategy with a D3D12 device.
    /*!
     \param device The D3D12 device to query for capabilities.

     Determines appropriate heap sizes based on device capabilities. If device
     is nullptr, uses conservative defaults.
    */
    OXYGEN_D3D12_API explicit D3D12HeapAllocationStrategy(dx::IDevice* device = nullptr);

    OXYGEN_D3D12_API ~D3D12HeapAllocationStrategy() override = default;

    OXYGEN_DEFAULT_COPYABLE(D3D12HeapAllocationStrategy)
    OXYGEN_DEFAULT_MOVABLE(D3D12HeapAllocationStrategy)

    //! Returns a unique key based on the D3D12 heap type and visibility.
    /*!
     Maps the abstract ResourceViewType to the corresponding D3D12 descriptor
     heap type, then combines with visibility to create a unique string key.

     \throws std::runtime_error if the view_type or visibility are invalid, or
             their combination is illegal.
    */
    OXYGEN_D3D12_API auto GetHeapKey(
        ResourceViewType view_type, DescriptorVisibility visibility) const
        -> std::string override;

    //! Returns the heap description for a given heap key.
    /*!
     \throws std::runtime_error if the key is invalid.
    */
    OXYGEN_D3D12_API auto GetHeapDescription(const std::string& heap_key) const
        -> const HeapDescription& override;

    //! Returns the base index for descriptors in the heap.
    /*!
     \throws std::runtime_error if the view_type or visibility are invalid, or
             their combination is illegal.
    */
    OXYGEN_D3D12_API auto GetHeapBaseIndex(
        ResourceViewType view_type, DescriptorVisibility visibility) const
        -> DescriptorHandle::IndexT override;

    //! Returns the D3D12 descriptor heap type for a given view type.
    /*!
     This method is used internally to map the abstract ResourceViewType to the
     corresponding D3D12 descriptor heap type. Caller must ensure that the
     view_type is valid and supported. The method will abort if not.
     */
    static OXYGEN_D3D12_API auto GetHeapType(ResourceViewType view_type) noexcept
        -> D3D12_DESCRIPTOR_HEAP_TYPE;

    //! Returns descriptor heap flags for a given visibility.
    static constexpr auto GetHeapFlags(const DescriptorVisibility visibility) noexcept
        -> D3D12_DESCRIPTOR_HEAP_FLAGS
    {
        return visibility == DescriptorVisibility::kShaderVisible
            ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
            : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    }

private:
    //! Builds a heap key string from type and visibility
    static auto BuildHeapKey(D3D12_DESCRIPTOR_HEAP_TYPE type, bool shader_visible)
        -> std::string;

    //! Maps heap keys to their descriptions
    std::unordered_map<std::string, HeapDescription> heap_descriptions_;

    //! Maps (view_type, visibility) pairs to their base indices
    std::unordered_map<std::string, DescriptorHandle::IndexT> heap_base_indices_;
};

} // namespace oxygen::graphics::d3d12
