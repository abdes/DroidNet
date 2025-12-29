//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <ranges>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>

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

auto ResourceRegistry::Register(std::shared_ptr<void> resource, TypeId type_id)
  -> void
{
  DCHECK_NOTNULL_F(resource, "Resource must not be null");

  std::lock_guard lock(registry_mutex_);

  LOG_SCOPE_F(1, "Register resource");
  DLOG_F(2, "resource : {}", fmt::ptr(resource.get()));
  DLOG_F(2, "type id  : {}", type_id);

  const NativeResource key { resource.get(), type_id };
  if (const auto cache_it = resources_.find(key);
    cache_it != resources_.end()) {
    DLOG_F(2, "cache hit ({})", fmt::ptr(resource.get()));
    // This is a programming error, abort.
    ABORT_F("-failed- use Replace() to replace registered resources");
  }

  ResourceEntry entry {
    .resource = std::move(resource),
    .descriptors = {} // Initialize with empty descriptors
  };
  resources_.emplace(key, std::move(entry));
  DLOG_F(3, "{} resources in registry", resources_.size());
}

auto ResourceRegistry::RegisterView(NativeResource resource, NativeView view,
  DescriptorHandle view_handle, std::any view_description, size_t key_hash,
  [[maybe_unused]] ResourceViewType view_type,
  [[maybe_unused]] DescriptorVisibility visibility) -> NativeView
{
  // The resource native object is constructed from a reference to the resource
  // and its type ID. It must be valid.
  CHECK_F(view_handle.IsValid(), "View handle must be valid");

  // These values are ensured by the ResourceRegistry wrapper methods.
  DCHECK_F(resource->IsValid(), "invalid resource used for view registration");
  DCHECK_F(view_description.has_value(), "View description must be valid");
  DCHECK_F(key_hash != 0, "Key hash must be valid");

  std::lock_guard lock(registry_mutex_);

  LOG_SCOPE_F(1, "Register view");
  DLOG_F(1, "resource: {}", nostd::to_string(resource));
  DLOG_F(1, "view: {}", nostd::to_string(view));
  DLOG_F(1, "view handle: {}", nostd::to_string(view_handle));
  DLOG_F(3, "view type: {}, visibility: {}", view_type, visibility);
  DLOG_F(3, "key hash: {}", key_hash);

  // View native object is obtained from the Graphics API, and this may fail for
  // various reasons.
  if (!view->IsValid()) {
    LOG_F(ERROR, "-failed- invalid view used for view registration");
    return {};
  }

  // Check if resource exists
  const auto resource_it = resources_.find(resource);
  if (resource_it == resources_.end()) {
    LOG_F(ERROR, "-failed- resource not found");
    return {};
  }

  // Check view cache first
  const CacheKey cache_key { .resource = resource, .view_desc_hash = key_hash };
  if (const auto cache_it = view_cache_.find(cache_key);
    cache_it != view_cache_.end()) {
    DLOG_F(2, "cache hit ({})", cache_it->second.view_object);
    // This is a programming error, abort.
    ABORT_F("-failed- use UpdateView() to update registered views");
  }

  // Store in maps
  auto index = view_handle.GetBindlessHandle();
  auto& descriptors = resource_it->second.descriptors;
  auto [desc_it, inserted] = descriptors.emplace(index,
    ResourceEntry::ViewEntry {
      .view_object = view,
      .descriptor = std::move(view_handle),
    });
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
  DLOG_F(3, "returning view {}", view, resource);
  return view;
}

auto ResourceRegistry::Contains(const NativeResource& resource) const -> bool
{
  std::lock_guard lock(registry_mutex_);
  return resources_.contains(resource);
}

auto ResourceRegistry::Contains(
  const NativeResource& resource, const size_t key_hash) const -> bool
{
  std::lock_guard lock(registry_mutex_);

  const CacheKey cache_key { .resource = resource, .view_desc_hash = key_hash };
  return view_cache_.contains(cache_key);
}

auto ResourceRegistry::Find(
  const NativeResource& resource, const size_t key_hash) const -> NativeView
{
  std::lock_guard lock(registry_mutex_);

  const CacheKey cache_key { .resource = resource, .view_desc_hash = key_hash };

  if (const auto it = view_cache_.find(cache_key); it != view_cache_.end()) {
    return it->second.view_object;
  }

  return {}; // Return invalid NativeView
}

auto ResourceRegistry::UnRegisterView(
  const NativeResource& resource, const NativeView& view) -> void
{
  std::lock_guard lock(registry_mutex_);
  UnRegisterViewNoLock(resource, view);
}

auto ResourceRegistry::UnRegisterViewNoLock(
  const NativeResource& resource, const NativeView& view) -> void
{
  LOG_SCOPE_F(3, "UnRegister view");
  DLOG_F(3, "resource : {}", resource);
  DLOG_F(3, "view     : {}", view);

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
  [[maybe_unused]] const size_t erased_count
    = std::erase_if(view_cache_, [&resource, &view](const auto& cache_pair) {
        return cache_pair.first.resource == resource
          && cache_pair.second.view_object == view;
      });
  DCHECK_EQ_F(erased_count, 1,
    "Cache entry not found for resource {} and view {}", resource, view);
}

auto ResourceRegistry::UnRegisterResource(const NativeResource& resource)
  -> void
{
  std::lock_guard lock(registry_mutex_);
  const auto it = resources_.find(resource);
  if (it == resources_.end()) {
    DLOG_F(3,
      "UnRegisterResource: resource {} not found (already unregistered)",
      resource);
    return;
  }
  DLOG_F(
    2, "UnRegisterResource: removing resource {} and all its views", resource);
  UnRegisterResourceViewsNoLock(resource);
  resources_.erase(it);
  DLOG_F(3, "UnRegisterResource: resource {} removed", resource);
}

auto ResourceRegistry::UnRegisterResourceViews(const NativeResource& resource)
  -> void
{
  std::lock_guard lock(registry_mutex_);

  LOG_SCOPE_F(2, "UnRegisterResourceViews");
  DLOG_F(2, "resource {}", nostd::to_string(resource));
  UnRegisterResourceViewsNoLock(resource);
}

// Private helper to avoid lock duplication
auto ResourceRegistry::UnRegisterResourceViewsNoLock(
  const NativeResource& resource) -> void
{
  LOG_SCOPE_F(3, "UnRegisterResourceViews");
  DLOG_F(2, "resource : {}", resource);

  const auto it = resources_.find(resource);
  if (it == resources_.end()) {
    // Contrarily to UnRegisterView, this is not an error. We just log and
    // return. We consider that when unregistering a specific view, there is an
    // implicit assumption that the resource is still there and may have other
    // views.
    DLOG_F(3, "resource not found -> nothing to un-register");
    return;
  }

  auto& [_, descriptors] = it->second;
  if (descriptors.empty()) {
    DLOG_F(4, "no views to un-register");
    return;
  }

  [[maybe_unused]] const size_t view_count = descriptors.size();
  DLOG_F(2, "{} view{} to un-register", view_count, view_count == 1 ? "" : "s");

  // Release all descriptors and remove from descriptor_to_resource_ map
  for (auto& [index, view_entry] : descriptors) {
    DLOG_F(3, "view for index {}", view_entry.descriptor.GetBindlessHandle());
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

//=== Internal helpers ----------------------------------------------------//

auto ResourceRegistry::PurgeCachedViewsForResource(
  const NativeResource& resource) -> void
{
  // Remove all relevant entries from view_cache_ in a single pass
  std::erase_if(view_cache_, [&resource](const auto& cache_entry) {
    return cache_entry.first.resource == resource;
  });
}

auto ResourceRegistry::AttachDescriptorWithView(
  const NativeResource& dst_resource, const bindless::HeapIndex index,
  DescriptorHandle descriptor_handle, const NativeView& view,
  std::any description, const std::size_t key_hash) -> void
{
  DCHECK_F(view->IsValid(), "invalid native view object");
  const auto it = resources_.find(dst_resource);
  DCHECK_F(it != resources_.end(), "destination resource not registered: {}",
    dst_resource);
  it->second.descriptors[index]
    = ResourceEntry::ViewEntry { .view_object = view,
        .descriptor = std::move(descriptor_handle) };
  descriptor_to_resource_[index] = dst_resource;

  // Update cache entry
  ViewCacheEntry cache_entry { .view_object = view,
    .view_description = std::move(description) };
  const CacheKey new_cache_key { .resource = dst_resource,
    .view_desc_hash = key_hash };
  view_cache_[new_cache_key] = std::move(cache_entry);
}

auto ResourceRegistry::CollectDescriptorIndicesForResource(
  const NativeResource& resource) const -> std::vector<bindless::HeapIndex>
{
  const auto it = resources_.find(resource);
  if (it == resources_.end()) {
    return {};
  }
  std::vector<bindless::HeapIndex> out;
  out.reserve(it->second.descriptors.size());
  for (const auto& idx : it->second.descriptors | std::views::keys) {
    out.push_back(idx);
  }
  return out;
}
