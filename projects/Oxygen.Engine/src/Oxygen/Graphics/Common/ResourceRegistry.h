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
#include <Oxygen/Base/Logging.h>
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
    explicit ResourceRegistry(std::shared_ptr<DescriptorAllocator> allocator)
        : descriptor_allocator_(std::move(allocator))
    {
    }

    ~ResourceRegistry()
    {
    }

    OXYGEN_MAKE_NON_COPYABLE(ResourceRegistry)
    OXYGEN_DEFAULT_MOVABLE(ResourceRegistry)

    //! Register a graphics resource, such as textures and buffers.
    /*!
     The registry will keep a strong reference to the resource until it is
     unregistered. Therefore, the recommended lifetime management process is to
     un-register the resource when it is no longer needed, which will also
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

    OXYGEN_GFX_API auto Contains(const DescriptorHandle& descriptor) const -> NativeObject;

    template <ResourceWithViews Resource>
    auto Contains(const Resource& resource) const -> bool
    {
        return Contains(NativeObject(&resource, Resource::ClassTypeId()));
    }

    template <ResourceWithViews Resource>
    auto Contains(
        const Resource& resource,
        const typename Resource::ViewDescriptionT& desc) const -> bool
    {
        auto key = std::hash<std::remove_cvref_t<decltype(desc)>> {}(desc);
        return Contains(NativeObject(const_cast<Resource*>(&resource), Resource::ClassTypeId()), key);
    }

    //! Get native view for a descriptor
    OXYGEN_GFX_API auto Find(const DescriptorHandle& descriptor) const -> NativeObject;

    template <ResourceWithViews Resource>
    auto Find(
        const Resource& resource,
        const typename Resource::ViewDescriptionT& desc) const
        -> NativeObject
    {
        auto key = std::hash<std::remove_cvref_t<decltype(desc)>> {}(desc);
        return Find(NativeObject(const_cast<Resource*>(&resource), Resource::ClassTypeId()), key);
    }

    template <SupportedResource Resource>
    void UnRegister(const Resource& resource)
    {
        UnRegister(NativeObject { const_cast<Resource*>(&resource), Resource::ClassTypeId() });
    }

    //! Release all views for a resource
    OXYGEN_GFX_API void UnRegisterViews(const NativeObject& resource);

private:
    OXYGEN_GFX_API void Register(std::shared_ptr<void> resource, TypeId type_id);

    OXYGEN_GFX_API auto RegisterView(
        NativeObject resource, NativeObject view,
        std::any view_description_for_cache,
        size_t key,
        ResourceViewType view_type, DescriptorVisibility visibility)
        -> NativeObject;

    OXYGEN_GFX_API void UnRegister(const NativeObject& resource);
    OXYGEN_GFX_API void UnRegisterViewsInternal(const NativeObject& resource);

    OXYGEN_GFX_API auto Contains(NativeObject resource) const -> bool;
    OXYGEN_GFX_API auto Contains(NativeObject resource, size_t key) const -> bool;

    OXYGEN_GFX_API auto Find(NativeObject resource, size_t key) const -> NativeObject;

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

        bool operator==(const CacheKey& other) const
        {
            return hash == other.hash && resource == other.resource;
        }
    };

    //! A custom hash functor for CacheKey.
    struct CacheKeyHasher {
        std::size_t operator()(const CacheKey& k) const noexcept
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

    //! A unified view cache for all resource and view types.
    std::unordered_map<CacheKey, ViewCacheEntry, CacheKeyHasher> view_cache_;
};

} // namespace oxygen::graphics
