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

//! Registry for graphics resources and their views, supporting bindless
//! rendering.
class ResourceRegistry {
public:
  OXGN_GFX_API explicit ResourceRegistry(std::string_view debug_name);

  OXGN_GFX_API virtual ~ResourceRegistry() noexcept;

  OXYGEN_MAKE_NON_COPYABLE(ResourceRegistry)
  OXYGEN_DEFAULT_MOVABLE(ResourceRegistry)

  //! Register a graphics resource, such as textures, buffers, and samplers.
  /*!
   The registry will keep a strong reference to the resource until it is
   unregistered. For resources with views (textures, buffers), views can be
   registered and managed for bindless rendering. For simple resources like
   samplers, only the resource itself is registered and tracked.

   \throws std::runtime_error if the resource is already registered.
  */
  template <SupportedResource Resource>
  auto Register(const std::shared_ptr<Resource>& resource) -> void
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
           Returns an invalid NativeView if the view is invalid.

   \throws std::runtime_error if the resource is not registered, or a view
           with a compatible descriptor exists in the cache.
  */
  template <ResourceWithViews Resource>
  auto RegisterView(Resource& resource, DescriptorHandle view_handle,
    const typename Resource::ViewDescriptionT& desc)
    -> NativeView // TODO: document exceptions
  {
    auto view = resource.GetNativeView(view_handle, desc);
    auto key = std::hash<std::remove_cvref_t<decltype(desc)>> {}(desc);
    return RegisterView(NativeResource { &resource, Resource::ClassTypeId() },
      std::move(view), std::move(view_handle), std::any(desc), key,
      desc.view_type, desc.visibility);
  }

  //! Register an already created view for a graphics resource, such as
  //! textures and buffers, making it available for cached lookups.
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

  //! Update a registered view in-place, keeping the same descriptor slot.
  /*!
   Replaces the native view object bound at the given bindless index with a new
   view created from the provided resource and view description. The
   shader-visible bindless index remains unchanged. If the descriptor was
   previously associated with a different resource, ownership is transferred to
   the new resource. The view cache is updated accordingly.

   @tparam Resource The resource type (must satisfy ResourceWithViews)
   @param resource The resource that will own the updated view.
   @param index The existing bindless descriptor slot to update.
   @param desc The new view description.
   @return true if the view was updated successfully.

  ### Failure Semantics

  - If view creation fails for any reason after the descriptor is acquired
    from the previous owner, the descriptor handle is released and the view
    registration for that index is removed (index becomes free), matching
    Replace() behavior.
  */
  template <ResourceWithViews Resource>
  auto UpdateView(Resource& resource, bindless::Handle index,
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
        } else {
          // Inconsistent state
          return false;
        }
      }
    } else {
      // Unknown index
      return false;
    }

    // Create the new native view at the same descriptor slot using the owned
    // descriptor handle.
    auto new_view = resource.GetNativeView(owned_descriptor, desc);
    if (!new_view->IsValid()) {
      // Failure -> release the temporary descriptor and purge old cache; the
      // index becomes free and no registration remains for it.
      if (owned_descriptor.IsValid()) {
        owned_descriptor.Release();
      }
      // Remove any cache entry for the old view object if present.
      if (old_view_obj->IsValid()) {
        std::erase_if(view_cache_, [&](const auto& it) {
          return it.first.resource == old_res_obj
            && it.second.view_object == old_view_obj;
        });
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

    // Update cache entry
    ViewCacheEntry cache_entry {
      .view_object = new_view,
      .view_description = std::any(desc),
    };
    const CacheKey new_cache_key {
      .resource = new_res_obj,
      .view_desc_hash = key_hash,
    };
    view_cache_[new_cache_key] = std::move(cache_entry);
    // Remove any cache entry for the old view object (previous owner)
    if (old_view_obj->IsValid()) {
      std::erase_if(view_cache_, [&](const auto& it) {
        return it.first.resource == old_res_obj
          && it.second.view_object == old_view_obj;
      });
    }
    return true;
  }

  //! Replace a registered resource with standardized semantics.
  /*!
   Two explicit modes are supported:

   1) Updater provided (recreate-in-place):
      - For each descriptor of `old_resource`, call `update_fn` with the
        previous `ViewDescriptionT`.
      - If `update_fn` returns a description and creating the view for
        `new_resource` succeeds, the view is recreated in-place at the same
        bindless index (stable handle retained).
      - If `update_fn` returns `std::nullopt` or view creation fails, that
        descriptor handle is released (freed) and not transferred.

   2) No updater (nullptr):
      - No descriptors are transferred. All views/handles of `old_resource`
        are unregistered and released. `new_resource` remains registered but
        owns no descriptors.

   Consequences:
   - Stable handles are guaranteed only for successfully recreated views.
   - Null-updater path performs a clean release; no dangling entries exist in
     the registry, and no follow-up UpdateView calls are expected to succeed
     for the old indices.

   @tparam Resource A resource type that satisfies ResourceWithViews.
   @tparam ViewUpdater Callable conforming to
   `detail::ViewUpdaterCallable<Resource, ViewUpdater>`. Use
   `std::nullptr_t` to select the release-all mode.

   @param old_resource The currently registered resource being replaced.
   @param new_resource The new resource to register; may receive recreated
          views when the updater is provided and succeeds.
   @param update_fn Per-descriptor policy; given the previous
          `ViewDescriptionT`, returns an optional next description.

   ### Performance Characteristics

   - Time Complexity: O(N) for N descriptors of the resource.
   - Memory: O(N) transient for snapshotting indices; no heap growth in the
     registry beyond the new resource entry.
   - Optimization: Avoids descriptor re-allocation for recreated views and
     ensures immediate cleanup for released ones.

   ### Usage Example

   ```cpp
   // Recreate in-place (keep handles for successful updates)
   registry.Replace(old_buf, new_buf,
     [](const Buffer::ViewDescriptionT& prev) -> std::optional<auto> {
       auto next = prev; // adjust as needed
       return next;
     });

   // Release-all (no transfers)
   registry.Replace(old_buf, new_buf, nullptr);
   ```

   @throw std::runtime_error if `old_resource` is not registered.
   @see UpdateView
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
      std::vector<bindless::Handle> indices
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
              DLOG_F(WARNING,
                "-discarded- could not create native view with new "
                "description");
            }
          } else {
            DLOG_F(WARNING, "-discarded- updater returned no description");
          }
        } catch (std::exception& ex) {
          // Swallow and fall through to unified release below.
          DLOG_F(WARNING, "-discarded- with exception: {}", ex.what());
          (void)0;
        } catch (...) {
          // Swallow and fall through to unified release below.
          DLOG_F(WARNING, "-discarded- with unknown exception");
          (void)0;
        }

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

  template <SupportedResource Resource>
  auto UnRegisterResource(const Resource& resource) -> void
  {
    UnRegisterResource(NativeResource {
      const_cast<Resource*>(&resource),
      Resource::ClassTypeId(),
    });
  }

  //! Release all views for a resource
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
  //! Attach a descriptor and associate a native view and cache entry.
  //! Assumes registry_mutex_ held.
  OXGN_GFX_API auto AttachDescriptorWithView(const NativeResource& dst_resource,
    bindless::Handle index, DescriptorHandle descriptor_handle,
    const NativeView& view, std::any description, std::size_t key_hash) -> void;
  //! Collect all descriptor indices owned by a resource. Assumes
  //! registry_mutex_ held.
  OXGN_GFX_NDAPI auto CollectDescriptorIndicesForResource(
    const NativeResource& resource) const -> std::vector<bindless::Handle>;

  OXGN_GFX_NDAPI auto Contains(const NativeResource& resource) const -> bool;
  OXGN_GFX_NDAPI auto Contains(
    const NativeResource& resource, size_t key_hash) const -> bool;

  OXGN_GFX_NDAPI auto Find(
    const NativeResource& resource, size_t key_hash) const -> NativeView;

  // Thread safety
  mutable std::mutex registry_mutex_;

  // Resource tracking
  struct ResourceEntry {
    // Erase the type information, but hold a strong reference to the
    // resource while it is registered.
    std::shared_ptr<void> resource;

    // Descriptors associated with this resource
    struct ViewEntry {
      NativeView view_object; // Native view object
      DescriptorHandle descriptor; // Handle to descriptor heap entry
    };

    // Map from descriptor index to view entry
    std::unordered_map<bindless::Handle, ViewEntry> descriptors;
  };

  // Primary storage
  std::unordered_map<NativeResource, ResourceEntry> resources_;

  // Map from descriptor index to owning resource
  std::unordered_map<bindless::Handle, NativeResource> descriptor_to_resource_;

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
