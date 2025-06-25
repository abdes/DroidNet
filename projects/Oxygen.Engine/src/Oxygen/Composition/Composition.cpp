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
#if !defined(NDEBUG)
    const auto* comp = pool_ptr->GetUntyped(handle);
    DLOG_F(1, "Destroying pooled component(t={}/{}, h={})", comp->GetTypeId(),
      comp->GetTypeNamePretty(), oxygen::to_string_compact(handle));
#endif // NDEBUG
    pool_ptr->Deallocate(handle); // destroys component
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "exception caught while freeing pooled component({}): {}",
      oxygen::to_string_compact(handle), ex.what());
  }
}

auto Composition::PooledEntry::GetComponent() const -> Component*
{
  return pool_ptr ? pool_ptr->GetUntyped(handle) : nullptr;
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

auto Composition::TopologicallySortedPooledEntries() -> std::vector<TypeId>
{
  std::unordered_map<TypeId, size_t> in_degree;
  in_degree.reserve(pooled_components_.size());
  std::unordered_map<TypeId, std::vector<TypeId>> graph;

  // Collect all local entries
  for (const auto& local_comp : local_components_) {
    DCHECK_NOTNULL_F(local_comp, "corrupted entry, component is null");
    auto type_id = local_comp->GetTypeId();
    if (local_comp->HasDependencies()) {
      auto dependencies = local_comp->Dependencies();
      for (const auto dep : dependencies) {
        graph[type_id].push_back(dep);
        in_degree[dep]++;
      }
    }
    // Ensure all nodes are in the in-degree map
    in_degree.try_emplace(type_id, 0U);
  }

  // Collect all pooled entries
  for (const auto& [type_id, entry] : pooled_components_) {
    const auto* comp = entry->pool_ptr->GetUntyped(entry->handle);
    DCHECK_NOTNULL_F(comp, "corrupted entry, component is null");
    if (comp->HasDependencies()) {
      auto dependencies = comp->Dependencies();
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

  std::vector<TypeId> sorted;
  while (!q.empty()) {
    auto type_id = q.front();
    q.pop();
    sorted.push_back(type_id);

    for (auto neighbor : graph[type_id]) {
      if (--in_degree[neighbor] == 0) {
        q.push(neighbor);
      }
    }
  }

  DCHECK_EQ_F(
    sorted.size(), local_components_.size() + pooled_components_.size());

  return sorted;
}

auto Composition::DestroyComponents() noexcept -> void
{
  std::unique_lock lock(mutex_);

  // Destroy pooled components, dependencies first, using a topological sort
  // that merges both local and pooled components
  const auto sorted_entries = TopologicallySortedPooledEntries();
  for (auto type_id : sorted_entries) {
    // Try to find in pooled components first
    const auto pooled_it = pooled_components_.find(type_id);
    if (pooled_it != pooled_components_.end()) {
      DCHECK_NOTNULL_F(pooled_it->second, "corrupted pooled entry, null");
      pooled_components_.erase(pooled_it);
      continue;
    }
    // Fallback: non-pooled
    const auto local_it = std::ranges::find_if(local_components_,
      [type_id](const auto& comp) { return comp->GetTypeId() == type_id; });
    DCHECK_NE_F(local_it, local_components_.end());
    if (local_it->use_count() == 1) {
      DLOG_F(1, "Destroying local component(t={}/{})", (*local_it)->GetTypeId(),
        (*local_it)->GetTypeNamePretty());
    }
    local_components_.erase(local_it);
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

Composition::Composition(
  const std::size_t local_capacity, const size_t pooled_capacity)
{
  local_components_.reserve(local_capacity);
  pooled_components_.reserve(pooled_capacity);
}

namespace {
auto ValidateDependencies(
  const TypeId comp_id, const std::span<const TypeId> dependencies) -> void
{
  DCHECK_F(!dependencies.empty(), "Dependencies must not be empty");

  using oxygen::ComponentError;
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
} // namespace

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
      [this](const TypeId id) -> Component& { return GetComponentImpl(id); });
  }
}
auto Composition::GetComponentImpl(const TypeId type_id) const
  -> const Component&
{
  const auto pooled_it = pooled_components_.find(type_id);
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
  const auto it = std::ranges::find_if(local_components_,
    [type_id](const auto& comp) { return comp->GetTypeId() == type_id; });
  if (it == local_components_.end()) {
    throw ComponentError("Missing dependency component");
  }
  return **it;
}

auto Composition::GetComponentImpl(const TypeId type_id) -> Component&
{
  return const_cast<Component&>(std::as_const(*this).GetComponentImpl(type_id));
}

auto Composition::GetPooledComponentImpl(
  const composition::detail::ComponentPoolUntyped& pool,
  const TypeId type_id) const -> const Component&
{
  const auto it = pooled_components_.find(type_id);
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
  const composition::detail::ComponentPoolUntyped& pool, const TypeId type_id)
  -> Component&
{
  return const_cast<Component&>(
    std::as_const(*this).GetPooledComponentImpl(pool, type_id));
}

// TODO: must deep copy local and pooled components
auto Composition::DeepCopyComponentsFrom(const Composition& other) -> void
{
  std::unique_lock lock(mutex_);

  DeepCopyLocalComponentsFrom(other);
  DeepCopyPooledComponentsFrom(other);

  // Update dependencies AFTER all components are added to prevent invalidation
  for (const auto& comp : local_components_) {
    if (comp->HasDependencies()) {
      comp->UpdateDependencies(
        [this](const TypeId id) -> Component& { return GetComponentImpl(id); });
    }
  }
  for (const auto& entry : pooled_components_ | std::views::values) {
    auto* comp = entry->GetComponent();
    if (comp->HasDependencies()) {
      comp->UpdateDependencies(
        [this](const TypeId id) -> Component& { return GetComponentImpl(id); });
    }
  }
}
auto Composition::DeepCopyLocalComponentsFrom(const Composition& other) -> void
{
  local_components_.clear();
  // Resize to fit the actual number of components being copied
  const auto component_count = other.local_components_.size();
  local_components_.reserve(component_count);
  for (const auto& entry : other.local_components_) {
    if (const Component* comp = entry.get(); comp->IsCloneable()) {
      // Clone the component
      std::shared_ptr clone = comp->Clone();
      if (!clone) {
        throw ComponentError("Failed to clone pooled component");
      }
      local_components_.emplace_back(clone);
    } else {
      throw ComponentError("Component must be cloneable");
    }
  }
}
auto Composition::DeepCopyPooledComponentsFrom(const Composition& other) -> void
{
  pooled_components_.clear();
  pooled_components_.reserve(other.pooled_components_.size());

  for (const auto& [type_id, entry] : other.pooled_components_) {
    composition::detail::ComponentPoolUntyped* pool = entry->pool_ptr;
    ResourceHandle src_handle = entry->handle;
    if (!pool || !src_handle.IsValid()) {
      throw ComponentError("Invalid pooled entry in source composition");
    }

    // Get the source component
    const Component* src_comp = pool->GetUntyped(src_handle);
    DCHECK_NOTNULL_F(src_comp); // valid handle -> valid component
    if (!src_comp->IsCloneable()) {
      throw ComponentError("Pooled component must be cloneable");
    }

    // Clone the component
    std::unique_ptr clone = src_comp->Clone();
    if (!clone) {
      throw ComponentError("Failed to clone pooled component");
    }

    // Allocate a new instance in the same pool using the type-erased Allocate
    ResourceHandle new_handle = pool->Allocate(std::move(*clone));
    if (!new_handle.IsValid()) {
      throw ComponentError("Failed to allocate pooled component clone");
    }

    pooled_components_[type_id]
      = std::make_shared<PooledEntry>(new_handle, pool);
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

// Helper to print a component's one-line info, and dependencies if present
auto Composition::PrintComponentInfo(std::ostream& out, const TypeId type_id,
  const std::string_view type_name, const std::string_view kind,
  const Component* comp) const -> void
{
  if (!comp) {
    out << "   [" << type_id << "] " << type_name << " (" << kind
        << ") [INVALID]\n";
    return;
  }

  out << "   [" << type_id << "] " << type_name << " (" << kind << ")";
  if (comp->HasDependencies() && !comp->Dependencies().empty()) {
    out << " << Requires: ";
    const auto& type_registry = TypeRegistry::Get();
    for (size_t i = 0; i < comp->Dependencies().size(); ++i) {
      const auto dep_type_id = comp->Dependencies()[i];
      std::string_view dep_name;
      try {
        dep_name = GetComponentImpl(dep_type_id).GetTypeNamePretty();
      } catch (...) {
        try {
          dep_name = type_registry.GetTypeNamePretty(dep_type_id);
        } catch (...) {
          dep_name = "<missing>";
        }
      }
      out << dep_name;
      if (i < comp->Dependencies().size() - 1) {
        out << ", ";
      }
    }
  }
  out << "\n";
}

auto Composition::GetDebugName() const -> std::string
{
  std::string object_name = "Unknown";
  if (HasComponent<ObjectMetaData>()) {
    object_name = static_cast<const ObjectMetaData&>(
      GetComponentImpl(ObjectMetaData::ClassTypeId()))
                    .GetName();
  } else {
    object_name = GetTypeNamePretty();
  }
  return object_name;
}
auto Composition::LogComponentInfo(const TypeId type_id,
  const std::string_view type_name, const std::string_view kind,
  const Component* comp) const -> void
{
  if (!comp) {
    LOG_F(INFO, "[{}] {} ({}) [INVALID]", type_id, type_name, kind);
    return;
  }

  LOG_F(INFO, "[{}] {} ({})", type_id, type_name, kind);
  if (comp->HasDependencies() && !comp->Dependencies().empty()) {
    LOG_SCOPE_F(INFO, "Requires");
    const auto& type_registry = TypeRegistry::Get();
    for (size_t i = 0; i < comp->Dependencies().size(); ++i) {
      const auto dep_type_id = comp->Dependencies()[i];
      std::string_view dep_name;
      try {
        dep_name = GetComponentImpl(dep_type_id).GetTypeNamePretty();
      } catch (...) {
        try {
          dep_name = type_registry.GetTypeNamePretty(dep_type_id);
        } catch (...) {
          dep_name = "<missing>";
        }
      }
      LOG_F(INFO, "{}", dep_name);
    }
  }
}

auto Composition::PrintComponents(std::ostream& out) const -> void
{
  std::shared_lock lock(mutex_);

  const std::size_t total_count
    = local_components_.size() + pooled_components_.size();
  out << "> Object \"" << GetDebugName() << "\" has " << total_count
      << " components:\n";

  // Print direct (non-pooled) components
  for (const auto& entry : local_components_) {
    PrintComponentInfo(out, entry->GetTypeId(), entry->GetTypeNamePretty(),
      "Direct", entry.get());
  }

  // Print pooled components
  for (const auto& [type_id, pooled_entry] : pooled_components_) {
    std::string_view type_name;
    try {
      type_name = TypeRegistry::Get().GetTypeNamePretty(type_id);
    } catch (...) {
      type_name = "<unknown>";
    }
    const Component* comp = nullptr;
    if (pooled_entry && pooled_entry->pool_ptr
      && pooled_entry->handle.IsValid()) {
      comp = pooled_entry->GetComponent();
    }
    PrintComponentInfo(out, type_id, type_name, "Pooled", comp);
  }
  out << "\n";
}
auto Composition::LogComponents() const -> void
{
  LOG_SCOPE_F(INFO, "Composition");
  std::shared_lock lock(mutex_);
  LOG_F(INFO, "name: {}", GetDebugName());

  {
    LOG_SCOPE_F(INFO, "Local Components");
    LOG_F(INFO, "count: {}", local_components_.size());
    for (const auto& entry : local_components_) {
      LogComponentInfo(
        entry->GetTypeId(), entry->GetTypeNamePretty(), "Direct", entry.get());
    }
  }

  {
    LOG_SCOPE_F(INFO, "Pooled Components");
    LOG_F(INFO, "count: {}", pooled_components_.size());
    for (const auto& [type_id, pooled_entry] : pooled_components_) {
      std::string_view type_name;
      try {
        type_name = TypeRegistry::Get().GetTypeNamePretty(type_id);
      } catch (...) {
        type_name = "<unknown>";
      }
      const Component* comp = nullptr;
      if (pooled_entry && pooled_entry->pool_ptr
        && pooled_entry->handle.IsValid()) {
        comp = pooled_entry->GetComponent();
      }
      LogComponentInfo(type_id, type_name, "Pooled", comp);
    }
  }
}
