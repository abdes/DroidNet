//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <any>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

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
    LOG_F(ERROR, "ResourceRegistry cleanup failed during destruction");
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
  RequireManualNoLock({ .object = key, .backing = {} });
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
  RequireManualNoLock({ .object = key, .backing = {} });
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
  DLOG_F(1, "resource: {}", resource);
  DLOG_F(1, "view: {}", view);
  DLOG_F(1, "view handle: {}", view_handle);
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
  RequireManualNoLock({ .object = resource, .backing = {} });

  // Check view cache first
  if (const auto* cached = FindViewNoLock(resource, key_hash, query);
    cached != nullptr) {
    DLOG_F(2, "cache hit ({})", cached->view_object);
    // This is a programming error, abort.
    ABORT_F("-failed- use UpdateView() to update registered views");
  }

  const auto index = view_handle.GetBindlessHandle();
  auto& descriptors = resource_it->second.descriptors;
  if (const auto previous = descriptors.find(index);
    previous != descriptors.end()) {
    UnlinkViewNoLock(resource, previous->second);
    descriptor_to_resource_.erase(index);
    descriptors.erase(previous);
  }
  AttachDescriptorWithView(resource, index, std::move(view_handle), view,
    std::move(view_description), key_hash, query);

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

  if (const auto* cached = FindViewNoLock(resource, key_hash, query);
    cached != nullptr) {
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

  if (cached->descriptor.IsValid()
    && cached->descriptor.GetAllocator() != nullptr) {
    return cached->descriptor.GetAllocator()->GetShaderVisibleIndex(
      cached->descriptor);
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
  RequireManualNoLock({ .object = resource, .backing = {} });

  auto& descriptors = it->second.descriptors;
  // Remove all descriptors with the matching view_object.
  size_t removed_descriptor_count = 0;
  for (auto desc_it = descriptors.begin(); desc_it != descriptors.end();) {
    if (desc_it->second.view_object != view) {
      ++desc_it;
      continue;
    }

    DLOG_F(4, "release view descriptor handle ({})", desc_it->first);
    UnlinkViewNoLock(resource, desc_it->second);
    descriptor_to_resource_.erase(desc_it->first);
    desc_it->second.descriptor.Release();
    desc_it = descriptors.erase(desc_it);
    ++removed_descriptor_count;
  }

  if (removed_descriptor_count == 0) {
    DLOG_F(3, "view not found, already unregistered?");
    return; // Nothing to do
  }
}

auto ResourceRegistry::UnRegisterViewBatch(const NativeResource& resource,
  const std::span<const NativeView> views) -> void
{
  std::scoped_lock lock(registry_mutex_);
  RequireManualNoLock({ .object = resource, .backing = {} });
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
    UnlinkViewNoLock(resource, it->second);
    descriptor_to_resource_.erase(it->first);
    it->second.descriptor.Release();
    it = descriptors.erase(it);
  }
}

auto ResourceRegistry::Close() -> void
{
  std::scoped_lock lock(registry_mutex_);
  closed_ = true;
  for (const auto& [native, ownership] : state_->native_ownership) {
    NotifyResourceForgottenNoLock(native);
  }
  for (auto& [resource, entry] : resources_) {
    PurgeCachedViewsForResource(resource);
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
  RequireManualNoLock({ .object = resource, .backing = {} });
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
  DLOG_F(2, "resource {}", resource);
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
  RequireManualNoLock({ .object = resource, .backing = {} });

  auto& descriptors = it->second.descriptors;
  if (descriptors.empty()) {
    DLOG_F(4, "no views to un-register");
    return;
  }

  [[maybe_unused]] const size_t view_count = descriptors.size();
  DLOG_F(2, "{} view{} to un-register", view_count, view_count == 1 ? "" : "s");

  // Release all descriptors and remove from descriptor_to_resource_ map
  for (auto& [index, view_entry] : descriptors) {
    UnlinkViewNoLock(resource, view_entry);
    DLOG_F(3, "view for index {}", view_entry.descriptor.GetBindlessHandle());
    if (view_entry.descriptor.IsValid()) {
      view_entry.descriptor.Release();
      descriptor_to_resource_.erase(index);
    }
  }

  // Clear descriptors map
  descriptors.clear();
}

//=== Internal helpers ----------------------------------------------------//

auto ResourceRegistry::PurgeCachedViewsForResource(
  const NativeResource& resource) -> void
{
  const auto owner = resources_.find(resource);
  if (owner == resources_.end()) {
    return;
  }
  auto& descriptors = owner->second.managed ? owner->second.managed->descriptors
                                            : owner->second.descriptors;
  for (auto& [index, entry] : descriptors) {
    UnlinkViewNoLock(resource, entry);
  }
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
  const auto owner = resources_.find(dst_resource);
  if (owner == resources_.end()) {
    throw std::logic_error("View destination resource is not registered");
  }
  auto& descriptors = owner->second.descriptors;
  const auto [inserted, unique]
    = descriptors.try_emplace(index, view, std::move(descriptor_handle),
      std::move(description), key_hash, query.domain);
  if (!unique) {
    throw std::logic_error("View descriptor is already registered");
  }
  bool mapped = false;
  bool committed = false;
  const ScopeGuard rollback([&]() noexcept {
    if (!committed) {
      if (mapped) {
        descriptor_to_resource_.erase(index);
      }
      descriptors.erase(inserted);
    }
  });
  mapped = descriptor_to_resource_.emplace(index, dst_resource).second;
  if (!mapped) {
    throw std::logic_error("Descriptor identity already registered");
  }
  LinkViewNoLock(dst_resource, inserted->second);
  committed = true;
}

auto ResourceRegistry::FindViewNoLock(const NativeResource& resource,
  const std::size_t key_hash, ViewQuery query) const
  -> const ResourceEntry::ViewEntry*
{
  const auto group = view_cache_.find(
    CacheKey { .resource = resource, .view_desc_hash = key_hash });
  if (group == view_cache_.end()) {
    return nullptr;
  }
  for (const auto* entry = group->second; entry != nullptr;
    entry = entry->cache_next) {
    if (query.domain == entry->domain
      && query.matches(entry->view_description, query.description)) {
      return entry;
    }
  }
  return nullptr;
}

auto ResourceRegistry::PrependViewNoLock(ResourceEntry::ViewEntry& entry,
  ResourceEntry::ViewEntry*& head) noexcept -> void
{
  // A linked entry already belongs to this group; move it to the front.
  if (entry.cached) {
    if (head == &entry) {
      return;
    }
    entry.cache_previous->cache_next = entry.cache_next;
    if (entry.cache_next != nullptr) {
      entry.cache_next->cache_previous = entry.cache_previous;
    }
  }
  entry.cache_previous = nullptr;
  entry.cache_next = head;
  if (head != nullptr) {
    head->cache_previous = &entry;
  }
  head = &entry;
  entry.cached = true;
}

auto ResourceRegistry::LinkViewNoLock(
  const NativeResource& resource, ResourceEntry::ViewEntry& entry) -> void
{
  DCHECK_F(!entry.cached);
  const auto group
    = view_cache_
        .try_emplace(CacheKey { .resource = resource,
                       .view_desc_hash = entry.description_hash },
          nullptr)
        .first;
  PrependViewNoLock(entry, group->second);
}

auto ResourceRegistry::UnlinkViewNoLock(const NativeResource& resource,
  ResourceEntry::ViewEntry& entry) noexcept -> void
{
  if (!entry.cached) {
    return;
  }
  if (entry.cache_previous != nullptr) {
    entry.cache_previous->cache_next = entry.cache_next;
  } else {
    const auto group = view_cache_.find(CacheKey {
      .resource = resource, .view_desc_hash = entry.description_hash });
    DCHECK_F(group != view_cache_.end() && group->second == &entry);
    if (entry.cache_next != nullptr) {
      group->second = entry.cache_next;
    } else {
      view_cache_.erase(group);
    }
  }
  if (entry.cache_next != nullptr) {
    entry.cache_next->cache_previous = entry.cache_previous;
  }
  entry.cache_previous = nullptr;
  entry.cache_next = nullptr;
  entry.cached = false;
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
