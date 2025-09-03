//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <ranges>

using oxygen::graphics::ResourceRegistry;

ResourceRegistry::ResourceRegistry(std::string_view debug_name)
  : debug_name_(debug_name)
{
  DLOG_F(1, "ResourceRegistry `{}` created.", debug_name_);
}

ResourceRegistry::~ResourceRegistry() noexcept
{
  try {
    LOG_SCOPE_FUNCTION(INFO);

    // Note: we don't clean up any of the native objects corresponding to
    // the resources or their views. We do, however, have to release the
    // descriptors for views in the cache that were not unregistered before
    // the registry is destroyed. This may indicate bad resource management
    // in the client code, or that the renderer is shutting down and some
    // permanent resources are still in the registry. In any case, we must
    // leave the allocator in a clean state.

    std::lock_guard lock(registry_mutex_);

    const size_t resource_count = resources_.size();
    if (resource_count == 0) {
      return;
    }

    DLOG_F(1, "{} resource{} still registered", resource_count,
      resource_count == 1 ? "" : "s");
    {
      for (auto& [resource, entry] : resources_) {
        auto view_count = entry.descriptors.size();
        DLOG_F(1, "resource `{}` with {} view{}", nostd::to_string(resource),
          view_count, view_count == 1 ? "" : "s");
        if (view_count > 0) {
          LOG_SCOPE_F(4, "releasing resource descriptors");
          for (auto& [_, descriptor] : entry.descriptors | std::views::values) {
            if (descriptor.IsValid()) {
              descriptor.Release();
            }
          }
        }
        // Release the reference to the resource
        entry.resource.reset();
      }
    }

    // The rest will be done automatically when the different collections are
    // destroyed.
  } catch (...) { // NOLINT(bugprone-empty-catch)
    // Swallow all exceptions to guarantee noexcept
  }
}

void ResourceRegistry::Register(std::shared_ptr<void> resource, TypeId type_id)
{
  DCHECK_NOTNULL_F(resource, "Resource must not be null");

  std::lock_guard lock(registry_mutex_);

  LOG_SCOPE_F(1, "Register resource");
  DLOG_F(3, "resource: {}, type id: {}", fmt::ptr(resource.get()),
    nostd::to_string(type_id));

  const NativeObject key { resource.get(), type_id };
  if (const auto cache_it = resources_.find(key);
    cache_it != resources_.end()) {
    DLOG_F(4, "cache hit ({})-> ", fmt::ptr(resource.get()));
    DLOG_F(4,
      "resource already registered, "
      "use Replace() to replace the resource and its views");
    throw std::runtime_error("Resource already registered");
  }

  ResourceEntry entry {
    .resource = std::move(resource),
    .descriptors = {} // Initialize with empty descriptors
  };
  resources_.emplace(key, std::move(entry));
  DLOG_F(3, "{} resources in registry", resources_.size());
}

auto ResourceRegistry::RegisterView(NativeObject resource, NativeObject view,
  DescriptorHandle view_handle, std::any view_description, size_t key_hash,
  ResourceViewType view_type, DescriptorVisibility visibility) -> NativeObject
{
  CHECK_F(view_description.has_value(), "View description must be valid");
  CHECK_F(key_hash != 0, "Key hash must be valid");
  CHECK_F(view_handle.IsValid(), "View handle must be valid");

  // The resource native object is constructed from a reference to the resource
  // and its type ID. It must be valid.
  DCHECK_F(resource.IsValid(), "invalid resource used for view registration");

  std::lock_guard lock(registry_mutex_);

  LOG_SCOPE_F(1, "Register view");
  DLOG_F(1, "resource: {}", nostd::to_string(resource));
  DLOG_F(1, "view: {}", nostd::to_string(view));
  DLOG_F(1, "view handle: {}", nostd::to_string(view_handle));
  DLOG_F(3, "view type: {}, visibility: {}", nostd::to_string(view_type),
    nostd::to_string(visibility));
  DLOG_F(3, "key hash: {}", key_hash);

  if (!view.IsValid()) {
    LOG_F(ERROR, "invalid view used for view registration");
    return {};
  }

  // Check if resource exists
  const auto resource_it = resources_.find(resource);
  if (resource_it == resources_.end()) {
    LOG_F(WARNING, "resource not found -> failed");
    return {};
  }

  // Check view cache first
  const CacheKey cache_key { .resource = resource, .hash = key_hash };
  if (const auto cache_it = view_cache_.find(cache_key);
    cache_it != view_cache_.end()) {
    DLOG_F(
      4, "cache hit ({})-> ", nostd::to_string(cache_it->second.view_object));
    DLOG_F(4,
      "view already registered, "
      "use UpdateView() to update while keeping the same descriptor");
    throw std::runtime_error("View already registered");
  }

  // Store in maps
  auto index = view_handle.GetIndex();
  auto& descriptors = resource_it->second.descriptors;
  auto [desc_it, inserted] = descriptors.emplace(index,
    ResourceEntry::ViewEntry {
      .view_object = view, .descriptor = std::move(view_handle) });
  DLOG_F(4, "updated descriptors map with index {} ({})", index,
    inserted ? "inserted" : "reused");
  descriptor_to_resource_[index] = resource;

  // Store in view cache
  ViewCacheEntry cache_entry {
    .view_object = view,
    .view_description
    = std::move(view_description) // Store the original description
  };
  view_cache_[cache_key] = std::move(cache_entry);
  DLOG_F(4, "updated cache");

  // Return the view
  DLOG_F(
    3, "returning view {}", nostd::to_string(view), nostd::to_string(resource));
  return view;
}

auto ResourceRegistry::Contains(const NativeObject& resource) const -> bool
{
  std::lock_guard lock(registry_mutex_);
  return resources_.contains(resource);
}

auto ResourceRegistry::Contains(
  const NativeObject& resource, const size_t key_hash) const -> bool
{
  std::lock_guard lock(registry_mutex_);

  const CacheKey cache_key { .resource = resource, .hash = key_hash };
  return view_cache_.contains(cache_key);
}

auto ResourceRegistry::Find(
  const NativeObject& resource, const size_t key_hash) const -> NativeObject
{
  std::lock_guard lock(registry_mutex_);

  const CacheKey cache_key { .resource = resource, .hash = key_hash };

  if (const auto it = view_cache_.find(cache_key); it != view_cache_.end()) {
    return it->second.view_object;
  }

  return {}; // Return invalid NativeObject
}

auto ResourceRegistry::Contains(const DescriptorHandle& descriptor) const
  -> NativeObject
{
  std::lock_guard lock(registry_mutex_);

  if (!descriptor.IsValid()) {
    return {}; // Return invalid NativeObject
  }

  const auto index = descriptor.GetIndex();
  // If we have a mapping for this descriptor, return the resource
  if (const auto it = descriptor_to_resource_.find(index);
    it != descriptor_to_resource_.end()) {
    return it->second;
  }

  return {}; // Return invalid NativeObject
}

auto ResourceRegistry::Find(const DescriptorHandle& descriptor) const
  -> NativeObject
{
  std::lock_guard lock(registry_mutex_);

  if (!descriptor.IsValid()) {
    return { 0ULL, kInvalidTypeId }; // Return invalid NativeObject
  }

  const auto index = descriptor.GetIndex();
  const auto resource_it = descriptor_to_resource_.find(index);

  if (resource_it == descriptor_to_resource_.end()) {
    return { 0ULL, kInvalidTypeId }; // Return invalid NativeObject
  }

  const NativeObject& resource = resource_it->second;
  const auto it = resources_.find(resource);
  if (it == resources_.end()) {
    // This shouldn't happen - inconsistent state
    LOG_F(
      ERROR, "Inconsistent state: descriptor points to non-existent resource");
    return {}; // Return invalid NativeObject
  }

  // Look up the view entry
  auto& descriptors = it->second.descriptors;
  const auto view_it = descriptors.find(index);

  if (view_it == descriptors.end()) {
    // Again, shouldn't happen
    LOG_F(ERROR, "Inconsistent state: descriptor not found in resource entry");
    return {}; // Return invalid NativeObject
  }

  return view_it->second.view_object;
}

void ResourceRegistry::UnRegisterView(
  const NativeObject& resource, const NativeObject& view)
{
  std::lock_guard lock(registry_mutex_);
  UnRegisterViewNoLock(resource, view);
}

void ResourceRegistry::UnRegisterViewNoLock(
  const NativeObject& resource, const NativeObject& view)
{
  LOG_SCOPE_F(3, "UnRegister view");
  DLOG_F(3, "resource: {}", nostd::to_string(resource));
  DLOG_F(3, "view: {}", nostd::to_string(view));

  const auto it = resources_.find(resource);
  if (it == resources_.end()) {
    DLOG_F(3, "resource not found -> throw");
    throw std::runtime_error("resource not found while un-registering view");
  }

  auto& descriptors = it->second.descriptors;
  // Remove the descriptor with the matching view_object (only one possible)
  const auto desc_it = std::ranges::find_if(descriptors,
    [&](const auto& pair) { return pair.second.view_object == view; });
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
  const size_t erased_count
    = std::erase_if(view_cache_, [&resource, &view](const auto& cache_pair) {
        return cache_pair.first.resource == resource
          && cache_pair.second.view_object == view;
      });
  DCHECK_EQ_F(erased_count, 1,
    "Cache entry not found for resource {} and view {}",
    nostd::to_string(resource), nostd::to_string(view));
}

void ResourceRegistry::UnRegisterResource(const NativeObject& resource)
{
  std::lock_guard lock(registry_mutex_);
  const auto it = resources_.find(resource);
  if (it == resources_.end()) {
    DLOG_F(3,
      "UnRegisterResource: resource {} not found (already unregistered)",
      nostd::to_string(resource));
    return;
  }
  DLOG_F(2, "UnRegisterResource: removing resource {} and all its views",
    nostd::to_string(resource));
  UnRegisterResourceViewsNoLock(resource);
  resources_.erase(it);
  DLOG_F(
    3, "UnRegisterResource: resource {} removed", nostd::to_string(resource));
}

void ResourceRegistry::UnRegisterResourceViews(const NativeObject& resource)
{
  std::lock_guard lock(registry_mutex_);

  LOG_SCOPE_F(2, "UnRegisterResourceViews");
  DLOG_F(2, "resource {}", nostd::to_string(resource));
  UnRegisterResourceViewsNoLock(resource);
}

// Private helper to avoid lock duplication
void ResourceRegistry::UnRegisterResourceViewsNoLock(
  const NativeObject& resource)
{
  LOG_SCOPE_F(3, "UnRegister all views for resource");
  DLOG_F(3, "resource: {}", nostd::to_string(resource));

  const auto it = resources_.find(resource);
  if (it == resources_.end()) {
    DLOG_F(3, "resource not found -> nothing to un-register");
    return;
  }

  auto& [_, descriptors] = it->second;
  if (descriptors.empty()) {
    DLOG_F(4, "no views to un-register");
    return;
  }

  const size_t view_count = descriptors.size();
  DLOG_F(4, "{} view{} to un-register", view_count, view_count == 1 ? "" : "s");

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
