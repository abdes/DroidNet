//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

class NativeObject;
class CommandRecorder;

//! Describes the properties of a descriptor heap or pool.
struct HeapDescription {
  //! Initial capacity for CPU-visible descriptors.
  bindless::Capacity cpu_visible_capacity { 0 };

  //! Initial capacity for shader-visible descriptors.
  bindless::Capacity shader_visible_capacity { 0 };

  //! Flag indicating if dynamic growth is allowed when heaps are full.
  bool allow_growth { true };

  //! Growth factor when expanding descriptor heaps.
  float growth_factor { 2.0f };

  //! Maximum number of growth iterations before failing allocations.
  uint32_t max_growth_iterations { 3 };

  static constexpr auto WithCapacity(bindless::Capacity::UnderlyingType cpu,
    bindless::Capacity::UnderlyingType gpu)
  {
    return HeapDescription {
      .cpu_visible_capacity = bindless::Capacity { cpu },
      .shader_visible_capacity = bindless::Capacity { gpu },
    };
  }
};

//! Interface for heap mapping strategy used by descriptor allocators.
class DescriptorAllocationStrategy {
public:
  DescriptorAllocationStrategy() = default;
  virtual ~DescriptorAllocationStrategy() = default;

  OXYGEN_DEFAULT_COPYABLE(DescriptorAllocationStrategy)
  OXYGEN_DEFAULT_MOVABLE(DescriptorAllocationStrategy)

  //! Returns a unique heap key string for a given view type and visibility.
  /*!
   May throw an exception if the view type or visibility is not recognized.
  */
  [[nodiscard]] virtual auto GetHeapKey(ResourceViewType view_type,
    DescriptorVisibility visibility) const -> std::string
    = 0;

  //! Returns the heap description for a given heap key.
  /*!
   May throw an exception if the key is not recognized.
  */
  [[nodiscard]] virtual auto GetHeapDescription(
    const std::string& heap_key) const -> const HeapDescription& = 0;

  //! Returns the base index for a heap (default 0 for backward compatibility).
  [[nodiscard]] virtual auto GetHeapBaseIndex(ResourceViewType view_type,
    DescriptorVisibility visibility) const -> bindless::Handle
    = 0;
};

//! Default heap mapping strategy: one heap per (view type, visibility) pair,
//! using some reasonable value for HeapDescription.
class DefaultDescriptorAllocationStrategy
  : public DescriptorAllocationStrategy {
public:
  DefaultDescriptorAllocationStrategy()
  {
    bindless::Handle::UnderlyingType current_base = 0;
    for (const auto& [view_type_str, desc] : heaps_) {
      for (const DescriptorVisibility vis : { DescriptorVisibility::kCpuOnly,
             DescriptorVisibility::kShaderVisible }) {
        std::string heap_key = view_type_str + ":"
          + (vis == DescriptorVisibility::kCpuOnly ? "cpu" : "gpu");
        const bindless::Capacity capacity
          = vis == DescriptorVisibility::kShaderVisible
          ? desc.shader_visible_capacity
          : desc.cpu_visible_capacity;
        if (capacity == bindless::Capacity { 0 }) {
          continue;
        }
        heap_base_indices_[heap_key] = bindless::Handle { current_base };
        current_base += capacity.get();
      }
    }
  }

  ~DefaultDescriptorAllocationStrategy() override = default;

  OXYGEN_DEFAULT_COPYABLE(DefaultDescriptorAllocationStrategy)
  OXYGEN_DEFAULT_MOVABLE(DefaultDescriptorAllocationStrategy)

  //! Returns a unique key formed by concatenating the view type and
  //! visibility, separated by a colon.
  /*!
   The view type part is guaranteed to be unique on its own, and can be used
   to index the heap descriptions table.

   \note It is not recommended to frequently call this function in a
         performance-critical path as it creates a new string each time.

   \return A unique key for the combination of view type and visibility, or
           "__Unknown__:__Unknown__" if the view type or visibility is not
           recognized as valid.
  */
  [[nodiscard]] auto GetHeapKey(ResourceViewType view_type,
    DescriptorVisibility visibility) const -> std::string override
  {
    std::string view_type_str;
    switch (view_type) { // NOLINT(clang-diagnostic-switch-enum)
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
    switch (visibility) { // NOLINT(clang-diagnostic-switch-enum)
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
  [[nodiscard]] auto GetHeapDescription(const std::string& heap_key) const
    -> const HeapDescription& override
  {
    // Parse view type from heap_key (format: ViewType:Visibility)
    const auto pos = heap_key.find(':');
    if (pos == std::string::npos) {
      throw std::runtime_error("Invalid heap key format");
    }

    const std::string view_type_str = heap_key.substr(0, pos);

    if (const auto it = heaps_.find(view_type_str); it != heaps_.end()) {
      return it->second;
    }
    throw std::runtime_error(
      "Heap description not found for view type: " + view_type_str);
  }

  //! Allow specifying base indices for heaps (default 0, but can be extended).
  [[nodiscard]] auto GetHeapBaseIndex(const ResourceViewType view_type,
    const DescriptorVisibility visibility) const -> bindless::Handle override
  {
    if (const auto it
      = heap_base_indices_.find(GetHeapKey(view_type, visibility));
      it != heap_base_indices_.end()) {
      return it->second;
    }
    return bindless::Handle { 0 };
  }

private:
  //! Initial capacities for different resource view types.
  std::unordered_map<std::string, HeapDescription> heaps_ = {
    { "Texture_SRV", HeapDescription::WithCapacity(10000, 5000) },
    { "Texture_UAV", HeapDescription::WithCapacity(5000, 2500) },
    { "TypedBuffer_SRV", HeapDescription::WithCapacity(2000, 1000) },
    { "TypedBuffer_UAV", HeapDescription::WithCapacity(2000, 1000) },
    { "StructuredBuffer_SRV", HeapDescription::WithCapacity(2000, 1000) },
    { "StructuredBuffer_UAV", HeapDescription::WithCapacity(2000, 1000) },
    { "RawBuffer_SRV", HeapDescription::WithCapacity(2000, 1000) },
    { "RawBuffer_UAV", HeapDescription::WithCapacity(2000, 1000) },
    { "ConstantBuffer", HeapDescription::WithCapacity(2000, 1000) },
    { "Sampler", HeapDescription::WithCapacity(2048, 2048) },
    { "SamplerFeedbackTexture_UAV", HeapDescription::WithCapacity(100, 100) },
    { "RayTracingAccelStructure", HeapDescription::WithCapacity(100, 100) },
    { "Texture_DSV", HeapDescription::WithCapacity(1024, 0) },
    { "Texture_RTV", HeapDescription::WithCapacity(1024, 0) },
  };

  //! Base indices for different heaps.
  std::unordered_map<std::string, bindless::Handle> heap_base_indices_ {};
};

//! Abstract interface for descriptor allocation from heaps.
/*!
 Manages descriptor heaps of different types and visibility. Each descriptor
 type typically requires a separate heap allocation, and each heap has an
 associated visibility (shader-visible or CPU-only).

 In D3D12, this maps to descriptor heaps of different types (CBV_SRV_UAV,
 SAMPLER, RTV, DSV) each with a visibility flag. In Vulkan, this maps to
 descriptor pools that can contain mixed descriptor types.

 The allocator owns the descriptor heaps and is responsible for allocating,
 releasing, and optionally copying descriptors. It provides methods for
 obtaining platform-specific handles for descriptors and preparing resources for
 rendering.

 The allocator is responsible for managing the lifecycle of descriptors but not
 the resources they describe (textures, buffers, etc.).
*/
class OXGN_GFX_API DescriptorAllocator {
public:
  DescriptorAllocator() = default;
  virtual ~DescriptorAllocator() = default;

  OXYGEN_DEFAULT_COPYABLE(DescriptorAllocator)
  OXYGEN_DEFAULT_MOVABLE(DescriptorAllocator)

  //! Allocates a descriptor of the specified view type from the specified
  //! visibility.
  /*!
   \param view_type The resource view type to allocate (SRV, UAV, CBV, Sampler,
   RTV, DSV).
   \param visibility The memory visibility to allocate from (shader-visible or
   CPU-only).
   \return A handle to the allocated descriptor.
  */
  virtual auto Allocate(ResourceViewType view_type,
    DescriptorVisibility visibility) -> DescriptorHandle
    = 0;

  //! Releases a previously allocated descriptor.
  /*!
   \param handle The handle to release. After this call, the handle will be
   invalidated.
  */
  virtual auto Release(DescriptorHandle& handle) -> void = 0;

  //! Copies a descriptor from one visibility to another.
  /*!
   \param source The source descriptor handle.
   \param destination The destination descriptor handle.

   Source and destination must be of the same descriptor type but can be in
   different visibility spaces. Typically used to copy from CPU-only to
   shader-visible.
  */
  virtual auto CopyDescriptor(
    const DescriptorHandle& source, const DescriptorHandle& destination) -> void
    = 0;

  //! Returns the number of descriptors remaining of a specific view type in a
  //! specific visibility.
  /*!
   \param view_type The resource view type.
   \param visibility The memory visibility.
   \return The number of descriptors remaining.
  */
  [[nodiscard]] virtual auto GetRemainingDescriptorsCount(
    ResourceViewType view_type, DescriptorVisibility visibility) const
    -> bindless::Count
    = 0;

  //! Returns the global base index for a (view_type, visibility) domain.
  /*!
   This value is stable and derived from the allocation strategy. Shaders
   compute the final bindless index as: base + local_slot. Use this to
   validate ranges and to pre-compute indices before any allocation occurs.

   \param view_type The resource view type.
   \param visibility The memory visibility.
   \return The domain's global base index.
  */
  [[nodiscard]] virtual auto GetDomainBaseIndex(ResourceViewType view_type,
    DescriptorVisibility visibility) const -> bindless::Handle
    = 0;

  //! Attempts to reserve capacity in a domain and returns its base index.
  /*!
   Reservation validates that the requested count fits the configured domain
   capacity. Implementations may pre-create backing segments but are not
   required to. On success, returns the domain base index; on failure,
   returns std::nullopt.

   \param view_type The resource view type.
   \param visibility The memory visibility.
   \param count Number of descriptors to budget for this domain.
   \return Domain base index on success, std::nullopt otherwise.
  */
  [[nodiscard]] virtual auto Reserve(ResourceViewType view_type,
    DescriptorVisibility visibility, bindless::Count count)
    -> std::optional<bindless::Handle>
    = 0;

  //! Checks if this allocator owns the given descriptor handle.
  /*!
   \param handle The descriptor handle to check.
   \return True if this allocator owns the handle, false otherwise.
  */
  [[nodiscard]] virtual auto Contains(const DescriptorHandle& handle) const
    -> bool
    = 0;

  //! Returns the number of allocated descriptors of a specific view type in a
  //! specific visibility.
  /*!
   \param view_type The resource view type.
   \param visibility The memory visibility.
   \return The number of allocated descriptors.
  */
  [[nodiscard]] virtual auto GetAllocatedDescriptorsCount(
    ResourceViewType view_type, DescriptorVisibility visibility) const
    -> bindless::Count
    = 0;

  //! Returns the shader-visible bindless index for a descriptor allocated by
  //! this allocator.
  /*!
   The mapping from a global `DescriptorHandle` to a shader-visible index is
   allocator- and backend-specific. Callers must use this method instead of
   attempting to derive the index themselves.

   @param handle The descriptor handle to map to a shader-visible index.
   @return The shader-visible index into the bindless table for the resource
   type associated with \p handle, or `kInvalidShaderVisibleIndex` if \p
   handle is invalid or not owned by this allocator.
  */
  [[nodiscard]] virtual auto GetShaderVisibleIndex(
    const DescriptorHandle& handle) const noexcept
    -> bindless::ShaderVisibleIndex
    = 0;

protected:
  //! Protected method to create a descriptor handle instance. Provided for
  //! classes implementing this interface, which is the only one declared as
  //! a friend in DescriptorHandle.
  auto CreateDescriptorHandle(const bindless::Handle index,
    const ResourceViewType view_type, const DescriptorVisibility visibility)
  {
    return DescriptorHandle { this, index, view_type, visibility };
  }
};

} // namespace oxygen::graphics
