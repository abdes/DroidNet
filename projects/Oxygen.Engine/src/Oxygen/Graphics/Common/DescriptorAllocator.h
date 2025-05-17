//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

namespace oxygen::graphics {

class NativeObject;

//! Describes the properties of a descriptor heap or pool.
struct HeapDescription {
    //! Initial capacity for CPU-visible descriptors.
    uint32_t cpu_visible_capacity { 0 };

    //! Initial capacity for shader-visible descriptors.
    uint32_t shader_visible_capacity { 0 };

    //! Flag indicating if dynamic growth is allowed when heaps are full.
    bool allow_growth { true };

    //! Growth factor when expanding descriptor heaps.
    float growth_factor { 2.0f };

    //! Maximum number of growth iterations before failing allocations.
    uint32_t max_growth_iterations { 3 };
};

//! Interface for heap mapping strategy used by descriptor allocators.
class DescriptorAllocationStrategy {
public:
    virtual ~DescriptorAllocationStrategy() = default;

    //! Returns a unique heap key string for a given view type and visibility.
    /*!
     May throw an exception if the view type or visibility is not recognized.
    */
    virtual auto GetHeapKey(ResourceViewType view_type, DescriptorVisibility visibility) const -> std::string = 0;

    //! Returns the heap description for a given heap key.
    /*!
     May throw an exception if the key is not recognized.
    */
    virtual auto GetHeapDescription(const std::string& heap_key) const -> const HeapDescription& = 0;
};

//! Default heap mapping strategy: one heap per (view type, visibility) pair,
//! using some reasonable value for HeapDescription.
class DefaultDescriptorAllocationStrategy : public DescriptorAllocationStrategy {
public:
    ~DefaultDescriptorAllocationStrategy() override = default;

    //! Returns a unique key formed by concatenating the view type and
    //! visibility, separated by a colon.
    /*!
     The view type part is guaranteed to be unique on its own, and can be used
     to index the heap descriptions table.

     \note It is not reccommended to frequently call this function in a
           performance-critical path as it creates a new string each time.

     \return A unique key for the combination of view type and visibility, or
             "__Unknown__:__Unknown__" if the view type or visibility is not
             recognized as valid.
    */
    auto GetHeapKey(ResourceViewType view_type, DescriptorVisibility visibility) const
        -> std::string override
    {
        std::string view_type_str;
        switch (view_type) {
            // clang-format off
        case ResourceViewType::kTexture_SRV: view_type_str = "Texture_SRV"; break;
        case ResourceViewType::kTexture_UAV: view_type_str = "Texture_UAV"; break;
        case ResourceViewType::kTypedBuffer_SRV: view_type_str = "TypedBuffer_SRV"; break;
        case ResourceViewType::kTypedBuffer_UAV: view_type_str = "TypedBuffer_UAV"; break;
        case ResourceViewType::kStructuredBuffer_UAV: view_type_str = "StructuredBuffer_UAV"; break;
        case ResourceViewType::kStructuredBuffer_SRV: view_type_str = "StructuredBuffer_SRV"; break;
        case ResourceViewType::kRawBuffer_SRV: view_type_str = "RawBuffer_SRV"; break;
        case ResourceViewType::kRawBuffer_UAV: view_type_str = "RawBuffer_UAV"; break;
        case ResourceViewType::kConstantBuffer: view_type_str = "ConstantBuffer"; break;
        case ResourceViewType::kSampler: view_type_str = "Sampler"; break;
        case ResourceViewType::kSamplerFeedbackTexture_UAV: view_type_str = "SamplerFeedbackTexture_UAV"; break;
        case ResourceViewType::kRayTracingAccelStructure: view_type_str = "RayTracingAccelStructure"; break;
        case ResourceViewType::kTexture_DSV: view_type_str = "Texture_DSV"; break;
        case ResourceViewType::kTexture_RTV: view_type_str = "Texture_RTV"; break;
        default: view_type_str = "__Unknown__"; break;
            // clang-format on
        }

        std::string visibility_str;
        switch (visibility) {
            // clang-format off
        case DescriptorVisibility::kCpuOnly: visibility_str = "cpu"; break;
        case DescriptorVisibility::kShaderVisible: visibility_str = "gpu"; break;
        default: visibility_str = "__Unknown__"; break;
            // clang-format on
        }

        if (view_type_str == "__Unknown__" || visibility_str == "__Unknown__") {
            return "__Unknown__:__Unknown__";
        }
        return view_type_str + ":" + visibility_str;
    }

    //! Returns the heap description for a given heap key.
    /*!
     Uses the resource view type part of the heap key to find the corresponding
     heap description in the heaps configuration map.

     \param heap_key The heap key string (format: ViewType:Visibility).
     \return The corresponding HeapDescription.
     \throws std::runtime_error if the heap key is ill-formatted or does not
             correspond to a pre-configured heap.
    */
    auto GetHeapDescription(const std::string& heap_key) const -> const HeapDescription& override
    {
        // Parse view type from heap_key (format: ViewType:Visibility)
        auto pos = heap_key.find(':');
        if (pos == std::string::npos) {
            throw std::runtime_error("Invalid heap key format");
        }

        std::string view_type_str = heap_key.substr(0, pos);

        auto it = heaps_.find(view_type_str);
        if (it != heaps_.end()) {
            return it->second;
        }
        throw std::runtime_error("Heap description not found for view type: " + view_type_str);
    }

private:
    //! Initial capacities for different resource view types.
    std::unordered_map<std::string, HeapDescription> heaps_ = {
        // clang-format off
        { "Texture_SRV", HeapDescription { .cpu_visible_capacity = 10000, .shader_visible_capacity = 5000 } },
        { "Texture_UAV", HeapDescription { .cpu_visible_capacity = 5000, .shader_visible_capacity = 2500 } },
        { "TypedBuffer_SRV", HeapDescription { .cpu_visible_capacity = 2000, .shader_visible_capacity = 1000 } },
        { "TypedBuffer_UAV", HeapDescription { .cpu_visible_capacity = 2000, .shader_visible_capacity = 1000 } },
        { "StructuredBuffer_SRV", HeapDescription { .cpu_visible_capacity = 2000, .shader_visible_capacity = 1000 } },
        { "StructuredBuffer_UAV", HeapDescription { .cpu_visible_capacity = 2000, .shader_visible_capacity = 1000 } },
        { "RawBuffer_SRV", HeapDescription { .cpu_visible_capacity = 2000, .shader_visible_capacity = 1000 } },
        { "RawBuffer_UAV", HeapDescription { .cpu_visible_capacity = 2000, .shader_visible_capacity = 1000 } },
        { "ConstantBuffer", HeapDescription { .cpu_visible_capacity = 2000, .shader_visible_capacity = 1000 } },
        { "Sampler", HeapDescription { .cpu_visible_capacity = 2048, .shader_visible_capacity = 2048 } },
        { "SamplerFeedbackTexture_UAV", HeapDescription { .cpu_visible_capacity = 100, .shader_visible_capacity = 100 } },
        { "RayTracingAccelStructure", HeapDescription { .cpu_visible_capacity = 100, .shader_visible_capacity = 100 } },
        { "Texture_DSV", HeapDescription { .cpu_visible_capacity = 1024, .shader_visible_capacity = 0 } },
        { "Texture_RTV", HeapDescription { .cpu_visible_capacity = 1024, .shader_visible_capacity = 0 } },
        // clang-format on
    };
};

//! Abstract interface for descriptor allocation from heaps.
/*!
 Manages descriptor heaps of different types and visibility. Each descriptor
 type typically requires a separate heap allocation, and each heap has
 an associated visibility (shader-visible or CPU-only).

 In D3D12, this maps to descriptor heaps of different types (CBV_SRV_UAV, SAMPLER,
 RTV, DSV) each with a visibility flag. In Vulkan, this maps to descriptor pools
 that can contain mixed descriptor types.

 The allocator owns the descriptor heaps and is responsible for allocating,
 releasing, and optionally copying descriptors. It provides methods for obtaining
 platform-specific handles for descriptors and preparing resources for rendering.

 The allocator is responsible for managing the lifecycle of descriptors but not
 the resources they describe (textures, buffers, etc.).
*/
class OXYGEN_GFX_API DescriptorAllocator {
public:
    //! Represents an invalid descriptor index.
    static constexpr auto kInvalidIndex = std::numeric_limits<uint32_t>::max();

    virtual ~DescriptorAllocator() = default;

    //! Allocates a descriptor of the specified view type from the specified
    //! visibility.
    /*!
     \param view_type The resource view type to allocate (SRV, UAV, CBV, Sampler, RTV, DSV).
     \param visibility The memory visibility to allocate from (shader-visible or CPU-only).
     \return A handle to the allocated descriptor.
    */
    virtual DescriptorHandle Allocate(ResourceViewType view_type, DescriptorVisibility visibility) = 0;

    //! Releases a previously allocated descriptor.
    /*!
     \param handle The handle to release. After this call, the handle will be invalidated.
    */
    virtual void Release(DescriptorHandle& handle) = 0;

    //! Copies a descriptor from one visibility to another.
    /*!
     \param source The source descriptor handle.
     \param destination The destination descriptor handle.

     Source and destination must be of the same descriptor type but can be in
     different visibility spaces. Typically used to copy from CPU-only to shader-visible.
    */
    virtual void CopyDescriptor(const DescriptorHandle& source, const DescriptorHandle& destination) = 0;

    //! Gets a native platform-specific handle from a descriptor handle.
    /*!
     \param handle The descriptor handle.
     \return A NativeObject representing the platform-specific handle.

     The returned handle might be a D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE,
     or VkDescriptorSet, depending on the platform and the handle's visibility.
    */
    [[nodiscard]] virtual NativeObject GetNativeHandle(const DescriptorHandle& handle) const = 0;

    //! Prepares descriptors for rendering, binding necessary heaps.
    /*!
     \param command_list The command list to bind to (passed as NativeObject).

     This method is called before rendering to ensure all necessary descriptor
     heaps are properly bound to the command list or command buffer.
    */
    virtual void PrepareForRendering(const NativeObject& command_list) = 0;

    //! Returns the number of descriptors remaining of a specific view type in a specific visibility.
    /*!
     \param view_type The resource view type.
     \param visibility The memory visibility.
     \return The number of descriptors remaining.
    */
    [[nodiscard]] virtual uint32_t GetRemainingDescriptorsCount(
        ResourceViewType view_type, DescriptorVisibility visibility) const
        = 0;

    //! Checks if this allocator owns the given descriptor handle.
    /*!
     \param handle The descriptor handle to check.
     \return True if this allocator owns the handle, false otherwise.
    */
    [[nodiscard]] virtual bool Contains(const DescriptorHandle& handle) const = 0;

    //! Returns the number of allocated descriptors of a specific view type in a specific visibility.
    /*!
     \param view_type The resource view type.
     \param visibility The memory visibility.
     \return The number of allocated descriptors.
    */
    [[nodiscard]] virtual uint32_t GetAllocatedDescriptorsCount(ResourceViewType view_type, DescriptorVisibility visibility) const = 0;

protected:
    //! Protected method to create a descriptor handle instance. Provided for
    //! classes implementing this interface, which is the only one declared as
    //! a friend in DescriptorHandle.
    auto CreateDescriptorHandle(
        uint32_t index,
        ResourceViewType view_type,
        DescriptorVisibility visibility)
    {
        return DescriptorHandle { this, index, view_type, visibility };
    }
};

} // namespace oxygen::graphics
