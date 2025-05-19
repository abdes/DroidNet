//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <any>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

class Texture;
struct TextureViewKey;

class Buffer;
struct BufferViewKey;

struct RegisteredResource { };

template <typename T>
concept TextureResource = std::is_base_of_v<Texture, std::remove_cvref_t<T>>;

template <typename T>
concept BufferResource = std::is_base_of_v<Buffer, std::remove_cvref_t<T>>;

template <typename T>
concept AnyResource = std::is_base_of_v<RegisteredResource, std::remove_cvref_t<T>>;

template <typename T>
concept ViewDescription = std::equality_comparable<T>
    && requires(T vk) {
           { std::hash<T> {}(vk) } -> std::convertible_to<std::size_t>;
           { vk.view_type } -> std::convertible_to<ResourceViewType>;
           { vk.visibility } -> std::convertible_to<DescriptorVisibility>;
       };

template <typename T>
concept SupportedResource
    = (TextureResource<T>
          || BufferResource<T>
          || AnyResource<T>)
    && requires { { T::ClassTypeId() } -> std::convertible_to<TypeId>; };

template <typename T>
concept ResourceWithViews = SupportedResource<T>
    && requires { typename T::ViewDescriptionT; }
    && ViewDescription<typename T::ViewDescriptionT>;

//! Registry for graphics resources and their views, supporting bindless rendering.
class ResourceRegistry {
public:
    OXYGEN_GFX_API explicit ResourceRegistry(std::shared_ptr<DescriptorAllocator> allocator);

    OXYGEN_GFX_API virtual ~ResourceRegistry() noexcept;

    OXYGEN_MAKE_NON_COPYABLE(ResourceRegistry)
    OXYGEN_DEFAULT_MOVABLE(ResourceRegistry)

    // TODO: provide API to update a view registration with a new native object, keeping the same descriptor handle.

    //! Register a graphics resource, such as textures and buffers.
    /*!
     The registry will keep a strong reference to the resource until it is
     unregistered. Therefore, the recommended lifetime management process is to
     unregister the resource when it is no longer necessary, which will also
     release all views associated with it. Only then can the resource be really
     released.
    */
    template <SupportedResource Resource>
    void Register(std::shared_ptr<Resource> resource)
    {
        Register(
            std::static_pointer_cast<void>(resource),
            Resource::ClassTypeId());
    }

    //! Register a view for a graphics resource, such as textures and buffers,
    //! making it available for bindless rendering and for cached resource
    //! lookups.
    /*!
     Use this method when you are certain that the view description is unique
     and does not conflict with any existing views. If no cached view is found,
     a new view and a corresponding descriptor handle will be created. The view
     is then registered for bindless rendering and cached for future use. If a
     view with a compatible descriptor is already registered for the resource,
     this method will do nothing and throw and exception to prevent accidental
     overwriting of existing views.

     \note This method is thread-safe.

     \param resource The resource to register the view for. Must be already
            registered in the registry.
     \param desc The view description, which must be hashable and comparable.
     \return A handle to the native view, newly created or from the cache.

     \throws std::runtime_error if the resource is not registered, a view with a
             compatible descriptor exists in the cache, or an error occurs
             during the view creation or registration.
    */
    template <ResourceWithViews Resource>
    auto RegisterView(
        Resource& resource,
        const typename Resource::ViewDescriptionT& desc)
        -> NativeObject
    {
        auto view = resource.GetNativeView(desc);
        auto key = std::hash<std::remove_cvref_t<decltype(desc)>> {}(desc);
        return RegisterView(
            NativeObject { &resource, Resource::ClassTypeId() },
            std::move(view),
            std::any(desc),
            key,
            desc.view_type,
            desc.visibility);
    }

    //! Register an already created view for a graphics resource, such as
    //! textures and buffers, making it available for bindless rendering and for
    //! cached resource lookups.
    /*!
     Use this method when complete control over the view creation is needed. The
     view description is still required to be unique and not conflict with any
     cached views. If no cached view is found, a corresponding descriptor handle
     will be created. The view is then registered for bindless rendering and
     cached for future use. If a view with a compatible descriptor is already
     registered for the resource, this method will do nothing and throw and
     exception to prevent accidental overwriting of existing views.

     \note This method is thread-safe.

     \param resource The resource to register the view for. Must be already
            registered in the registry.
     \param view The native view object to register.
     \param desc The view description, which must be hashable and comparable.
     \return A handle to the native view, newly created or from the cache.

     \throws std::runtime_error if the resource is not registered, a view with a
             compatible descriptor exists in the cache, or an error occurs
             during the view registration.
    */
    template <ResourceWithViews Resource>
    auto RegisterView(Resource& resource, NativeObject view,
        const typename Resource::ViewDescriptionT& desc) -> bool
    {
        auto key = std::hash<std::remove_cvref_t<decltype(desc)>> {}(desc);
        return RegisterView(
            NativeObject { &resource, Resource::ClassTypeId() },
            std::move(view),
            std::any(desc),
            key,
            desc.view_type,
            desc.visibility)
            .IsValid();
    }

    [[nodiscard]] OXYGEN_GFX_API auto Contains(const DescriptorHandle& descriptor) const -> NativeObject;

    template <ResourceWithViews Resource>
    [[nodiscard]] auto Contains(const Resource& resource) const -> bool
    {
        return Contains(NativeObject(&resource, Resource::ClassTypeId()));
    }

    template <ResourceWithViews Resource>
    [[nodiscard]] auto Contains(
        const Resource& resource,
        const typename Resource::ViewDescriptionT& desc) const -> bool
    {
        auto key = std::hash<std::remove_cvref_t<decltype(desc)>> {}(desc);
        return Contains(NativeObject(const_cast<Resource*>(&resource), Resource::ClassTypeId()), key);
    }

    //! Get native view for a descriptor
    [[nodiscard]] OXYGEN_GFX_API auto Find(const DescriptorHandle& descriptor) const -> NativeObject;

    template <ResourceWithViews Resource>
    [[nodiscard]] auto Find(
        const Resource& resource,
        const typename Resource::ViewDescriptionT& desc) const
        -> NativeObject
    {
        auto key = std::hash<std::remove_cvref_t<decltype(desc)>> {}(desc);
        return Find(NativeObject(const_cast<Resource*>(&resource), Resource::ClassTypeId()), key);
    }

    template <SupportedResource Resource>
    void UnRegisterView(const Resource& resource, const NativeObject& view)
    {
        UnRegisterView(NativeObject { const_cast<Resource*>(&resource), Resource::ClassTypeId() }, view);
    }

    template <SupportedResource Resource>
    void UnRegisterResource(const Resource& resource)
    {
        UnRegisterResource(NativeObject { const_cast<Resource*>(&resource), Resource::ClassTypeId() });
    }

    //! Release all views for a resource
    template <SupportedResource Resource>
    void UnRegisterViews(const Resource& resource)
    {
        UnRegisterResourceViews(NativeObject { const_cast<Resource*>(&resource), Resource::ClassTypeId() });
    }

private:
    OXYGEN_GFX_API void Register(std::shared_ptr<void> resource, TypeId type_id);

    OXYGEN_GFX_API auto RegisterView(
        NativeObject resource, NativeObject view,
        std::any view_description_for_cache,
        size_t key,
        ResourceViewType view_type, DescriptorVisibility visibility)
        -> NativeObject;

    OXYGEN_GFX_API void UnRegisterView(const NativeObject& resource, const NativeObject& view);

    OXYGEN_GFX_API void UnRegisterResource(const NativeObject& resource);
    OXYGEN_GFX_API void UnRegisterResourceViews(const NativeObject& resource);

    void UnRegisterViewNoLock(const NativeObject& resource, const NativeObject& view);
    void UnRegisterResourceViewsNoLock(const NativeObject& resource);

    [[nodiscard]] OXYGEN_GFX_API auto Contains(const NativeObject& resource) const -> bool;
    [[nodiscard]] OXYGEN_GFX_API auto Contains(const NativeObject& resource, size_t key_hash) const -> bool;

    [[nodiscard]] OXYGEN_GFX_API auto Find(const NativeObject& resource, size_t key_hash) const -> NativeObject;

    // Core dependencies
    std::shared_ptr<DescriptorAllocator> descriptor_allocator_;

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
        std::unordered_map<uint32_t, ViewEntry> descriptors;
    };

    // Primary storage - Resource NativeObject to resource entry
    std::unordered_map<NativeObject, ResourceEntry> resources_;

    // Map from descriptor index to owning resource
    std::unordered_map<uint32_t, NativeObject> descriptor_to_resource_;

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
};

} // namespace oxygen::graphics
