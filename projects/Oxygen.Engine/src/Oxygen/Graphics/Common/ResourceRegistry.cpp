//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>

using oxygen::graphics::ResourceRegistry;

void ResourceRegistry::Register(std::shared_ptr<void> resource, TypeId type_id)
{
    std::lock_guard lock(registry_mutex_);

    NativeObject key { resource.get(), type_id };
    ResourceEntry entry;
    entry.resource = std::move(resource);
    resources_[key] = std::move(entry);
}

auto ResourceRegistry::RegisterView(
    NativeObject resource, NativeObject view,
    std::any view_description_for_cache, // Added
    size_t key_hash,
    ResourceViewType view_type, DescriptorVisibility visibility)
    -> NativeObject
{
    std::lock_guard lock(registry_mutex_);

    // Check if resource exists
    auto resource_it = resources_.find(resource);
    if (resource_it == resources_.end()) {
        LOG_F(WARNING, "Attempt to register view for unregistered resource: {}",
             nostd::to_string(resource));
        return {};
    }

    // Check view cache first
    CacheKey cache_key { resource, key_hash };
    auto cache_it = view_cache_.find(cache_key);

    if (cache_it != view_cache_.end()) {
        DLOG_F(4, "View cache hit for resource {} (desc hash {})",
            nostd::to_string(resource), key_hash);
        return cache_it->second.view_object;
    }

    // Allocate a descriptor
    DescriptorHandle descriptor = descriptor_allocator_->Allocate(view_type, visibility);

    if (!descriptor.IsValid()) {
        LOG_F(ERROR, "Failed to allocate descriptor for view type: {} (resource: {})",
            nostd::to_string(view_type), nostd::to_string(resource));
        return {};
    }

    // Store in maps
    auto index = descriptor.GetIndex();
    auto& descriptors = resource_it->second.descriptors;
    auto [desc_it, inserted] = descriptors.emplace(
        index,
        ResourceEntry::ViewEntry { view, std::move(descriptor) });
    DLOG_F(4, "Descriptor {} {} for resource {}",
        index, inserted ? "inserted" : "reused", nostd::to_string(resource));
    descriptor_to_resource_[index] = resource;

    // Store in view cache
    ViewCacheEntry cache_entry {
        .view_object = view,
        .view_description = std::move(view_description_for_cache) // Store the original description
    };
    view_cache_[cache_key] = std::move(cache_entry);
    DLOG_F(4, "View cached for resource {} (desc hash {})",
        nostd::to_string(resource), key_hash);

    // Return the view
    DLOG_F(3, "RegisterView: returning view {} for resource {}",
         nostd::to_string(view), nostd::to_string(resource));
    return view;
}

auto ResourceRegistry::Contains(NativeObject resource) const -> bool
{
    std::lock_guard lock(registry_mutex_);
    return resources_.contains(resource);
}

auto ResourceRegistry::Contains(NativeObject resource, size_t key_hash) const -> bool
{
    std::lock_guard lock(registry_mutex_);

    CacheKey cache_key { resource, key_hash };
    return view_cache_.find(cache_key) != view_cache_.end();
}

auto ResourceRegistry::Find(NativeObject resource, size_t key_hash) const -> NativeObject
{
    std::lock_guard lock(registry_mutex_);

    CacheKey cache_key { resource, key_hash };
    auto it = view_cache_.find(cache_key);

    if (it != view_cache_.end()) {
        return it->second.view_object;
    }

    return {}; // Return invalid NativeObject
}

auto ResourceRegistry::Contains(const DescriptorHandle& descriptor) const -> NativeObject
{
    std::lock_guard lock(registry_mutex_);

    if (!descriptor.IsValid()) {
        return {}; // Return invalid NativeObject
    }

    uint32_t index = descriptor.GetIndex();
    auto it = descriptor_to_resource_.find(index);

    // If we have a mapping for this descriptor, return the resource
    if (it != descriptor_to_resource_.end()) {
        return it->second;
    }

    return {}; // Return invalid NativeObject
}

auto ResourceRegistry::Find(const DescriptorHandle& descriptor) const -> NativeObject
{
    std::lock_guard lock(registry_mutex_);

    if (!descriptor.IsValid()) {
        return { 0ULL, kInvalidTypeId }; // Return invalid NativeObject
    }

    uint32_t index = descriptor.GetIndex();
    auto resource_it = descriptor_to_resource_.find(index);

    if (resource_it == descriptor_to_resource_.end()) {
        return { 0ULL, kInvalidTypeId }; // Return invalid NativeObject
    }

    const NativeObject& resource = resource_it->second;
    auto it = resources_.find(resource);
    if (it == resources_.end()) {
        // This shouldn't happen - inconsistent state
        LOG_F(ERROR, "Inconsistent state: descriptor points to non-existent resource");
        return {}; // Return invalid NativeObject
    }

    // Look up the view entry
    auto& descriptors = it->second.descriptors;
    auto view_it = descriptors.find(index);

    if (view_it == descriptors.end()) {
        // Again, shouldn't happen
        LOG_F(ERROR, "Inconsistent state: descriptor not found in resource entry");
        return {}; // Return invalid NativeObject
    }

    return view_it->second.view_object;
}

void ResourceRegistry::UnRegister(const NativeObject& resource)
{
    std::lock_guard lock(registry_mutex_);

    auto it = resources_.find(resource);
    if (it == resources_.end()) {
        DLOG_F(3, "UnRegister: resource {} not found (already unregistered)",
             nostd::to_string(resource));
        return; // Already unregistered
    }

    DLOG_F(2, "UnRegister: removing resource {} and all its views",
        nostd::to_string(resource));
    // First remove all views associated with this resource
    UnRegisterViewsInternal(resource);

    // Now remove the resource itself
    resources_.erase(it);
    DLOG_F(3, "UnRegister: resource {} removed", nostd::to_string(resource));
}

void ResourceRegistry::UnRegisterViews(const NativeObject& resource)
{
    std::lock_guard lock(registry_mutex_);
    DLOG_F(2, "UnRegisterViews: removing all views for resource {}",
         nostd::to_string(resource));
    UnRegisterViewsInternal(resource);
}

// Private helper to avoid lock duplication
void ResourceRegistry::UnRegisterViewsInternal(const NativeObject& resource)
{
    auto it = resources_.find(resource);
    if (it == resources_.end()) {
        DLOG_F(3, "UnRegisterViewsInternal: resource {} not found",
            nostd::to_string(resource));
        return;
    }

    // Get descriptors to release
    auto& descriptors = it->second.descriptors;

    // Release all descriptors
    for (auto& [index, view_entry] : descriptors) {
        DLOG_F(4, "Releasing descriptor {} for resource {}", index,
             nostd::to_string(resource));
        descriptor_to_resource_.erase(index);
        view_entry.descriptor.Release();
    }

    // Clear the descriptors
    descriptors.clear();

    // Remove all view cache entries for this resource
    auto cache_it = view_cache_.begin();
    while (cache_it != view_cache_.end()) {
        if (cache_it->first.resource == resource) {
            DLOG_F(4, "Removing view cache entry for resource {} (desc hash {})",
                nostd::to_string(resource), cache_it->first.hash);
            cache_it = view_cache_.erase(cache_it);
        } else {
            ++cache_it;
        }
    }
    DLOG_F(3, "UnRegisterViewsInternal: all views removed for resource {}",
        nostd::to_string(resource));
}
