//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>
#include <queue>
#include <ranges>
#include <utility> // std::as_const

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Composition/TypeSystem.h>

using oxygen::Component;
using oxygen::Composition;
using oxygen::TypeId;

Composition::PooledEntry::~PooledEntry() noexcept
{
  if (pool_ptr == nullptr || !handle.IsValid()) {
    return; // Nothing to do
  }

  try {
    const auto* comp = pool_ptr->GetUntyped(handle);
    DLOG_F(1, "Destroying pooled component(t={}/{}, h={})", comp->GetTypeId(),
      comp->GetTypeNamePretty(), oxygen::to_string_compact(handle));
    pool_ptr->Deallocate(handle); // destroys component
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "exception caught while freeing pooled component({}): {}",
      oxygen::to_string_compact(handle), ex.what());
  }
}

Composition::~Composition() noexcept
{
  LOG_SCOPE_FUNCTION(3);
  DestroyComponents();
}

auto Composition::HasComponents() const noexcept -> bool
{
  std::shared_lock lock(mutex_);
  return !local_components_.empty();
}
auto Composition::TopologicallySortedPooledEntries()
  -> std::vector<std::shared_ptr<PooledEntry>>
{
  std::unordered_map<TypeId, size_t> in_degree;
  in_degree.reserve(pooled_components_.size());
  std::unordered_map<TypeId, std::vector<TypeId>> graph;

  // Collect all pooled entries
  for (const auto& [type_id, entry] : pooled_components_) {
    const auto& comp = entry->pool_ptr->GetUntyped(entry->handle);
    DCHECK_NOTNULL_F(comp, "corrupted entry, component is null");
    if (comp->HasDependencies()) {
      auto dependencies
        = entry->pool_ptr->GetUntyped(entry->handle)->Dependencies();
      for (const auto dep : dependencies) {
        graph[type_id].push_back(dep);
        in_degree[dep]++;
      }
    }
    // Ensure all nodes are in the in-degree map
    in_degree.try_emplace(type_id, 0U);
  }

  // Use Kahn's algorithm to sort by in-degree
  std::queue<TypeId> q;
  for (const auto& [type_id, deg] : in_degree) {
    if (deg == 0) {
      q.push(type_id);
    }
  }

  std::vector<std::shared_ptr<PooledEntry>> sorted;
  while (!q.empty()) {
    auto type_id = q.front();
    q.pop();
    sorted.push_back(pooled_components_[type_id]);

    for (auto neighbor : graph[type_id]) {
      if (--in_degree[neighbor] == 0) {
        q.push(neighbor);
      }
    }
  }

  DCHECK_EQ_F(sorted.size(), pooled_components_.size());

  return sorted;
}

auto Composition::DestroyComponents() noexcept -> void
{
  std::unique_lock lock(mutex_);
  // Clear in reverse order - dependents before dependencies
  while (!local_components_.empty()) {
    // Absorb all exceptions
    try {
      const auto& comp = local_components_.back();
      if (comp.use_count() == 1) {
        DLOG_F(1, "Destroying local component(t={}/{})", comp->GetTypeId(),
          comp->GetTypeNamePretty());
      }
      local_components_.pop_back();
    } catch (const std::exception& e) {
      LOG_F(
        ERROR, "Exception caught while destructing components: %s", e.what());
    }
  }

  if (pooled_components_.empty()) {
    return; // Nothing to destroy
  }

  // Destroy pooled components, dependencies first, using a topological sort
  auto sorted_entries = TopologicallySortedPooledEntries();
  for (auto& entry : sorted_entries) {
    DCHECK_NOTNULL_F(entry, "corrupted sorted entry, pointer is null");
    const auto* comp = entry->pool_ptr->GetUntyped(entry->handle);
    DCHECK_NOTNULL_F(comp, "corrupted entry, component is null");
    auto type_id = comp->GetTypeId();
    pooled_components_.erase(type_id);
  }
}

Composition::Composition(const Composition& other)
  : local_components_(other.local_components_)
  , pooled_components_(other.pooled_components_)
{
  // Shallow copy: share the same component instances
}

auto Composition::operator=(const Composition& other) -> Composition&
{
  if (this != &other) {
    // Shallow copy: share the same component instances
    local_components_ = other.local_components_;
  }
  return *this;
}

Composition::Composition(Composition&& other) noexcept
  : local_components_(std::move(other.local_components_))
{
}

auto Composition::operator=(Composition&& other) noexcept -> Composition&
{
  if (this != &other) {
    DestroyComponents();
    local_components_ = std::move(other.local_components_);
  }
  return *this;
}

Composition::Composition(std::size_t initial_capacity)
{
  // Reserve capacity to prevent reallocations that would invalidate pointers
  // stored during UpdateDependencies calls
  local_components_.reserve(initial_capacity);
}

auto Composition::ValidateDependencies(
  const TypeId comp_id, const std::span<const TypeId> dependencies) -> void
{
  DCHECK_F(!dependencies.empty(), "Dependencies must not be empty");

  for (size_t i = 0; i < dependencies.size(); ++i) {
    const auto dep_id = dependencies[i];

    // Check for self-dependency
    if (dep_id == comp_id) {
      throw ComponentError("Component cannot depend on itself");
    }

    // Check for duplicates by comparing with previous elements
    for (size_t j = 0; j < i; ++j) {
      if (dependencies[j] == dep_id) {
        throw ComponentError("Duplicate dependency detected");
      }
    }
  }
}

auto Composition::EnsureDependencies(const TypeId comp_id,
  const std::span<const TypeId> dependencies) const -> void
{
  ValidateDependencies(comp_id, dependencies);

  for (TypeId dep : dependencies) {
    bool found = false;
    if (pooled_components_.contains(dep)) {
      found = true;
    } else {
      for (const auto& c : local_components_) {
        if (c && c->GetTypeId() == dep) {
          found = true;
          break;
        }
      }
    }
    if (!found) {
      throw ComponentError("Missing dependency component");
    }
  }
}

auto Composition::HasLocalComponentImpl(const TypeId id) const -> bool
{
  return std::ranges::any_of(local_components_,
    [id](const auto& comp) { return comp->GetTypeId() == id; });
}

auto Composition::HasPooledComponentImpl(const TypeId id) const -> bool
{
  return pooled_components_.contains(id);
}

auto Composition::UpdateComponentDependencies(Component& component) noexcept
  -> void
{
  if (component.HasDependencies()) {
    component.UpdateDependencies(
      [this](TypeId id) -> Component& { return GetComponentImpl(id); });
  }
}

auto Composition::ReplaceComponentImpl(
  const TypeId old_id, std::shared_ptr<Component> new_component) -> Component&
{
  DCHECK_NOTNULL_F(new_component, "Component must not be null");
  auto it = std::ranges::find_if(local_components_,
    [old_id](const auto& comp) { return comp->GetTypeId() == old_id; });
  DCHECK_F(it != local_components_.end(), "Old component must exist");

  *it = std::move(new_component);
  return **it;
}

auto Composition::GetComponentImpl(const TypeId id) const -> const Component&
{
  auto pooled_it = pooled_components_.find(id);
  if (pooled_it != pooled_components_.end()) {
    auto* pool = pooled_it->second->pool_ptr;
    if (!pool)
      throw ComponentError("Pooled component pool pointer is null");
    auto* ptr = pool->GetUntyped(pooled_it->second->handle);
    if (!ptr)
      throw ComponentError("Pooled component handle invalid");
    return *ptr;
  }
  // Fallback: non-pooled
  auto it = std::ranges::find_if(local_components_,
    [id](const auto& comp) { return comp->GetTypeId() == id; });
  if (it == local_components_.end()) {
    throw ComponentError("Missing dependency component");
  }
  return **it;
}

auto Composition::GetComponentImpl(const TypeId type_id) -> Component&
{
  return const_cast<Component&>(std::as_const(*this).GetComponentImpl(type_id));
}

auto Composition::GetPooledComponentImpl(const IComponentPoolUntyped& pool,
  const TypeId type_id) const -> const Component&
{
  auto it = pooled_components_.find(type_id);
  if (it == pooled_components_.end()) {
    throw ComponentError("Component not found");
  }
  // If an entry in the composition pooled components map exists, it is
  // guaranteed to have a valid handle and a corresponding component in the
  // pool.
  auto* ptr = pool.GetUntyped(it->second->handle);
  DCHECK_NOTNULL_F(ptr, "unexpected invalid pooled component");
  return *ptr;
}

auto Composition::GetPooledComponentImpl(
  const IComponentPoolUntyped& pool, const TypeId id) -> Component&
{
  return const_cast<Component&>(
    std::as_const(*this).GetPooledComponentImpl(pool, id));
}

auto Composition::DeepCopyComponentsFrom(const Composition& other) -> void
{
  std::unique_lock lock(mutex_);

  // Resize to fit the actual number of components being copied
  const auto component_count = other.local_components_.size();
  local_components_.clear();
  local_components_.reserve(component_count);
  for (const auto& entry : other.local_components_) {
    if (const Component* comp = entry.get(); comp->IsCloneable()) {
      local_components_.emplace_back(std::shared_ptr<Component>(comp->Clone()));
    } else {
      throw ComponentError("Component must be cloneable");
    }
  }

  // Update dependencies AFTER all components are added to prevent invalidation
  for (const auto& comp : local_components_) {
    if (comp->HasDependencies()) {
      comp->UpdateDependencies([this](TypeId id) -> Component& {
        // Nollocking needed here
        return GetComponentImpl(id);
      });
    }
  }
}

namespace {

auto EnsureTypeIsNoInDependenciesOf(const Component& comp, const TypeId type_id)
  -> void
{
  using oxygen::ComponentError;
  using oxygen::TypeRegistry;

  const auto& dependencies = comp.Dependencies();
  if (std::ranges::find(dependencies, type_id) != dependencies.end()) {
    const auto& tr = TypeRegistry::Get();
    throw ComponentError(fmt::format("component({}/{}) is required by other "
                                     "components, including at least ({}/{})",
      type_id, tr.GetTypeNamePretty(type_id), comp.GetTypeId(),
      comp.GetTypeNamePretty()));
  }
}

} // namespace

auto Composition::EnsureNotRequired(const TypeId type_id) const -> void
{
  // Check non-pooled components
  for (const auto& comp : local_components_) {
    EnsureTypeIsNoInDependenciesOf(*comp, type_id);
  }

  // Check pooled components
  for (const auto& entry : pooled_components_ | std::views::values) {
    const auto& [handle, pool_ptr] = *entry;
    DCHECK_F(handle.IsValid(), "pooled entry with invalid handle");
    DCHECK_NOTNULL_F(pool_ptr, "pooled entry with no pool");
    const auto* comp = pool_ptr->GetUntyped(handle);
    DCHECK_NOTNULL_F(comp, "pooled entry with no component");
    EnsureTypeIsNoInDependenciesOf(*comp, type_id);
  }
}

auto Composition::PrintComponents(std::ostream& out) const -> void
{
  std::shared_lock lock(mutex_);

  std::string object_name = "Unknown";
  if (HasComponent<ObjectMetaData>()) {
    object_name = static_cast<const ObjectMetaData&>(
      GetComponentImpl(ObjectMetaData::ClassTypeId()))
                    .GetName();
  }

  std::size_t total_count
    = local_components_.size() + pooled_components_.size();
  out << "> Object \"" << object_name << "\" has " << total_count
      << " components:\n";

  // Print direct (non-pooled) components
  for (const auto& entry : local_components_) {
    out << "   [" << entry->GetTypeId() << "] " << entry->GetTypeNamePretty()
        << " (Direct)";
    if (!entry->Dependencies().empty()) {
      out << " << Requires: ";
      for (size_t i = 0; i < entry->Dependencies().size(); ++i) {
        const auto& dep_component = GetComponentImpl(entry->Dependencies()[i]);
        out << dep_component.GetTypeNamePretty();
        if (i < entry->Dependencies().size() - 1) {
          out << ", ";
        }
      }
    }
    out << "\n";
  }

  // Print pooled components
  const auto& type_registry = TypeRegistry::Get();
  for (const auto& [type_id, handle] : pooled_components_) {
    // Print the class name using TypeRegistry
    std::string_view type_name {};
    try {
      type_name = type_registry.GetTypeNamePretty(type_id);
    } catch (...) {
      type_name = "<unknown>";
    }
    out << "   [" << type_id << "] " << type_name << " (Pooled)";
    out << "\n";
  }
  out << "\n";
}
