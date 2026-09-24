//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <ranges>
#include <unordered_set>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>

using oxygen::graphics::ResourceRegistry;

ResourceRegistry::ResourceRegistry(std::string_view debug_name)
  : state_(std::make_shared<detail::ResourceRegistryState>())
  , registry_mutex_(state_->mutex)
  , closed_(state_->closed)
  , resources_(state_->resources)
  , descriptor_to_resource_(state_->descriptor_to_resource)
  , view_cache_(state_->view_cache)
  , on_resource_unregistered_(state_->on_resource_unregistered)
  , debug_name_(debug_name)
{
  DLOG_F(1, "ResourceRegistry `{}` created.", debug_name_);
}

ResourceRegistry::~ResourceRegistry() noexcept
{
  try {
    // Component teardown may already hold the facade's composition lock. Close
    // explicitly while Graphics is alive; destructor cleanup cannot call it.
    SetResourceUnregisteredCallback({});
    Close();
  } catch (...) {
    // All valid descriptor releases are allocation-free. User callbacks cannot
    // escape destruction of a registry facade.
  }
}
auto ResourceRegistry::Register(std::shared_ptr<void> resource, TypeId type_id,
  NativeResource native_resource) -> void
{
  CHECK_NOTNULL_F(resource, "Resource must not be null");

  std::scoped_lock lock(registry_mutex_);
  if (closed_) {
    throw std::logic_error("Resource registry is closed");
  }

  LOG_SCOPE_F(1, "Register resource");
  DLOG_F(2, "resource : {}", fmt::ptr(resource.get()));
  DLOG_F(2, "type id  : {}", type_id);

  const NativeResource key { resource.get(), type_id };
  RequireManualNoLock(key);
  if (const auto cache_it = resources_.find(key);
    cache_it != resources_.end()) {
    DLOG_F(2, "cache hit ({})", fmt::ptr(resource.get()));
    ABORT_F("resource `{}` is already registered; use Replace() or explicit "
            "Contains()/ownership discipline instead",
      fmt::ptr(resource.get()));
  }

  InsertManualNoLock(std::move(resource), type_id, native_resource);
  DLOG_F(3, "{} resources in registry", resources_.size());
}

auto ResourceRegistry::AcquireRegistration(std::shared_ptr<void> resource,
  TypeId type_id, NativeResource native_resource) -> bool
{
  CHECK_NOTNULL_F(resource, "Resource must not be null");

  std::scoped_lock lock(registry_mutex_);
  if (closed_) {
    throw std::logic_error("Resource registry is closed");
  }

  const NativeResource key { resource.get(), type_id };
  RequireManualNoLock(key);
  if (resources_.contains(key)) {
    return false;
  }

  InsertManualNoLock(std::move(resource), type_id, native_resource);
  DLOG_F(3, "{} resources in registry", resources_.size());
  return true;
}

auto ResourceRegistry::RegisterViewNoLock(NativeResource resource,
  NativeView view, DescriptorAllocationHandle view_handle,
  std::any view_description, size_t key_hash,
  [[maybe_unused]] ResourceViewType view_type,
  [[maybe_unused]] DescriptorVisibility visibility, ViewQuery query)
  -> NativeView
{
  // The resource native object is constructed from a reference to the resource
  // and its type ID. It must be valid.
  CHECK_F(view_handle.IsValid(), "View handle must be valid");

  // These values are ensured by the ResourceRegistry wrapper methods.
  DCHECK_F(resource->IsValid(), "invalid resource used for view registration");
  DCHECK_F(view_description.has_value(), "View description must be valid");

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
  RequireManualNoLock(resource);

  // Check view cache first
  const CacheKey cache_key { .resource = resource, .view_desc_hash = key_hash };
  if (const auto* cached = FindViewNoLock(resource, key_hash, query)) {
    DLOG_F(2, "cache hit ({})", cached->view_object);
    // This is a programming error, abort.
    ABORT_F("-failed- use UpdateView() to update registered views");
  }

  // Store in maps
  auto index = view_handle.GetBindlessHandle();
  auto& descriptors = resource_it->second.descriptors;
  auto desc_it = descriptors.find(index);
  const bool inserted = (desc_it == descriptors.end());
  if (inserted) {
    auto [it, _] = descriptors.emplace(index,
      ResourceEntry::ViewEntry {
        .view_object = view,
        .descriptor = std::move(view_handle),
      });
    desc_it = it;
  } else {
    // Descriptor index reuse: replace the previous entry and purge any stale
    // cache keys that still point to the old view object.
    if (desc_it->second.descriptor.IsValid()) {
      desc_it->second.descriptor.Release();
    }
    const auto old_view = desc_it->second.view_object;
    desc_it->second.view_object = view;
    desc_it->second.descriptor = std::move(view_handle);

    [[maybe_unused]] const auto stale_count
      = std::erase_if(view_cache_, [&](const auto& cache_pair) -> auto {
          return cache_pair.first.resource == resource
            && cache_pair.second.view_object == old_view;
        });
    DLOG_F(3,
      "RegisterView replaced existing descriptor index {} (purged {} "
      "stale cache entr{})",
      index, stale_count, stale_count == 1 ? "y" : "ies");
  }
  DLOG_F(4, "updated descriptors map with index {} ({})", index,
    inserted ? "inserted" : "replaced");
  descriptor_to_resource_[index] = resource;

  // Store in view cache
  ViewCacheEntry cache_entry {
    .view_object = view,
    .view_description = std::move(view_description),
    .descriptor_index = index,
  };
  StoreViewNoLock(cache_key, std::move(cache_entry), query);
  DLOG_F(4, "updated cache");

  // Return the view
  DLOG_F(3, "returning view {}", view, resource);
  return view;
}

auto ResourceRegistry::Contains(const NativeResource& resource) const -> bool
{
  std::scoped_lock lock(registry_mutex_);
  return resources_.contains(resource);
}

auto ResourceRegistry::Contains(const NativeResource& resource,
  const size_t key_hash, ViewQuery query) const -> bool
{
  std::scoped_lock lock(registry_mutex_);

  return FindViewNoLock(resource, key_hash, query) != nullptr;
}

auto ResourceRegistry::Find(const NativeResource& resource,
  const size_t key_hash, ViewQuery query) const -> NativeView
{
  std::scoped_lock lock(registry_mutex_);

  if (const auto* cached = FindViewNoLock(resource, key_hash, query)) {
    return cached->view_object;
  }

  return {}; // Return invalid NativeView
}

auto ResourceRegistry::GetRegisteredResourceCount() const noexcept -> size_t
{
  std::scoped_lock lock(registry_mutex_);
  return resources_.size();
}

auto ResourceRegistry::SetResourceUnregisteredCallback(
  std::function<void(const NativeResource&)> callback) -> void
{
  std::scoped_lock lock(registry_mutex_);
  on_resource_unregistered_ = std::move(callback);
}

auto ResourceRegistry::FindShaderVisibleIndex(
  const NativeResource& resource, size_t key_hash, ViewQuery query) const
  -> std::optional<bindless::ShaderVisibleIndex>
{
  std::scoped_lock lock(registry_mutex_);

  const auto* cached = FindViewNoLock(resource, key_hash, query);
  if (!cached) {
    return std::nullopt;
  }

  const NativeView view_obj = cached->view_object;

  // Find the resource entry and search its descriptor map for the matching
  // view object to obtain the descriptor handle and therefore the
  // shader-visible index.
  const auto res_it = resources_.find(resource);
  if (res_it == resources_.end()) {
    return std::nullopt;
  }

  const auto& descriptors = res_it->second.managed
    ? res_it->second.managed->descriptors
    : res_it->second.descriptors;
  for (const auto& [index, ve] : descriptors) {
    if (ve.view_object == view_obj) {
      if (ve.descriptor.IsValid() && ve.descriptor.GetAllocator() != nullptr) {
        return ve.descriptor.GetAllocator()->GetShaderVisibleIndex(
          ve.descriptor);
      }
      return std::nullopt;
    }
  }

  return std::nullopt;
}

auto ResourceRegistry::UnRegisterView(
  const NativeResource& resource, const NativeView& view) -> void
{
  std::scoped_lock lock(registry_mutex_);
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
  RequireManualNoLock(resource);

  auto& descriptors = it->second.descriptors;
  // Remove all descriptors with the matching view_object.
  size_t removed_descriptor_count = 0;
  for (auto desc_it = descriptors.begin(); desc_it != descriptors.end();) {
    if (desc_it->second.view_object != view) {
      ++desc_it;
      continue;
    }

    DLOG_F(4, "release view descriptor handle ({})", desc_it->first);
    descriptor_to_resource_.erase(desc_it->first);
    desc_it->second.descriptor.Release();
    desc_it = descriptors.erase(desc_it);
    ++removed_descriptor_count;
  }

  if (removed_descriptor_count == 0) {
    DLOG_F(3, "view not found, already unregistered?");
    return; // Nothing to do
  }

  DLOG_F(4, "remove cache entry");
  // Remove all matching cache entries; duplicates may exist after descriptor
  // index reuse with backend view-handle aliasing.
  [[maybe_unused]] const size_t erased_count = std::erase_if(
    view_cache_, [&resource, &view](const auto& cache_pair) -> auto {
      return cache_pair.first.resource == resource
        && cache_pair.second.view_object == view;
    });
  DCHECK_GE_F(erased_count, 1,
    "Cache entry not found for resource {} and view {}", resource, view);
}

auto ResourceRegistry::UnRegisterViewBatch(const NativeResource& resource,
  const std::span<const NativeView> views) -> void
{
  std::scoped_lock lock(registry_mutex_);
  RequireManualNoLock(resource);
  if (views.empty()) {
    return;
  }
  const auto selected
    = std::unordered_set<NativeView>(views.begin(), views.end());
  const auto owner = resources_.find(resource);
  if (owner == resources_.end()) {
    throw std::runtime_error("resource not found while un-registering views");
  }
  auto& descriptors = owner->second.descriptors;
  for (auto it = descriptors.begin(); it != descriptors.end();) {
    if (!selected.contains(it->second.view_object)) {
      ++it;
      continue;
    }
    descriptor_to_resource_.erase(it->first);
    it->second.descriptor.Release();
    it = descriptors.erase(it);
  }
  std::erase_if(view_cache_, [&](const auto& entry) {
    return entry.first.resource == resource
      && selected.contains(entry.second.view_object);
  });
}

auto ResourceRegistry::Close() -> void
{
  std::scoped_lock lock(registry_mutex_);
  closed_ = true;
  for (const auto& [native, ownership] : state_->native_ownership) {
    NotifyResourceForgottenNoLock(native);
  }
  for (auto& [resource, entry] : resources_) {
    if (entry.managed) {
      entry.managed->open = false;
      entry.managed->published = false;
      entry.managed->queued = false;
      entry.managed->ready_next = nullptr;
    }
    entry.descriptors.clear();
  }
  state_->ready_head = state_->ready_tail = nullptr;
  state_->managed_by_id.clear();
  state_->native_ownership.clear();
  view_cache_.clear();
  descriptor_to_resource_.clear();
  resources_.clear();
  on_resource_unregistered_ = {};
}

auto ResourceRegistry::UnRegisterResource(const NativeResource& resource)
  -> void
{
  std::scoped_lock lock(registry_mutex_);
  const auto it = resources_.find(resource);
  if (it == resources_.end()) {
    DLOG_F(3,
      "UnRegisterResource: resource {} not found (already unregistered)",
      resource);
    return;
  }
  RequireManualNoLock(resource);
  DLOG_F(
    2, "UnRegisterResource: removing resource {} and all its views", resource);
  UnRegisterResourceViewsNoLock(resource);
  if (CanForgetNativeNoLock(it->second)) {
    NotifyResourceForgottenNoLock(it->second.native_resource);
  }
  RemoveNativeOwnershipNoLock(it->second);
  resources_.erase(it);
  DLOG_F(3, "UnRegisterResource: resource {} removed", resource);
}

auto ResourceRegistry::UnRegisterResourceViews(const NativeResource& resource)
  -> void
{
  std::scoped_lock lock(registry_mutex_);

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
  RequireManualNoLock(resource);

  auto& descriptors = it->second.descriptors;
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
  std::erase_if(view_cache_, [&resource](const auto& cache_entry) -> auto {
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
  std::erase_if(view_cache_, [&resource](const auto& cache_entry) -> auto {
    return cache_entry.first.resource == resource;
  });
}

auto ResourceRegistry::NotifyResourceForgottenNoLock(
  const NativeResource& resource) -> void
{
  if (resource->IsValid() && on_resource_unregistered_) {
    on_resource_unregistered_(resource);
  }
}

auto ResourceRegistry::AttachDescriptorWithView(
  const NativeResource& dst_resource, const bindless::HeapIndex index,
  DescriptorAllocationHandle descriptor_handle, const NativeView& view,
  std::any description, const std::size_t key_hash, ViewQuery query) -> void
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
    .view_description = std::move(description),
    .descriptor_index = index };
  const CacheKey new_cache_key { .resource = dst_resource,
    .view_desc_hash = key_hash };
  StoreViewNoLock(new_cache_key, std::move(cache_entry), query);
}

auto ResourceRegistry::FindViewNoLock(const NativeResource& resource,
  std::size_t key_hash, ViewQuery query) const -> const ViewCacheEntry*
{
  const auto [first, last] = view_cache_.equal_range(
    CacheKey { .resource = resource, .view_desc_hash = key_hash });
  for (auto it = first; it != last; ++it) {
    if (query.matches(it->second.view_description, query.description)) {
      return &it->second;
    }
  }
  return nullptr;
}

auto ResourceRegistry::StoreViewNoLock(
  const CacheKey& key, ViewCacheEntry entry, ViewQuery query) -> void
{
  const auto [first, last] = view_cache_.equal_range(key);
  for (auto it = first; it != last; ++it) {
    if (query.matches(it->second.view_description, query.description)) {
      it->second = std::move(entry);
      return;
    }
  }
  view_cache_.emplace(key, std::move(entry));
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
