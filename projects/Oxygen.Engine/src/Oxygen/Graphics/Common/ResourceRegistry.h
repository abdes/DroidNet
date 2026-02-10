//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <any>
#include <concepts>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Concepts.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

namespace detail {
  template <typename Resource, typename Fn>
  concept ViewUpdaterCallable = std::is_same_v<std::decay_t<Fn>, std::nullptr_t>
    || requires(Fn&& fn, const typename Resource::ViewDescriptionT& desc) {
         {
           fn(desc)
         } -> std::convertible_to<
           std::optional<typename Resource::ViewDescriptionT>>;
       };
} // namespace detail

//! Thread-safe registry for graphics resources and bindless rendering views.
/*!
 ResourceRegistry is the central component for managing graphics resources
 (textures, buffers, samplers) and their associated views in the Oxygen graphics
 system. It provides comprehensive lifecycle management with strong reference
 semantics, thread-safe operations, and optimized view caching for bindless
 rendering architectures.

 ### Key Features

 - **Bindless Rendering Support**: Resources are accessed via global indices
   rather than per-draw bindings, enabling efficient GPU-driven rendering.
 - **View Caching**: Automatically caches native views based on resource and
   description hash, avoiding redundant view creation.
 - **Thread Safety**: All operations are protected by internal mutex, enabling
   safe concurrent access from multiple threads.
 - **Stable Descriptor Indices**: UpdateView and Replace operations preserve
   bindless indices where possible, maintaining shader compatibility.
 - **Strong Reference Management**: Registry holds shared_ptr references to
   resources, ensuring they remain valid while registered.

 ### Resource Types

 - **Simple Resources**: Samplers and other resources without views - only the
   resource itself is registered and tracked.
 - **Resources with Views**: Textures and buffers that support multiple view
   types (SRV, UAV, CBV, etc.) with descriptor handle management.

 ### Architecture Patterns

 - **Handle/View Pattern**: Resources are accessed through NativeResource
   handles and NativeView objects for type safety and abstraction.
 - **Type-Safe Templates**: Operations are constrained by SupportedResource and
   ResourceWithViews concepts for compile-time safety.
 - **Composition-Based**: Uses TypeId-based resource identification rather than
   RTTI for performance and flexibility.

 ### Usage Patterns

 ```cpp
 // Register a resource
 registry.Register(my_texture);

 // Register a view with descriptor
 auto desc = TextureViewDesc{...};
 auto handle = allocator->Allocate(desc.view_type, desc.visibility);
 auto view = registry.RegisterView(*my_texture, std::move(handle), desc);

 // Update view in-place (keeps same bindless index)
 auto new_desc = TextureViewDesc{...};
 registry.UpdateView(*my_texture, index, new_desc);

 // Replace resource with transformation
 registry.Replace(*old_texture, new_texture,
   [](const auto& old_desc) { return transform(old_desc); });
 ```

 ### Performance Characteristics

 - **Registration**: O(1) average case with hash table lookup
 - **View Caching**: O(1) cache hit, O(resource_creation) on cache miss
 - **Update/Replace**: O(N) for N views on the resource
 - **Thread Contention**: Single mutex protects all operations - consider
   operation batching for high-contention scenarios

 ### Error Handling

 - **Graceful Degradation**: Invalid view creation returns invalid NativeView
   rather than throwing, allowing fallback strategies.
 - **Resource Validation**: Comprehensive validation with descriptive error
   messages for debugging.
 - **Exception Safety**: Strong exception safety guarantees - failed operations
   leave registry in consistent state.

 ### Critical Contract Violations (Program Termination)

 ResourceRegistry enforces strict contracts through runtime assertions that will
 **abort the program** when violated. These are considered programming errors
 and indicate incorrect API usage:

 - **Duplicate Resource Registration**: Calling Register() on an already
   registered resource instance will abort the program. Use Replace() instead.
 - **Duplicate View Registration**: Registering identical views (same resource
   + description) will abort the program. Use UpdateView() instead.
 - **Invalid Descriptor Handles**: Passing invalid DescriptorHandle objects will
   abort the program in all view operations.
 - **Null Resource Registration**: Attempting to register null resources will
   abort the program.

 These contracts ensure registry integrity and prevent subtle bugs in production
 code.

 @warning All template parameters must satisfy their respective concepts
          (SupportedResource, ResourceWithViews) for correct operation.
 @see DescriptorHandle, NativeResource, NativeView
*/
class ResourceRegistry {
public:
  OXGN_GFX_API explicit ResourceRegistry(std::string_view debug_name);

  OXGN_GFX_API virtual ~ResourceRegistry() noexcept;

  OXYGEN_MAKE_NON_COPYABLE(ResourceRegistry)
  OXYGEN_DEFAULT_MOVABLE(ResourceRegistry)

  //! Register a graphics resource for lifecycle management and view operations.
  /*!
   Registers a graphics resource (texture, buffer, sampler, etc.) in the
   registry, establishing a strong reference that keeps the resource alive until
   explicitly unregistered. This is a prerequisite for all view-related
   operations on the resource.

   The registry maintains a shared_ptr to the resource, ensuring it remains
   valid throughout its registered lifetime. For resources that support views
   (textures, buffers), this registration enables subsequent RegisterView,
   UpdateView, and Replace operations. For simple resources like samplers, only
   the resource itself is tracked.

   ### Registration Semantics

   - **Strong Reference**: Registry holds shared_ptr, preventing resource
     destruction while registered.
   - **Type Safety**: Template parameter constrains to SupportedResource types
     at compile time.
   - **Identity**: Resources are identified by their memory address and TypeId
     for efficient lookup and collision detection.
   - **Uniqueness**: Each resource instance can only be registered once.

   ### Performance Characteristics

   - Time Complexity: O(1) average case hash table insertion
   - Memory: Adds one registry entry plus shared_ptr overhead
   - Thread Safety: Fully thread-safe with internal synchronization

   ### Critical Contracts

   - **No Duplicate Registration**: Attempting to register the same resource
     instance twice will abort the program. Use Replace() if you need to swap
     resources at the same identity.

   ### Usage Examples

   ```cpp
   auto texture = std::make_shared<Texture>(device, texture_desc);
   registry.Register(texture);  // Now available for view operations

   auto buffer = std::make_shared<Buffer>(device, buffer_desc);
   registry.Register(buffer);   // Ready for view registration
   ```

   @tparam Resource The resource type, must satisfy SupportedResource concept
   @param resource Shared pointer to the resource to register. Must be valid.

   @throw std::runtime_error if the resource is already registered in this
          registry instance.

   @see UnRegisterResource, RegisterView
  */
  template <SupportedResource Resource>
  auto Register(const std::shared_ptr<Resource>& resource) -> void
  {
    Register(std::static_pointer_cast<void>(resource), Resource::ClassTypeId());
  }

  //! Register a view for bindless rendering with automatic view creation.
  /*!
   Creates and registers a native view for a graphics resource using the
   resource's GetNativeView method, then caches it for efficient bindless
   rendering. This is the primary method for establishing resource views with
   descriptor handles in the bindless architecture.

   The registry calls the resource's GetNativeView method to create the
   platform-specific view object, then associates it with the provided
   descriptor handle for shader access. Views are cached based on the
   combination of resource identity and view description hash, enabling
   efficient reuse when identical views are requested.

   ### View Creation and Caching

   - **Automatic Creation**: Calls resource.GetNativeView(handle, desc) to
     create platform-specific view objects.
   - **Intelligent Caching**: Caches views by (resource, description_hash) key
     to avoid redundant creation.
   - **Bindless Integration**: Associates view with descriptor handle for
     shader-visible bindless access.
   - **Validation**: Verifies view validity and handle compatibility before
     registration.

   ### Descriptor Handle Lifecycle

   - **Ownership Transfer**: Registry takes ownership of the descriptor handle
     and manages its lifecycle.
   - **Automatic Release**: Handle is released when view is unregistered or
     resource is removed.
   - **Bindless Mapping**: Handle's bindless index becomes the shader-visible
     access point for this view.

   ### Performance Characteristics

   - Time Complexity: O(1) cache hit, O(view_creation) cache miss
   - Memory: Adds cache entry plus native view overhead
   - Optimization: Cache eliminates redundant view creation for identical
     descriptions

   ### Critical Contracts

   - **Valid Descriptor Handle Required**: Passing an invalid DescriptorHandle
     will abort the program. Handle must be properly allocated.
   - **No Duplicate Views**: Registering a view with identical description for
     the same resource will abort the program. Use UpdateView() to modify
     existing views.

   ### Usage Examples

   ```cpp
   // Create a shader resource view
   TextureViewDesc srv_desc {
     .view_type = ResourceViewType::kShaderResource,
     .visibility = DescriptorVisibility::kShaderVisible,
     .format = Format::kR8G8B8A8_UNORM
   };
   auto handle = allocator->Allocate(srv_desc.view_type, srv_desc.visibility);
   auto view = registry.RegisterView(*texture, std::move(handle), srv_desc);

   // Create a UAV for compute shaders
   TextureViewDesc uav_desc {
     .view_type = ResourceViewType::kUnorderedAccess,
     .visibility = DescriptorVisibility::kShaderVisible,
     .mip_level = 0
   };
   auto uav_handle = allocator->Allocate(uav_desc.view_type,
   uav_desc.visibility); auto uav_view = registry.RegisterView(*texture,
   std::move(uav_handle), uav_desc);
   ```

   @tparam Resource The resource type, must satisfy ResourceWithViews concept
   @param resource The resource to create a view for. Must be registered.
   @param view_handle Descriptor handle for the view. Must be valid and
          compatible with the resource type. Ownership transfers to registry.
   @param desc View description specifying format, type, and access patterns.
          Must be hashable and comparable.

   @return Handle to the native view object (platform-specific). Returns invalid
           NativeView if view creation fails.

   @throw std::runtime_error if the resource is not registered, or if a view
          with the same description already exists for this resource.

   @see RegisterView(Resource&, NativeView, DescriptorHandle, ViewDescriptionT),
        UpdateView, UnRegisterView
  */
  template <ResourceWithViews Resource>
  auto RegisterView(Resource& resource, DescriptorHandle view_handle,
    const typename Resource::ViewDescriptionT& desc) -> NativeView
  {
    auto view = resource.GetNativeView(view_handle, desc);
    auto key = std::hash<std::remove_cvref_t<decltype(desc)>> {}(desc);
    return RegisterView(NativeResource { &resource, Resource::ClassTypeId() },
      std::move(view), std::move(view_handle), std::any(desc), key,
      desc.view_type, desc.visibility);
  }

  //! Register a pre-created view for advanced control over view lifecycle.
  /*!
   Registers an already-created native view object for a graphics resource,
   providing complete control over the view creation process. This method is
   intended for advanced scenarios where custom view creation logic is required,
   or when integrating with external graphics APIs that provide pre-created view
   objects.

   Unlike the primary RegisterView method, this variant does not call the
   resource's GetNativeView method. Instead, it directly registers the provided
   native view object, enabling custom view creation workflows while still
   benefiting from the registry's caching and lifecycle management.

   ### Advanced Use Cases

   - **Custom View Creation**: When resource's GetNativeView doesn't provide
     sufficient control over view parameters.
   - **External Integration**: Registering views created by external graphics
     libraries or frameworks.
   - **Performance Optimization**: Pre-creating views in batch operations or
     background threads.
   - **Testing and Mocking**: Injecting test doubles or mock view objects for
     unit testing.

   ### Validation and Safety

   - **View Validity**: Validates that the native view object is valid before
     registration.
   - **Uniqueness Check**: Ensures no conflicting view with the same description
     already exists.
   - **Resource Verification**: Confirms the target resource is registered
     before accepting the view.
   - **Handle Compatibility**: Verifies descriptor handle matches view type and
     visibility requirements.

   ### Performance Characteristics

   - Time Complexity: O(1) for registration, no view creation overhead
   - Memory: Minimal overhead for cache entry and handle management
   - Optimization: Bypasses resource's GetNativeView call for maximum
     performance when view is already available

   ### Critical Contracts

   - **Valid Descriptor Handle Required**: Passing an invalid DescriptorHandle
     will abort the program. Handle must be properly allocated.
   - **No Duplicate Views**: Registering a view with identical description for
     the same resource will abort the program. Use UpdateView() to modify
     existing views.

   ### Usage Examples

   ```cpp
   // Custom view creation with specific parameters
   auto custom_view = CreateCustomTextureView(d3d_texture, custom_desc);
   auto handle = allocator->Allocate(desc.view_type, desc.visibility);
   bool success = registry.RegisterView(*texture, custom_view,
                                       std::move(handle), desc);

   // Batch view registration from external source
   for (const auto& [view_obj, desc] : external_views) {
     auto handle = allocator->Allocate(desc.view_type, desc.visibility);
     registry.RegisterView(*resource, view_obj, std::move(handle), desc);
   }
   ```

   @tparam Resource The resource type, must satisfy ResourceWithViews concept
   @param resource The resource to associate the view with. Must be registered.
   @param view The pre-created native view object. Must be valid.
   @param view_handle Descriptor handle for the view. Must be valid and
          compatible. Ownership transfers to registry.
   @param desc View description for caching and validation. Must match the
          view's actual properties.

   @return true if the view was registered successfully, false if the resource
           or view is invalid.

   @throw std::runtime_error if the resource is not registered, or if a view
          with the same description already exists for this resource.

   @warning Ensure the provided view object matches the description exactly, as
            the registry cannot validate this correspondence.

   @see RegisterView(Resource&, DescriptorHandle, ViewDescriptionT), UpdateView,
        UnRegisterView
  */
  template <ResourceWithViews Resource>
  auto RegisterView(Resource& resource, NativeView view,
    DescriptorHandle view_handle,
    const typename Resource::ViewDescriptionT& desc) -> bool
  {
    auto key = std::hash<std::remove_cvref_t<decltype(desc)>> {}(desc);
    return RegisterView(NativeResource { &resource, Resource::ClassTypeId() },
      std::move(view), std::move(view_handle), std::any(desc), key,
      desc.view_type, desc.visibility)
      ->IsValid();
  }

  //! Update a view in-place while preserving its bindless descriptor index.
  /*!
   Replaces an existing view at a specific bindless descriptor index with a new
   view created from the provided resource and description. This operation is
   crucial for maintaining stable bindless handles in shaders while allowing
   view properties to change dynamically.

   The method preserves the shader-visible bindless index, ensuring that
   existing shader code continues to work without recompilation. If the
   descriptor was previously owned by a different resource, ownership is
   transferred to the new resource seamlessly.

   ### Bindless Stability Guarantees

   - **Stable Indices**: The bindless descriptor index remains unchanged,
     preserving shader compatibility.
   - **Seamless Transitions**: GPU can continue accessing the resource through
     the same index without interruption.
   - **Cache Consistency**: View cache is updated to reflect the new resource
     and description mapping.
   - **Ownership Transfer**: Supports transferring views between different
     resource instances at the same index.

   ### Update Semantics

   - **In-Place Replacement**: Creates new view and replaces the existing one
     atomically from the registry's perspective.
   - **Resource Ownership**: Updates internal mappings to associate the index
     with the new resource.
   - **Cache Invalidation**: Removes old cache entries and adds new ones
     reflecting the updated view.
   - **Descriptor Preservation**: Reuses the existing descriptor handle,
     avoiding allocation/deallocation overhead.

   ### Failure Handling

   If view creation fails after acquiring the descriptor from the previous
   owner, the descriptor handle is released and the index becomes free. This
   matches Replace() behavior and ensures no resource leaks or invalid states.

   ### Performance Characteristics

   - Time Complexity: O(1) for the update operation itself, plus view creation
     cost
   - Memory: No additional descriptor allocation, minimal cache overhead
   - Optimization: Reuses existing descriptor handle, avoiding allocator
     round-trips

   ### Usage Examples

   ```cpp
   // Dynamic LOD updates - same texture, different mip level
   TextureViewDesc hq_desc { .mip_level = 0, .view_type =
   ResourceViewType::kShaderResource }; TextureViewDesc lq_desc { .mip_level =
   2, .view_type = ResourceViewType::kShaderResource };

   auto view = registry.RegisterView(*texture, handle, hq_desc);
   auto index = handle.GetBindlessHandle();

   // Later, update to lower quality without changing shader bindings
   registry.UpdateView(*texture, index, lq_desc);

   // Transfer ownership to different resource
   registry.UpdateView(*new_texture, index, hq_desc);
   ```

   @tparam Resource The resource type, must satisfy ResourceWithViews concept
   @param resource The resource that will own the updated view. Must be
          registered in the registry.
   @param index The bindless descriptor index to update. Must correspond to an
          existing registered view.
   @param desc The new view description for the updated view.

   @return true if the view was updated successfully, false if the operation
           failed (resource not registered, invalid index, view creation
           failure, etc.).

   @note If view creation fails, the descriptor is released and the index
         becomes free, requiring re-registration for future use.

   @see RegisterView, Replace, UnRegisterView
  */
  template <ResourceWithViews Resource>
  auto UpdateView(Resource& resource, bindless::HeapIndex index,
    const typename Resource::ViewDescriptionT& desc) -> bool
  {
    std::lock_guard lock(registry_mutex_);

    const auto key_hash
      = std::hash<std::remove_cvref_t<decltype(desc)>> {}(desc);

    // Ensure destination resource is registered
    const NativeResource new_res_obj { &resource, Resource::ClassTypeId() };
    const auto new_res_it = resources_.find(new_res_obj);
    if (new_res_it == resources_.end()) {
      return false;
    }

    // Find existing owner and take ownership of the descriptor handle entry
    DescriptorHandle owned_descriptor;
    NativeView old_view_obj; // for cache purge
    NativeResource old_res_obj; // original owner
    // Track prior cache key (by hash) if available to perform precise purge
    std::optional<std::size_t> prior_desc_hash;
    if (const auto owner_it = descriptor_to_resource_.find(index);
      owner_it != descriptor_to_resource_.end()) {
      old_res_obj = owner_it->second;
      if (const auto old_res_it = resources_.find(old_res_obj);
        old_res_it != resources_.end()) {
        auto& old_descriptors = old_res_it->second.descriptors;
        if (const auto ve_it = old_descriptors.find(index);
          ve_it != old_descriptors.end()) {
          old_view_obj = ve_it->second.view_object;
          owned_descriptor = std::move(ve_it->second.descriptor);
          old_descriptors.erase(ve_it);
          // Clear mapping while we attempt update; will be re-added on success
          descriptor_to_resource_.erase(index);

          // Attempt to find the prior cache entry's key hash for precise erase
          if (old_view_obj->IsValid()) {
            for (const auto& [cache_key, entry] : view_cache_) {
              if (cache_key.resource == old_res_obj
                && entry.view_object == old_view_obj) {
                prior_desc_hash = cache_key.view_desc_hash;
                break;
              }
            }
          }
        } else {
          // Inconsistent state: owner resource has no view entry for index.
          // Programming error; self-heal by erasing stale mapping and fail.
          DCHECK_F(false, "UpdateView: missing view entry for index");
          // ReSharper disable once CppUnreachableCode
          descriptor_to_resource_.erase(index);
          return false;
        }
      } else {
        // Inconsistent state: mapped owner resource missing from registry.
        // Programming error; self-heal by erasing stale mapping and fail.
        DCHECK_F(false, "UpdateView: owner resource not registered");
        // ReSharper disable once CppUnreachableCode
        descriptor_to_resource_.erase(index);
        return false;
      }
    } else {
      // Unknown index
      return false;
    }

    // Create the new native view at the same descriptor slot using the owned
    // descriptor handle.
    NativeView new_view;
    try {
      new_view = resource.GetNativeView(owned_descriptor, desc);
    } catch (...) {
      // Failure -> release the temporary descriptor and purge old cache; the
      // index becomes free and no registration remains for it.
      if (owned_descriptor.IsValid()) {
        owned_descriptor.Release();
      }
      // Remove any cache entry for the old view using the prior key when known
      if (old_view_obj->IsValid()) {
        if (prior_desc_hash.has_value()) {
          const CacheKey prior_key { .resource = old_res_obj,
            .view_desc_hash = *prior_desc_hash };
          view_cache_.erase(prior_key);
        } else {
          std::erase_if(view_cache_, [&](const auto& it) {
            return it.first.resource == old_res_obj
              && it.second.view_object == old_view_obj;
          });
        }
      }
      throw; // Propagate
    }
    if (!new_view->IsValid()) {
      // Failure -> release the temporary descriptor and purge old cache; the
      // index becomes free and no registration remains for it.
      if (owned_descriptor.IsValid()) {
        owned_descriptor.Release();
      }
      // Remove any cache entry for the old view object if present.
      if (old_view_obj->IsValid()) {
        if (prior_desc_hash.has_value()) {
          const CacheKey prior_key { .resource = old_res_obj,
            .view_desc_hash = *prior_desc_hash };
          view_cache_.erase(prior_key);
        } else {
          std::erase_if(view_cache_, [&](const auto& it) {
            return it.first.resource == old_res_obj
              && it.second.view_object == old_view_obj;
          });
        }
      }
      return false;
    }

    // Attach descriptor to the new resource and update caches/mappings.
    auto& new_descriptors = new_res_it->second.descriptors;
    new_descriptors[index] = ResourceEntry::ViewEntry {
      .view_object = new_view,
      .descriptor = std::move(owned_descriptor),
    };
    descriptor_to_resource_[index] = new_res_obj;

    // Update cache entry: erase prior by key first, then insert the new entry
    if (old_view_obj->IsValid()) {
      if (prior_desc_hash.has_value()) {
        const CacheKey prior_key { .resource = old_res_obj,
          .view_desc_hash = *prior_desc_hash };
        view_cache_.erase(prior_key);
      } else {
        std::erase_if(view_cache_, [&](const auto& it) {
          return it.first.resource == old_res_obj
            && it.second.view_object == old_view_obj;
        });
      }
    }

    // Insert/overwrite new cache entry
    ViewCacheEntry cache_entry {
      .view_object = new_view,
      .view_description = std::any(desc),
    };
    const CacheKey new_cache_key {
      .resource = new_res_obj,
      .view_desc_hash = key_hash,
    };
    view_cache_[new_cache_key] = std::move(cache_entry);
    // Diagnostic: log repointing for runtime validation of descriptor updates
    DLOG_F(2, "ResourceRegistry::UpdateView: repointed index {} to {}", index,
      new_res_obj);
    return true;
  }

  //! Replace a resource with sophisticated view transformation capabilities.
  /*!
   Atomically replaces one registered resource with another, providing powerful
   control over how existing views are handled during the transition. This
   operation is essential for scenarios like resource streaming, format changes,
   or dynamic resource swapping while maintaining bindless stability.

   Two distinct modes are supported through the updater parameter:

   ### Mode 1: Transform-and-Recreate (Updater Provided)

   For each existing view of the old resource, the updater function is called
   with the view's description. Based on the updater's return value:
   - **Description Returned**: View is recreated for the new resource at the
     same bindless index, preserving shader compatibility.
   - **std::nullopt Returned**: View is discarded and its descriptor handle is
     released, freeing the bindless index.
   - **Exception Thrown**: Registry safely discards the View, with proper
     cleanup.

   ### Mode 2: Complete Release (nullptr Updater)

   All views and descriptor handles of the old resource are released. The new
   resource is registered but starts with no views. This mode is equivalent to
   UnRegisterResource(old) + Register(new).

   ### Transformation Capabilities

   The updater function enables sophisticated view transformations:
   - **Format Changes**: Update texture format while preserving view types
   - **LOD Adjustments**: Change mip levels or array slices dynamically
   - **Selective Migration**: Choose which views to transfer vs. discard
   - **Conditional Logic**: Apply complex policies based on view properties

   ### Atomicity and Safety

   - **Exception Safety**: Failed view creation results in clean descriptor
     release, never leaving dangling or corrupted state.
   - **Atomic Registration**: New resource is registered before any view
     operations begin.
   - **Consistent State**: Registry remains fully consistent regardless of
     individual view creation success/failure.
   - **Thread Safety**: Entire operation is protected by internal mutex.

   ### Performance Characteristics

   - Time Complexity: O(N) for N views on the old resource, plus view creation
     overhead for successful transformations
   - Memory: O(N) transient memory for snapshotting descriptor indices; no
     permanent growth beyond new resource entry
   - Optimization: Reuses existing descriptor handles for recreated views,
     avoiding allocator round-trips

   ### Usage Examples

   ```cpp
   // Format conversion with selective view migration
   registry.Replace(*old_texture, new_texture,
     [](const TextureViewDesc& desc) -> std::optional<TextureViewDesc> {
       if (desc.view_type == ResourceViewType::kShaderResource) {
         auto new_desc = desc;
         new_desc.format = Format::kBC7_UNORM;  // Compress format
         return new_desc;
       }
       return std::nullopt;  // Discard UAVs, keep only SRVs
     });

   // Dynamic LOD switching for performance
   registry.Replace(*high_res_texture, low_res_texture,
     [](const TextureViewDesc& desc) -> std::optional<TextureViewDesc> {
       auto lod_desc = desc;
       lod_desc.mip_level = std::min(desc.mip_level + 2, max_mips - 1);
       return lod_desc;
     });

   // Complete resource swap with cleanup
   registry.Replace(*old_buffer, new_buffer, nullptr);
   ```

   @tparam Resource A resource type that satisfies ResourceWithViews concept
   @tparam ViewUpdater Callable matching ViewUpdaterCallable concept, or
           std::nullptr_t for release-all mode

   @param old_resource The currently registered resource being replaced. Must be
          registered in this registry.
   @param new_resource The replacement resource. Will be registered if not
          already present.
   @param update_fn Transformation policy for existing views. Called with each
          view's description; return new description to recreate view at same
          index, or std::nullopt to release the descriptor.

   @throw std::runtime_error if old_resource is not registered in this registry.

   @warning Updater exceptions are caught and treated as std::nullopt return
            (view discarded). Ensure updater provides strong exception safety.

   @see UpdateView, UnRegisterResource, RegisterView
  */
  template <ResourceWithViews Resource, typename ViewUpdater = std::nullptr_t>
    requires detail::ViewUpdaterCallable<Resource, ViewUpdater>
  auto Replace(const Resource& old_resource,
    std::shared_ptr<Resource> new_resource, ViewUpdater&& update_fn) -> void
  {
    // Perfect-forward the updater callable (or nullptr) to a local so that we
    // do not have that linter warning about it not being forwarded or moved.
    const auto& updater = std::forward<ViewUpdater>(update_fn);

    std::lock_guard lock(registry_mutex_);

    const NativeResource old_obj { const_cast<Resource*>(&old_resource),
      Resource::ClassTypeId() };
    const auto old_it = resources_.find(old_obj);
    if (old_it == resources_.end()) {
      throw std::runtime_error(
        "ResourceRegistry::Replace: old resource not registered");
    }

    DLOG_SCOPE_FUNCTION(2);

    const NativeResource new_obj { new_resource.get(),
      Resource::ClassTypeId() };
    auto new_it = resources_.find(new_obj);
    if (new_it == resources_.end()) {
      // Inline registration to avoid re-entrant lock in Register().
      ResourceEntry new_entry { .resource
        = std::static_pointer_cast<void>(new_resource),
        .descriptors = {} };
      resources_.emplace(new_obj, std::move(new_entry));
      new_it = resources_.find(new_obj);
    }
    DLOG_F(2, "replaced resource {} with {}", old_obj, new_obj);

    // No updater => release all views/handles of the old resource.
    if constexpr (std::is_same_v<std::decay_t<ViewUpdater>, std::nullptr_t>) {
      // Release all descriptors and associated cache entries for old_resource.
      UnRegisterResourceViewsNoLock(old_obj);
      // Remove the old resource entry and purge any remaining cached views.
      resources_.erase(old_it);
      PurgeCachedViewsForResource(old_obj);
      return;
    }
    // Updater provided => attempt to recreate views in-place.
    else {
      // Snapshot indices before we mutate the map to preserve iteration
      // guarantees while moving entries between maps.
      std::vector<bindless::HeapIndex> indices
        = CollectDescriptorIndicesForResource(old_obj);

      // Helper to find cached description for a given view object.
      const auto find_desc_any
        = [&](const NativeView& view_obj) -> std::optional<std::any> {
        for (const auto& [key, entry] : view_cache_) {
          if (key.resource == old_obj && entry.view_object == view_obj) {
            return entry.view_description;
          }
        }
        return std::nullopt;
      };

      for (const auto index : indices) {
        auto ve_it = old_it->second.descriptors.find(index);
        if (ve_it == old_it->second.descriptors.end()) {
          continue;
        }

        DescriptorHandle owned_descriptor = std::move(ve_it->second.descriptor);
        const NativeView view_obj = ve_it->second.view_object;

        DLOG_F(2, "replacing view: {}. {}", view_obj, owned_descriptor);

        old_it->second.descriptors.erase(ve_it);
        // Clear any owner mapping for this index; it will be re-added if we
        // successfully recreate the view for the new resource.
        descriptor_to_resource_.erase(index);

        bool recreated = false;
        // Apply updater policy: if it yields a new description and view
        // creation succeeds, recreate in place; otherwise, release the handle.
        try {
          auto desc_any = find_desc_any(view_obj);
          DCHECK_F(desc_any.has_value());
          const auto& typed_desc
            = std::any_cast<const typename Resource::ViewDescriptionT&>(
              desc_any.value());
          if (auto next_desc = updater(typed_desc); next_desc.has_value()) {
            auto new_view
              = new_resource->GetNativeView(owned_descriptor, *next_desc);
            if (new_view->IsValid()) {
              const auto key_hash = std::hash<
                std::remove_cvref_t<typename Resource::ViewDescriptionT>> {}(
                *next_desc);
              AttachDescriptorWithView(new_obj, index,
                std::move(owned_descriptor), new_view, std::any(*next_desc),
                key_hash);
              recreated = true;
            } else {
              LOG_F(WARNING,
                "-discarded- could not create view with new description");
            }
          } else {
            LOG_F(WARNING, "-discarded- updater returned no description");
          }
        } catch (std::exception& ex) {
          // Swallow and fall through to unified release below.
          LOG_F(WARNING, "-discarded- with exception: {}", ex.what());
          (void)0;
        } catch (...) {
          // Swallow and fall through to unified release below.
          LOG_F(WARNING, "-discarded- with unknown exception");
          (void)0;
        }

        // We're only checking for validity and releasing only if the descriptor
        // was not moved (which will invalidate it).
        // NOLINTNEXTLINE(bugprone-use-after-move)
        if (!recreated && owned_descriptor.IsValid()) {
          // Ensure the temporary descriptor is not leaked; bindless index is
          // free.
          owned_descriptor.Release();
        }
      }
    }

    // Remove the old resource entry and purge its cached views.
    resources_.erase(old_it);
    PurgeCachedViewsForResource(old_obj);
  }

  template <ResourceWithViews Resource>
  [[nodiscard]] auto Contains(const Resource& resource) const -> bool
  {
    return Contains(NativeResource(&resource, Resource::ClassTypeId()));
  }

  template <ResourceWithViews Resource>
  [[nodiscard]] auto Contains(const Resource& resource,
    const typename Resource::ViewDescriptionT& desc) const -> bool
  {
    auto key = std::hash<std::remove_cvref_t<decltype(desc)>> {}(desc);
    return Contains(
      NativeResource {
        const_cast<Resource*>(&resource),
        Resource::ClassTypeId(),
      },
      key);
  }

  template <ResourceWithViews Resource>
  [[nodiscard]] auto Find(const Resource& resource,
    const typename Resource::ViewDescriptionT& desc) const -> NativeView
  {
    auto key = std::hash<std::remove_cvref_t<decltype(desc)>> {}(desc);
    return Find(
      NativeResource {
        const_cast<Resource*>(&resource),
        Resource::ClassTypeId(),
      },
      key);
  }

  // Returns the shader-visible index for an existing view description if one
  // has already been registered for the given resource; useful to avoid
  // allocating/registering duplicate views.
  template <ResourceWithViews Resource>
  [[nodiscard]] auto FindShaderVisibleIndex(const Resource& resource,
    const typename Resource::ViewDescriptionT& desc) const
    -> std::optional<bindless::ShaderVisibleIndex>
  {
    auto key = std::hash<std::remove_cvref_t<decltype(desc)>> {}(desc);
    return FindShaderVisibleIndex(
      NativeResource { const_cast<Resource*>(&resource),
        Resource::ClassTypeId() },
      key);
  }

  //! Unregister a specific view while preserving the resource and other views.
  /*!
   Removes a specific native view object from the registry, releasing its
   associated descriptor handle and purging it from the view cache. This
   operation provides fine-grained control over view lifecycle, allowing
   selective cleanup of views while keeping the resource and other views intact.

   The method locates the view by its native object identity and removes all
   associated registry state: descriptor handle mapping, cache entries, and
   internal bookkeeping. The descriptor handle is properly released back to its
   originating allocator, making the bindless index available for reuse.

   ### Cleanup Guarantees

   - **Descriptor Release**: Automatically releases the descriptor handle back
     to its allocator, preventing resource leaks.
   - **Cache Purging**: Removes the view from the cache, ensuring no stale
     entries remain.
   - **Index Reclamation**: Frees the bindless index for future allocation.
   - **Selective Removal**: Only affects the specified view; other views on the
     same resource remain unaffected.

   ### Safety and Error Handling

   - **Resource Validation**: Verifies the resource is registered before
     attempting view removal.
   - **Idempotent Operation**: Safe to call multiple times with the same view;
     subsequent calls are no-ops.
   - **Invalid View Handling**: Safely handles invalid or non-existent views
     without throwing exceptions.
   - **Thread Safety**: Fully thread-safe with internal synchronization.

   ### Performance Characteristics

   - Time Complexity: O(1) average case for hash-based lookups, O(N) worst case
     for cache purging where N is the number of cached views for the resource
   - Memory: Immediately frees descriptor and cache entry memory
   - Optimization: Minimal overhead for selective view cleanup

   ### Usage Examples

   ```cpp
   // Remove a specific view while keeping others
   auto srv_view = registry.RegisterView(*texture, srv_handle, srv_desc);
   auto uav_view = registry.RegisterView(*texture, uav_handle, uav_desc);

   // Later, remove only the SRV
   registry.UnRegisterView(*texture, srv_view);
   // UAV remains valid and accessible

   // Safe cleanup in destructors
   class ViewWrapper {
     ~ViewWrapper() {
       if (view_->IsValid()) {
         registry_->UnRegisterView(*resource_, view_);
       }
     }
   };
   ```

   @tparam Resource The resource type, must satisfy SupportedResource concept
   @param resource The resource that owns the view. Must be registered.
   @param view The native view object to remove. Can be invalid (no-op).

   @throw std::runtime_error if the resource is not registered in this registry.

   @note This method is idempotent - calling it multiple times with the same
         view is safe and has no effect after the first call.

   @see UnRegisterViews, UnRegisterResource, RegisterView
  */
  template <SupportedResource Resource>
  auto UnRegisterView(const Resource& resource, const NativeView& view) -> void
  {
    UnRegisterView(
      NativeResource {
        const_cast<Resource*>(&resource),
        Resource::ClassTypeId(),
      },
      view);
  }

  //! Completely remove a resource and all its associated views from the
  //! registry.
  /*!
   Unregisters a resource from the registry, automatically cleaning up all
   associated views, descriptor handles, and cache entries. This operation
   provides complete resource lifecycle management, ensuring no resource leaks
   or dangling references remain after removal.

   The method performs comprehensive cleanup:
   1. Releases all descriptor handles associated with the resource's views
   2. Removes all view cache entries for the resource
   3. Clears internal mappings and bookkeeping structures
   4. Releases the strong reference to the resource

   After this operation, the resource is completely disconnected from the
   registry and may be destroyed if no other references exist.

   ### Cleanup Scope

   - **All Views**: Every view registered for this resource is automatically
     unregistered and cleaned up.
   - **Descriptor Handles**: All associated descriptor handles are released back
     to their respective allocators.
   - **Cache Entries**: All cached views for this resource are purged to prevent
     stale access.
   - **Strong Reference**: Registry releases its shared_ptr, potentially
     triggering resource destruction.

   ### Resource Lifecycle Integration

   - **Automatic Cleanup**: Eliminates need for manual view-by-view cleanup when
     destroying resources.
   - **Exception Safety**: Cleanup is performed safely even if individual view
     cleanup encounters errors.
   - **Memory Management**: Ensures no memory leaks from forgotten views or
     descriptor handles.
   - **Deterministic Destruction**: Predictable resource cleanup behavior.

   ### Performance Characteristics

   - Time Complexity: O(N) where N is the number of views registered for this
     resource
   - Memory: Immediately frees all memory associated with the resource and its
     views in the registry
   - Optimization: Batch cleanup avoids repeated synchronization overhead

   ### Usage Examples

   ```cpp
   // Resource lifecycle management
   {
     auto texture = std::make_shared<Texture>(device, desc);
     registry.Register(texture);

     // Register multiple views
     registry.RegisterView(*texture, srv_handle, srv_desc);
     registry.RegisterView(*texture, uav_handle, uav_desc);
     registry.RegisterView(*texture, rtv_handle, rtv_desc);

     // Complete cleanup with single call
     registry.UnRegisterResource(*texture);
   } // texture may be destroyed here if no other references

   // Integration with resource managers
   class ResourceManager {
     void DestroyTexture(TextureId id) {
       auto texture = texture_map_[id];
       registry_.UnRegisterResource(*texture);
       texture_map_.erase(id);
     }
   };
   ```

   @tparam Resource The resource type, must satisfy SupportedResource concept
   @param resource The resource to completely remove from the registry. Must be
          currently registered.

   @throw std::runtime_error if the resource is not registered in this registry.

   @note This operation is not reversible - after unregistration, the resource
         must be re-registered before any view operations can be performed.

   @see UnRegisterViews, UnRegisterView, Register
  */
  template <SupportedResource Resource>
  auto UnRegisterResource(const Resource& resource) -> void
  {
    UnRegisterResource(NativeResource {
      const_cast<Resource*>(&resource),
      Resource::ClassTypeId(),
    });
  }

  //! Release all views for a resource while keeping the resource registered.
  /*!
   Removes all views associated with a resource from the registry while
   preserving the resource registration itself. This operation provides
   selective cleanup for scenarios where you want to rebuild views or
   temporarily clear all views without affecting the resource's registration
   status.

   Unlike UnRegisterResource, this method:
   - Preserves the resource registration in the registry
   - Only removes views, descriptor handles, and cache entries
   - Allows immediate re-registration of new views
   - Maintains the resource's identity and lifecycle state

   ### Selective Cleanup Benefits

   - **View Rebuilding**: Clear all existing views before registering new ones
     with different properties.
   - **Format Changes**: Remove views with old formats before creating views
     with new formats.
   - **Performance Optimization**: Bulk removal is more efficient than
     individual UnRegisterView calls.
   - **State Management**: Reset view state while maintaining resource
     lifecycle.

   ### Cleanup Guarantees

   - **All Views Removed**: Every view registered for this resource is
     unregistered and cleaned up.
   - **Descriptor Release**: All descriptor handles are released back to their
     allocators.
   - **Cache Purging**: All cached view entries for this resource are removed.
   - **Resource Preservation**: The resource remains registered and available
     for new view operations.

   ### Performance Characteristics

   - Time Complexity: O(N) where N is the number of views for this resource
   - Memory: Frees all view-related memory while preserving resource entry
   - Optimization: More efficient than multiple individual UnRegisterView calls
     due to reduced synchronization overhead

   ### Usage Examples

   ```cpp
   // View format migration
   auto texture = std::make_shared<Texture>(device, old_desc);
   registry.Register(texture);

   // Register views with old format
   registry.RegisterView(*texture, srv_handle_old, old_srv_desc);
   registry.RegisterView(*texture, uav_handle_old, old_uav_desc);

   // Clear all views for format change
   registry.UnRegisterViews(*texture);

   // Re-register with new format
   registry.RegisterView(*texture, srv_handle_new, new_srv_desc);
   registry.RegisterView(*texture, uav_handle_new, new_uav_desc);

   // Conditional view cleanup
   if (should_reset_views) {
     registry.UnRegisterViews(*my_buffer);
     // Resource is still registered, ready for new views
   }
   ```

   @tparam Resource The resource type, must satisfy SupportedResource concept
   @param resource The resource whose views should be removed. Must be
          registered in the registry.

   @note The resource itself remains registered after this operation and can
         immediately accept new view registrations.

   @see UnRegisterResource, UnRegisterView, RegisterView
  */
  template <SupportedResource Resource>
  auto UnRegisterViews(const Resource& resource) -> void
  {
    UnRegisterResourceViews(NativeResource {
      const_cast<Resource*>(&resource),
      Resource::ClassTypeId(),
    });
  }

private:
  OXGN_GFX_API auto Register(std::shared_ptr<void> resource, TypeId type_id)
    -> void;

  OXGN_GFX_API auto RegisterView(NativeResource resource, NativeView view,
    DescriptorHandle view_handle, std::any view_description_for_cache,
    size_t key, ResourceViewType view_type, DescriptorVisibility visibility)
    -> NativeView;

  OXGN_GFX_API auto UnRegisterView(
    const NativeResource& resource, const NativeView& view) -> void;

  OXGN_GFX_API auto UnRegisterResource(const NativeResource& resource) -> void;
  OXGN_GFX_API auto UnRegisterResourceViews(const NativeResource& resource)
    -> void;

  auto UnRegisterViewNoLock(
    const NativeResource& resource, const NativeView& view) -> void;
  OXGN_GFX_API auto UnRegisterResourceViewsNoLock(
    const NativeResource& resource) -> void;

  // Internal helpers (assume registry_mutex_ is held)
  //! Remove all cached views for a resource. Assumes registry_mutex_ held.
  OXGN_GFX_API auto PurgeCachedViewsForResource(const NativeResource& resource)
    -> void;
  //! Attach a descriptor and associate a native view and cache entry. Assumes
  //! registry_mutex_ held.
  OXGN_GFX_API auto AttachDescriptorWithView(const NativeResource& dst_resource,
    bindless::HeapIndex index, DescriptorHandle descriptor_handle,
    const NativeView& view, std::any description, std::size_t key_hash) -> void;
  //! Collect all descriptor indices owned by a resource. Assumes
  //! registry_mutex_ held.
  OXGN_GFX_NDAPI auto CollectDescriptorIndicesForResource(
    const NativeResource& resource) const -> std::vector<bindless::HeapIndex>;

  OXGN_GFX_NDAPI auto Contains(const NativeResource& resource) const -> bool;
  OXGN_GFX_NDAPI auto Contains(
    const NativeResource& resource, size_t key_hash) const -> bool;

  OXGN_GFX_NDAPI auto Find(
    const NativeResource& resource, size_t key_hash) const -> NativeView;
  // Find the shader-visible index (if any) for a registered view description
  // associated with `resource` and `key_hash`.
  OXGN_GFX_NDAPI auto FindShaderVisibleIndex(
    const NativeResource& resource, size_t key_hash) const
    -> std::optional<bindless::ShaderVisibleIndex>;

  // Thread safety
  mutable std::mutex registry_mutex_;

  // Resource tracking
  struct ResourceEntry {
    // Erase the type information, but hold a strong reference to the resource
    // while it is registered.
    std::shared_ptr<void> resource;

    // Descriptors associated with this resource
    struct ViewEntry {
      NativeView view_object; // Native view object
      DescriptorHandle descriptor; // Handle to descriptor heap entry
    };

    // Map from descriptor index to view entry
    std::unordered_map<bindless::HeapIndex, ViewEntry> descriptors;
  };

  // Primary storage
  std::unordered_map<NativeResource, ResourceEntry> resources_;

  // Map from descriptor index to owning resource
  std::unordered_map<bindless::HeapIndex, NativeResource>
    descriptor_to_resource_;

  // Unified view cache
  struct CacheKey {
    NativeResource resource; // The resource object
    std::size_t view_desc_hash; // Hash of the view description

    auto operator==(const CacheKey& other) const -> bool
    {
      return view_desc_hash == other.view_desc_hash
        && resource == other.resource;
    }
  };

  //! A custom hash functor for CacheKey.
  struct CacheKeyHasher {
    auto operator()(const CacheKey& k) const noexcept -> std::size_t
    {
      std::size_t result = std::hash<NativeResource> {}(k.resource);
      HashCombine(result, k.view_desc_hash);
      return result;
    }
  };

  //! View cache entry that stores both the view and its description.
  struct ViewCacheEntry {
    NativeView view_object; //!< The native object holding the view.
    std::any view_description; //!< The original view description.
  };

  //! A unified view cache for all resources and view types.
  std::unordered_map<CacheKey, ViewCacheEntry, CacheKeyHasher> view_cache_;

  std::string debug_name_; //!< Debug name for the registry.
};

} // namespace oxygen::graphics
