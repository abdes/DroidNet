//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <any>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Graphics/Common/Concepts.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

//! Registry for graphics resources and their views, supporting bindless
//! rendering.
class ResourceRegistry {
public:
  OXYGEN_GFX_API explicit ResourceRegistry(std::string_view debug_name);

  OXYGEN_GFX_API virtual ~ResourceRegistry() noexcept;

  OXYGEN_MAKE_NON_COPYABLE(ResourceRegistry)
  OXYGEN_DEFAULT_MOVABLE(ResourceRegistry)

  // TODO: provide API to update a view registration with a new native object,
  // keeping the same descriptor handle.

  //! Register a graphics resource, such as textures, buffers, and samplers.
  /*!
   The registry will keep a strong reference to the resource until it is
   unregistered. For resources with views (textures, buffers), views can be
   registered and managed for bindless rendering. For simple resources like
   samplers, only the resource itself is registered and tracked.

   \throws std::runtime_error if the resource is already registered.
  */
  template <SupportedResource Resource>
  void Register(const std::shared_ptr<Resource>& resource)
  {
    Register(std::static_pointer_cast<void>(resource), Resource::ClassTypeId());
  }

  //! Register a view for a graphics resource, such as textures and buffers.
  /*!
   Registers a view for a graphics resource (e.g., texture or buffer) and
   returns a handle to the native view object (check GetNativeView of the
   corresponding resource class for the native view object type). This method
   is only enabled for resources that satisfy ResourceWithViews. For simple
   resources like samplers, view registration is not supported or required.

   The registry will create the native view using the resource's GetNativeView
   method, and then attempt to register it for view caching. The descriptor
   handle must be valid and allocated from a compatible DescriptorAllocator.
   The view description must be hashable and comparable.

   \param resource The resource to register the view for. Must already be
          registered in the registry.
   \param view_handle The descriptor handle for the view. Must be valid and
          compatible with the resource type.
   \param desc The view description. Must be hashable and comparable.

   \return A handle to the native view, newly created or from the cache.
           Returns an invalid NativeObject if the view is invalid.

   \throws std::runtime_error if the resource is not registered, or a view
           with a compatible descriptor exists in the cache.
  */
  template <ResourceWithViews Resource>
  auto RegisterView(Resource& resource, DescriptorHandle view_handle,
    const typename Resource::ViewDescriptionT& desc)
    -> NativeObject // TODO: document exceptions
  {
    auto view = resource.GetNativeView(view_handle, desc);
    auto key = std::hash<std::remove_cvref_t<decltype(desc)>> {}(desc);
    return RegisterView(NativeObject { &resource, Resource::ClassTypeId() },
      std::move(view), std::move(view_handle), std::any(desc), key,
      desc.view_type, desc.visibility);
  }

  //! Register an already created view for a graphics resource, such as
  //! textures and buffers, making it available for for cached lookups.
  /*!
   Registers an already created native view for a graphics resource (e.g.,
   texture or buffer), making it available for bindless rendering and view
   caching. This method is only enabled for resources that satisfy
   ResourceWithViews.

   Use this method when you need complete control over view creation. The view
   description must be unique and not conflict with any cached views. If a
   view with a compatible descriptor is already registered for the resource,
   this method will throw a std::runtime_error. If the resource is not
   registered, or the view is invalid, the method will return false. If the
   descriptor handle is invalid, the view description is empty, or the key
   hash is zero, the method will abort.

   \param resource The resource to register the view for. Must already be
          registered in the registry.
   \param view The native view object to register. Must be valid.
   \param view_handle The descriptor handle for the view. Must be valid and
          compatible with the resource type.
   \param desc The view description. Must be hashable and comparable.

   \return true if the view was registered successfully, false if the resource
           or view is invalid.

   \throws std::runtime_error if the resource is not registered, or a view
           with a compatible descriptor exists in the cache.
  */
  template <ResourceWithViews Resource>
  auto RegisterView(Resource& resource, NativeObject view,
    DescriptorHandle view_handle,
    const typename Resource::ViewDescriptionT& desc) -> bool
  {
    auto key = std::hash<std::remove_cvref_t<decltype(desc)>> {}(desc);
    return RegisterView(NativeObject { &resource, Resource::ClassTypeId() },
      std::move(view), std::move(view_handle), std::any(desc), key,
      desc.view_type, desc.visibility)
      .IsValid();
  }

  template <ResourceWithViews Resource>
  [[nodiscard]] auto Contains(const Resource& resource) const -> bool
  {
    return Contains(NativeObject(&resource, Resource::ClassTypeId()));
  }

  template <ResourceWithViews Resource>
  [[nodiscard]] auto Contains(const Resource& resource,
    const typename Resource::ViewDescriptionT& desc) const -> bool
  {
    auto key = std::hash<std::remove_cvref_t<decltype(desc)>> {}(desc);
    return Contains(
      NativeObject(const_cast<Resource*>(&resource), Resource::ClassTypeId()),
      key);
  }

  template <ResourceWithViews Resource>
  [[nodiscard]] auto Find(const Resource& resource,
    const typename Resource::ViewDescriptionT& desc) const -> NativeObject
  {
    auto key = std::hash<std::remove_cvref_t<decltype(desc)>> {}(desc);
    return Find(
      NativeObject(const_cast<Resource*>(&resource), Resource::ClassTypeId()),
      key);
  }

  template <SupportedResource Resource>
  void UnRegisterView(const Resource& resource, const NativeObject& view)
  {
    UnRegisterView(NativeObject { const_cast<Resource*>(&resource),
                     Resource::ClassTypeId() },
      view);
  }

  template <SupportedResource Resource>
  void UnRegisterResource(const Resource& resource)
  {
    UnRegisterResource(NativeObject {
      const_cast<Resource*>(&resource), Resource::ClassTypeId() });
  }

  //! Release all views for a resource
  template <SupportedResource Resource>
  void UnRegisterViews(const Resource& resource)
  {
    UnRegisterResourceViews(NativeObject {
      const_cast<Resource*>(&resource), Resource::ClassTypeId() });
  }

private:
  OXYGEN_GFX_API void Register(std::shared_ptr<void> resource, TypeId type_id);

  OXYGEN_GFX_API auto RegisterView(NativeObject resource, NativeObject view,
    DescriptorHandle view_handle, std::any view_description_for_cache,
    size_t key, ResourceViewType view_type, DescriptorVisibility visibility)
    -> NativeObject;

  OXYGEN_GFX_API void UnRegisterView(
    const NativeObject& resource, const NativeObject& view);

  OXYGEN_GFX_API void UnRegisterResource(const NativeObject& resource);
  OXYGEN_GFX_API void UnRegisterResourceViews(const NativeObject& resource);

  void UnRegisterViewNoLock(
    const NativeObject& resource, const NativeObject& view);
  void UnRegisterResourceViewsNoLock(const NativeObject& resource);

  [[nodiscard]] OXYGEN_GFX_API auto Contains(const NativeObject& resource) const
    -> bool;
  [[nodiscard]] OXYGEN_GFX_API auto Contains(
    const NativeObject& resource, size_t key_hash) const -> bool;

  [[nodiscard]] OXYGEN_GFX_API auto Find(
    const NativeObject& resource, size_t key_hash) const -> NativeObject;

  // TODO: consider deleting these methods as I cannot find a use case
  [[nodiscard]] auto Contains(const DescriptorHandle& descriptor) const
    -> NativeObject;
  [[nodiscard]] auto Find(const DescriptorHandle& descriptor) const
    -> NativeObject;

  // Thread safety
  mutable std::mutex registry_mutex_;

  // Resource tracking
  struct ResourceEntry {
    // Erase the type information, but hold a strong reference to the
    // resource while it is registered.
    std::shared_ptr<void> resource;

    // Descriptors associated with this resource
    struct ViewEntry {
      NativeObject view_object; // Native view object
      DescriptorHandle descriptor; // Handle to descriptor heap entry
    };

    // Map from descriptor index to view entry
    std::unordered_map<bindless::Handle, ViewEntry> descriptors;
  };

  // Primary storage - Resource NativeObject to resource entry
  std::unordered_map<NativeObject, ResourceEntry> resources_;

  // Map from descriptor index to owning resource
  std::unordered_map<bindless::Handle, NativeObject> descriptor_to_resource_;

  // Unified view cache
  struct CacheKey {
    NativeObject resource; // The resource object
    std::size_t hash; // Hash of the view description

    auto operator==(const CacheKey& other) const -> bool
    {
      return hash == other.hash && resource == other.resource;
    }
  };

  //! A custom hash functor for CacheKey.
  struct CacheKeyHasher {
    auto operator()(const CacheKey& k) const noexcept -> std::size_t
    {
      std::size_t result = std::hash<NativeObject> {}(k.resource);
      oxygen::HashCombine(result, k.hash);
      return result;
    }
  };

  //! View cache entry that stores both the view and its description.
  struct ViewCacheEntry {
    NativeObject view_object; //!< The native object holding the view.
    std::any view_description; //!< The original view description.
  };

  //! A unified view cache for all resources and view types.
  std::unordered_map<CacheKey, ViewCacheEntry, CacheKeyHasher> view_cache_;

  std::string debug_name_; //!< Debug name for the registry.
};

} // namespace oxygen::graphics

/*
================================================================================
Resource Replacement & View Update Design Notes
================================================================================

- When replacing a resource (e.g., resizing a texture), it is critical to keep
  the shader-visible descriptor handle (bindless index) stable, even if the
  underlying native resource object changes.

- In D3D12/Vulkan/Metal, descriptors (views) are tightly bound to the native
  resource object. Replacing the resource invalidates all existing views; they
  must be recreated for the new resource at the same descriptor slot.

- There are two main strategies for handling view updates:
    1. The registry automatically recreates views in-place for the new resource.
    2. The registry clears cached views and requires the resource owner to
       explicitly recreate or discard them.

- The second approach is safer and more flexible, especially when resource
  compatibility is not guaranteed. It avoids accidental reuse of incompatible
  views and gives the resource owner full control.

- The best practice is for the registry to provide an Update or UpdateView
  method that takes a function (callback). The registry iterates all cached
  views for the replaced resource and invokes the callback for each view
  description. The resource owner decides whether to recreate, update, or
  discard each view.

- The callback should have the signature:
      NativeObject callback(const Resource::ViewDescriptionT& desc);
  If the returned NativeObject is valid, the view is recreated in-place at the
  same descriptor handle. If invalid, the view is discarded.

- Use a template for the callback to ensure zero-overhead, type safety, and
  flexibility. This is preferred over std::function for performance-critical
  engine code.

================================================================================
*/

//! Replace a registered resource with a new instance, updating or discarding
//! views as directed by a callback.
/*!
 This method enables resource replacement (e.g., resizing a texture) while
 keeping shader-visible descriptor handles stable. For each cached view of the
 old resource, the provided callback is invoked with the view description. The
 callback must return a NativeObject representing the new native view for the
 replacement resource, or an invalid NativeObject to indicate the view should be
 discarded.

 The callback signature must be:
     NativeObject callback(const Resource::ViewDescriptionT& desc);

 \tparam Resource The resource type (must satisfy ResourceWithViews).
 \tparam Callback The callback type (must accept a const ViewDescriptionT& and
         return NativeObject).
 \param old_resource The resource to be replaced (must be registered).
 \param new_resource The new resource instance (must be of the same type).
 \param update_view_fn The callback invoked for each view description.
 \throws std::runtime_error if the resource is not registered.

    template <typename Resource, typename Fn>
    concept ViewUpdateCallback =
        requires(Fn&& fn, const typename Resource::ViewDescriptionT& desc) {
            { fn(desc) } -> std::convertible_to<NativeObject>;
        };

    template <ResourceWithViews Resource, ViewUpdateCallback<Resource> Callback>
    void Replace(
        const Resource& resource,
        std::shared_ptr<Resource> new_resource,
        Callback&& update_fn = nullptr);
*/

/*
================================================================================
View Replacement & UpdateView Design Notes
================================================================================

- In modern graphics APIs, a descriptor handle (bindless index) refers to a
  specific slot in a descriptor heap. The view object (e.g., SRV/RTV/UAV) at
  that slot can be changed at runtime, allowing the shader to see a new view
  without changing the index.

- The UpdateView method enables replacing the native view object at a given
  descriptor handle for a resource, while keeping the shader-visible index
  stable. This is useful for hot-reloading, dynamic format/sub-resource changes,
  or swapping views at runtime.

- The new view does not need to be compatible with the old view; it can have a
  different description, as long as the application logic ensures correctness.

- This method is not for resource replacement (which invalidates all views), but
  for updating a single view of an existing resource.

- The registry updates the descriptor heap slot in-place and updates its cache
  to point to the new view object.

--------------------------------------------------------------------------------
Signatures:

Update a registered view for a resource, replacing the old view with a new one
at the same descriptor handle.

This method finds the existing view (old_view) for the given resource and
replaces it with new_view, keeping the same descriptor handle (bindless index)
for shader access. The new view must be compatible with the existing view
description (e.g., format, sub-resources, etc.), as the descriptor slot and view
description are not changed—only the underlying native view object is replaced.
This is useful for scenarios such as re-creating a view after device loss,
resource reinitialization, or hot-reloading when the view semantics remain the
same.

    template <ResourceWithViews Resource>
    void ReplaceView(
        Resource& resource,
        NativeObject old_view,
        NativeObject new_view);

Update a registered view for a resource, replacing the old view with a new one
at the same descriptor handle.

This method finds the existing view (old_view) for the given resource and
replaces it with new_view, keeping the same descriptor handle (bindless index)
for shader access. The new view can have a different description, as long as the
application logic ensures correctness. This is useful for hot-reloading, dynamic
format/sub-resource changes, or swapping views at runtime.

    template <ResourceWithViews Resource>
    bool ReplaceView(
        Resource& resource,
        NativeObject old_view,
        NativeObject new_view,
        const typename Resource::ViewDescriptionT& desc);

    // - resource: The resource whose view is being updated or registered.
    // - old_view: The native view object to be replaced (in-place update).
    // - new_view: The new native view object to use.
    // - desc: The new view description (for new descriptor handle).

================================================================================
*/
