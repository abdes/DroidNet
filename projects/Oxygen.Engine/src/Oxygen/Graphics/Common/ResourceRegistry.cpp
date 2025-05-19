//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>

using oxygen::graphics::ResourceRegistry;

ResourceRegistry::ResourceRegistry(std::shared_ptr<DescriptorAllocator> allocator)
    : descriptor_allocator_(std::move(allocator))
{
    DCHECK_NOTNULL_F(descriptor_allocator_, "Descriptor allocator must not be null");

    DLOG_F(INFO, "ResourceRegistry created.");
}

ResourceRegistry::~ResourceRegistry() noexcept
{
    try {
        // Note: we do not cleanup any of the native objects corresponding to the
        // resources or their views. We do however have to release the descriptors
        // for views in the cache that were not unregistered before the registry is
        // destroyed. This may indicate bad resource management in the client code,
        // or that the renderer is shutting down and some permanent resources are
        // still in the registry. In any case, we must leave the allocator in a
        // clean state.

        std::lock_guard lock(registry_mutex_);

        size_t resource_count = resources_.size();
        if (resource_count == 0) {
            DLOG_F(INFO, "Resource registry destroyed.");
            return;
        }

        DLOG_F(INFO, "Registry destroyed with {} resource{} still registered",
            resource_count, resource_count == 1 ? "" : "s");
        {
            LOG_SCOPE_FUNCTION(INFO);
            for (auto& [resource, entry] : resources_) {
                auto view_count = entry.descriptors.size();
                DLOG_F(INFO, "Resource `{}` with {} view{}",
                    nostd::to_string(resource), view_count, view_count == 1 ? "" : "s");
                if (view_count > 0) {
                    LOG_SCOPE_F(4, "Releasing descriptors");
                    for (auto& [index, view_entry] : entry.descriptors) {
                        if (view_entry.descriptor.IsValid()) {
                            view_entry.descriptor.Release();
                        }
                    }
                }
                // Release the strong reference to the resource
                entry.resource.reset();
            }
        }
        DLOG_F(INFO, "Resource registry destroyed.");

        // The rest will be done automatically when the different collections are
        // destroyed.
    } catch (...) {
        // Swallow all exceptions to guarantee noexcept
    }
}

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

    // BUG: this should be an error if the view is already registered.
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

void ResourceRegistry::UnRegisterView(const NativeObject& resource, const NativeObject& view)
{
    std::lock_guard lock(registry_mutex_);
    UnRegisterViewNoLock(resource, view);
}

void ResourceRegistry::UnRegisterViewNoLock(const NativeObject& resource, const NativeObject& view)
{
    LOG_SCOPE_F(3, "Unregister view");
    DLOG_F(3, "resource: {}", nostd::to_string(resource));
    DLOG_F(3, "view: {}", nostd::to_string(view));

    auto it = resources_.find(resource);
    if (it == resources_.end()) {
        DLOG_F(3, "resource not found -> throw");
        throw std::runtime_error("Resource not found while unregistering view");
    }

    auto& descriptors = it->second.descriptors;
    // Remove the descriptor with the matching view_object (only one possible)
    auto desc_it = std::ranges::find_if(descriptors, [&](const auto& pair) {
        return pair.second.view_object == view;
    });
    if (desc_it == descriptors.end()) {
        DLOG_F(3, "view not found, already unregistered?");
        return; // Nothing to do
    }

    DLOG_F(4, "release view descriptor handle ({})", desc_it->first);
    descriptor_to_resource_.erase(desc_it->first);
    desc_it->second.descriptor.Release();
    descriptors.erase(desc_it);

    DLOG_F(4, "remove cache entry");
    // Use std::erase_if to efficiently find and remove the matching cache entry
    size_t erased_count = std::erase_if(view_cache_, [&resource, &view](const auto& cache_pair) {
        return cache_pair.first.resource == resource && cache_pair.second.view_object == view;
    });
    DCHECK_EQ_F(erased_count, 1, "Cache entry not found for resource {} and view {}",
         nostd::to_string(resource), nostd::to_string(view));
}

void ResourceRegistry::UnRegisterResource(const NativeObject& resource)
{
    std::lock_guard lock(registry_mutex_);
    auto it = resources_.find(resource);
    if (it == resources_.end()) {
        DLOG_F(3, "UnRegisterResource: resource {} not found (already unregistered)", nostd::to_string(resource));
        return;
    }
    DLOG_F(2, "UnRegisterResource: removing resource {} and all its views", nostd::to_string(resource));
    UnRegisterResourceViewsNoLock(resource);
    resources_.erase(it);
    DLOG_F(3, "UnRegisterResource: resource {} removed", nostd::to_string(resource));
}

void ResourceRegistry::UnRegisterResourceViews(const NativeObject& resource)
{
    LOG_SCOPE_F(2, fmt::format("Unregister all views for resource `{}`", nostd::to_string(resource)).c_str());
    std::lock_guard lock(registry_mutex_);
    UnRegisterResourceViewsNoLock(resource);
}

// Private helper to avoid lock duplication
void ResourceRegistry::UnRegisterResourceViewsNoLock(const NativeObject& resource)
{
    LOG_SCOPE_F(3, "Unregister all views for resource");
    DLOG_F(3, "resource: {}", nostd::to_string(resource));

    auto it = resources_.find(resource);
    if (it == resources_.end()) {
        DLOG_F(3, "resource not found -> nothing to unregister");
        return;
    }

    auto& resource_entry = it->second;
    auto& descriptors = resource_entry.descriptors;
    if (descriptors.empty()) {
        DLOG_F(4, "no views to unregister");
        return;
    }

    size_t view_count = descriptors.size();
    DLOG_F(4, "{} view{} to unregister", view_count, view_count == 1 ? "" : "s");

    // Release all descriptors and remove from descriptor_to_resource_ map
    for (auto& [index, view_entry] : descriptors) {
        DLOG_F(4, "view for index {}", view_entry.descriptor.GetIndex());
        if (view_entry.descriptor.IsValid()) {
            view_entry.descriptor.Release();
            descriptor_to_resource_.erase(index);
        }
    }

    // Remove all relevant entries from view_cache in a single pass
    std::erase_if(view_cache_, [&resource](const auto& cache_entry) {
        return cache_entry.first.resource == resource;
    });

    // Clear descriptors map
    descriptors.clear();
}
