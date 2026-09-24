//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#if defined(OXYGEN_WITH_TRACY)
#  include <Oxygen/Profiling/CpuProfileScope.h>
#endif

#include <cassert>
#include <limits>

#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>

namespace oxygen::graphics {
auto ResourceRegistry::InstallManagedContext(std::weak_ptr<Graphics> owner,
  std::shared_ptr<BackendLifetime> lifetime,
  std::shared_ptr<void> native_lifetime) -> void
{
  std::lock_guard lock(registry_mutex_);
  if (closed_ || state_->lifetime) {
    throw std::logic_error(
      "Registry backend context already installed or closed");
  }
  state_->backend_owner = std::move(owner);
  state_->lifetime = std::move(lifetime);
  state_->native_lifetime = std::move(native_lifetime);
}

auto ResourceRegistry::RequireManualNoLock(
  const NativeResource& key, NativeResource native_resource) const -> void
{
  if (closed_) {
    throw std::logic_error("Resource registry is closed");
  }
  const auto found = resources_.find(key);
  if (found != resources_.end() && found->second.managed) {
    throw std::logic_error(
      "Raw mutation of a managed registration is prohibited");
  }
  if (native_resource->IsValid()) {
    const auto native = state_->native_ownership.find(native_resource);
    if (native != state_->native_ownership.end()
      && native->second.managed_id.get() != 0) {
      throw std::logic_error("Native resource already has managed ownership");
    }
  }
}

auto ResourceRegistry::InsertManualNoLock(std::shared_ptr<void> resource,
  TypeId type, NativeResource native_resource) -> void
{
  const NativeResource key { resource.get(), type };
  RequireManualNoLock(key, native_resource);
  bool native_inserted = false;
  if (native_resource->IsValid()) {
    native_inserted
      = state_->native_ownership.try_emplace(native_resource).second;
  }
  try {
    resources_.emplace(key,
      ResourceEntry { .resource = std::move(resource),
        .native_resource = native_resource,
        .descriptors = {} });
  } catch (...) {
    if (native_inserted) {
      state_->native_ownership.erase(native_resource);
    }
    throw;
  }
  if (native_resource->IsValid()) {
    ++state_->native_ownership.at(native_resource).manual_entries;
  }
}

auto ResourceRegistry::RemoveNativeOwnershipNoLock(
  const ResourceEntry& entry) noexcept -> void
{
  if (!entry.native_resource->IsValid()) {
    return;
  }
  const auto found = state_->native_ownership.find(entry.native_resource);
  assert(found != state_->native_ownership.end());
  if (entry.managed) {
    assert(found->second.managed_id == entry.managed->identity.registration);
    state_->native_ownership.erase(found);
  } else {
    assert(found->second.manual_entries != 0);
    if (--found->second.manual_entries == 0) {
      state_->native_ownership.erase(found);
    }
  }
}

auto ResourceRegistry::CanForgetNativeNoLock(
  const ResourceEntry& entry) const noexcept -> bool
{
  if (!entry.native_resource->IsValid()) {
    return false;
  }
  const auto found = state_->native_ownership.find(entry.native_resource);
  return found != state_->native_ownership.end()
    && found->second.manual_entries <= 1;
}

auto ResourceRegistry::MakeManagedLeaseNoLock(
  const std::shared_ptr<detail::RegistrationCore>& core,
  std::shared_ptr<Graphics> backend) -> RegistrationLease
{
  if (core->allocation_owners == (std::numeric_limits<uint64_t>::max)()) {
    throw std::bad_alloc();
  }
  auto owner = std::make_shared<detail::RegistrationAllocationOwner>(core);
  ++core->allocation_owners;
  owner->armed = true;
  return RegistrationLease(
    std::move(backend), RegistrationOwner(std::move(owner)));
}

auto ResourceRegistry::RegisterManaged(
  std::shared_ptr<void> resource, TypeId type, NativeResource native_resource)
  -> std::expected<RegistrationLease, RegistrationError>
{
  if (!state_->lifetime) {
    return std::unexpected(RegistrationError::kClosed);
  }
  try {
    const auto admission = state_->lifetime->AcquireOperation();
    auto backend = state_->backend_owner.lock();
    if (!backend) {
      return std::unexpected(RegistrationError::kClosed);
    }
    std::lock_guard lock(registry_mutex_);
    if (closed_) {
      return std::unexpected(RegistrationError::kClosed);
    }
    const NativeResource key { resource.get(), type };
    if (const auto found = resources_.find(key); found != resources_.end()) {
      if (!found->second.managed) {
        return std::unexpected(RegistrationError::kOwnershipConflict);
      }
      if (!found->second.managed->open) {
        return std::unexpected(RegistrationError::kClosed);
      }
      return MakeManagedLeaseNoLock(found->second.managed, std::move(backend));
    }
    if (state_->next_registration == (std::numeric_limits<uint64_t>::max)()) {
      return std::unexpected(RegistrationError::kAllocationFailed);
    }
    if (native_resource->IsValid()
      && state_->native_ownership.contains(native_resource)) {
      return std::unexpected(RegistrationError::kOwnershipConflict);
    }
    const auto id = state_->next_registration++;
    auto core = std::make_shared<detail::RegistrationCore>();
    core->state = state_;
    core->native_lifetime = state_->native_lifetime;
    core->resource = resource;
    core->identity = { state_->lifetime->Id(), RegistrationId { id } };
    core->key = key;
    core->native_resource = native_resource;
    bool native_inserted = false;
    try {
      if (native_resource->IsValid()) {
        native_inserted = state_->native_ownership
                            .emplace(native_resource,
                              detail::ResourceRegistryState::NativeOwnership {
                                0, RegistrationId { id } })
                            .second;
      }
      resources_.emplace(key,
        ResourceEntry { .resource = std::move(resource),
          .native_resource = native_resource,
          .descriptors = {},
          .managed = core });
      state_->managed_by_id.emplace(id, core);
      auto lease = MakeManagedLeaseNoLock(core, std::move(backend));
      core->published = true;
      return lease;
    } catch (...) {
      state_->managed_by_id.erase(id);
      resources_.erase(key);
      if (native_inserted) {
        state_->native_ownership.erase(native_resource);
      }
      throw;
    }
  } catch (const std::logic_error&) {
    return std::unexpected(RegistrationError::kClosed);
  } catch (...) {
    return std::unexpected(RegistrationError::kAllocationFailed);
  }
}

auto ResourceRegistry::AcquireManaged(RegistrationIdentity identity)
  -> std::expected<RegistrationLease, RegistrationError>
{
  if (!state_->lifetime) {
    return std::unexpected(RegistrationError::kClosed);
  }
  try {
    const auto admission = state_->lifetime->AcquireOperation();
    auto backend = state_->backend_owner.lock();
    if (!backend) {
      return std::unexpected(RegistrationError::kClosed);
    }
    std::lock_guard lock(registry_mutex_);
    if (closed_) {
      return std::unexpected(RegistrationError::kClosed);
    }
    if (identity.backend != state_->lifetime->Id()) {
      return std::unexpected(RegistrationError::kWrongBackend);
    }
    const auto found = state_->managed_by_id.find(identity.registration.get());
    const auto core
      = found == state_->managed_by_id.end() ? nullptr : found->second.lock();
    if (!core) {
      return std::unexpected(RegistrationError::kStaleRegistration);
    }
    if (!core->open) {
      return std::unexpected(RegistrationError::kClosed);
    }
    return MakeManagedLeaseNoLock(core, std::move(backend));
  } catch (const std::logic_error&) {
    return std::unexpected(RegistrationError::kClosed);
  } catch (...) {
    return std::unexpected(RegistrationError::kAllocationFailed);
  }
}

auto ResourceRegistry::GetManagedCoreNoLock(
  const RegistrationOwner& owner) const
  -> std::expected<std::shared_ptr<detail::RegistrationCore>, RegistrationError>
{
  if (closed_ || !state_->lifetime) {
    return std::unexpected(RegistrationError::kClosed);
  }
  if (!owner.owner_) {
    return std::unexpected(RegistrationError::kStaleRegistration);
  }
  const auto& core = owner.owner_->core;
  if (core->identity.backend != state_->lifetime->Id()) {
    return std::unexpected(RegistrationError::kWrongBackend);
  }
  if (core->state.lock() != state_ || !core->published) {
    return std::unexpected(RegistrationError::kStaleRegistration);
  }
  if (!core->open) {
    return std::unexpected(RegistrationError::kClosed);
  }
  return core;
}

auto ResourceRegistry::RetainUse(const RegistrationOwner& owner)
  -> std::expected<RegistrationUse, RegistrationError>
{
  if (!state_->lifetime) {
    return std::unexpected(RegistrationError::kClosed);
  }
  try {
    const auto admission = state_->lifetime->AcquireOperation();
    std::unique_lock lock(registry_mutex_, std::defer_lock);
#if defined(OXYGEN_WITH_TRACY)
    {
      static const profiling::CpuProfileScopeDesc kWait { .label
        = "Graphics.Registry.UsePin.LockWait",
        .category = profiling::ProfileCategory::kSynchronization };
      const profiling::CpuProfileScope wait(kWait);
      lock.lock();
    }
    static const profiling::CpuProfileScopeDesc kHeld { .label
      = "Graphics.Registry.UsePin.CriticalSection",
      .category = profiling::ProfileCategory::kSynchronization };
    const profiling::CpuProfileScope held(kHeld);
#else
    lock.lock();
#endif
    auto core = GetManagedCoreNoLock(owner);
    if (!core) {
      return std::unexpected(core.error());
    }
    if ((*core)->uses == (std::numeric_limits<uint64_t>::max)()) {
      return std::unexpected(RegistrationError::kAllocationFailed);
    }
    ++(*core)->uses;
    return RegistrationUse(std::move(*core));
  } catch (...) {
    return std::unexpected(RegistrationError::kClosed);
  }
}

auto ResourceRegistry::AcquireManagedView(const RegistrationOwner& owner,
  TypeId type, std::size_t hash, ResourceViewType view_type,
  DescriptorVisibility visibility, ViewQuery query, CreateManagedView create,
  CopyViewDescription copy) -> std::expected<ManagedView, RegistrationError>
{
  if (!state_->lifetime) {
    return std::unexpected(RegistrationError::kClosed);
  }
  try {
    const auto admission = state_->lifetime->AcquireOperation();
    auto backend = state_->backend_owner.lock();
    if (!backend) {
      return std::unexpected(RegistrationError::kClosed);
    }
    std::lock_guard lock(registry_mutex_);
    auto checked = GetManagedCoreNoLock(owner);
    if (!checked) {
      return std::unexpected(checked.error());
    }
    const auto& core = *checked;
    if (core->key->OwnerTypeId() != type) {
      return std::unexpected(RegistrationError::kStaleRegistration);
    }
    auto& allocator = backend->GetDescriptorAllocator();
    if (const auto* cached = FindViewNoLock(core->key, hash, query)) {
      const auto descriptor = core->descriptors.find(cached->descriptor_index);
      assert(descriptor != core->descriptors.end());
      const auto index = visibility == DescriptorVisibility::kShaderVisible
        ? allocator.GetShaderVisibleIndex(descriptor->second.descriptor)
        : kInvalidShaderVisibleIndex;
      return ManagedView { cached->view_object, index };
    }
    // Preparation is transactional. The existing shadow path uses raw SRV/DSV
    // allocations in the fixed heaps; sharing preserves that binding contract.
    auto description = copy(query.description);
    auto descriptor = allocator.AllocateRaw(view_type, visibility);
    if (!descriptor.IsValid()) {
      return std::unexpected(RegistrationError::kAllocationFailed);
    }
    const auto view
      = create(core->resource.get(), descriptor, query.description);
    if (!view->IsValid()) {
      return std::unexpected(RegistrationError::kAllocationFailed);
    }
    const auto shader_index = visibility == DescriptorVisibility::kShaderVisible
      ? allocator.GetShaderVisibleIndex(descriptor)
      : kInvalidShaderVisibleIndex;
    const auto index = descriptor.GetBindlessHandle();
    const auto [inserted, unique] = core->descriptors.emplace(
      index, ResourceEntry::ViewEntry { view, std::move(descriptor) });
    assert(unique);
    bool mapped = false;
    try {
      mapped = descriptor_to_resource_.emplace(index, core->key).second;
      if (!mapped) {
        throw std::logic_error("Descriptor identity already registered");
      }
      view_cache_.emplace(CacheKey { core->key, hash },
        ViewCacheEntry { view, std::move(description), index });
    } catch (...) {
      if (mapped) {
        descriptor_to_resource_.erase(index);
      }
      core->descriptors.erase(inserted);
      throw;
    }
    return ManagedView { view, shader_index };
  } catch (...) {
    return std::unexpected(
      state_->lifetime->State() == BackendLifecycle::kActive
        ? RegistrationError::kAllocationFailed
        : RegistrationError::kClosed);
  }
}

auto ResourceRegistry::InspectManagedIdentity(
  const NativeResource& resource) const -> std::optional<RegistrationIdentity>
{
  std::lock_guard lock(registry_mutex_);
  const auto found = resources_.find(resource);
  if (found == resources_.end() || !found->second.managed) {
    return {};
  }
  return found->second.managed->identity;
}

auto ResourceRegistry::HasManagedRegistrations() const noexcept -> bool
{
  std::lock_guard lock(registry_mutex_);
  return !state_->managed_by_id.empty();
}

auto ResourceRegistry::PollManagedRetirements() noexcept -> void
{
  std::lock_guard lock(registry_mutex_);
  while (auto* ready = state_->ready_head) {
    state_->ready_head = ready->ready_next;
    if (!state_->ready_head) {
      state_->ready_tail = nullptr;
    }
    ready->queued = false;
    ready->ready_next = nullptr;
    const auto found = resources_.find(ready->key);
    if (found == resources_.end() || found->second.managed.get() != ready) {
      continue;
    }
    const auto core = found->second.managed;
    assert(core->allocation_owners == 0 && core->uses == 0);
    core->published = false;
    try {
      NotifyResourceForgottenNoLock(core->native_resource);
    } catch (...) {
      LOG_F(ERROR, "Resource-state retirement callback threw");
    }
    for (const auto& [index, view] : core->descriptors) {
      descriptor_to_resource_.erase(index);
    }
    PurgeCachedViewsForResource(core->key);
    state_->managed_by_id.erase(core->identity.registration.get());
    RemoveNativeOwnershipNoLock(found->second);
    resources_.erase(found);
  }
}

auto ResourceRegistry::InvalidateManagedRegistrations(
  std::span<const RegistrationIdentity> identities) noexcept -> void
{
  std::lock_guard lock(registry_mutex_);
  for (const auto identity : identities) {
    if (!state_->lifetime || identity.backend != state_->lifetime->Id()) {
      continue;
    }
    const auto found = state_->managed_by_id.find(identity.registration.get());
    if (found == state_->managed_by_id.end()) {
      continue;
    }
    if (const auto core = found->second.lock()) {
      core->open = false;
      core->poisoned = true;
    }
  }
}
} // namespace oxygen::graphics
