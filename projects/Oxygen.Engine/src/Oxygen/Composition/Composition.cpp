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

// --- Utility: DRY for dependency name resolution ---
namespace {
auto TryGetTypeNamePretty(const TypeId type_id) -> std::string_view
{
  try {
    return oxygen::TypeRegistry::Get().GetTypeNamePretty(type_id);
  } catch (...) {
    return "<unknown>";
  }
}
}

// --- PooledEntry ---

Composition::PooledEntry::~PooledEntry() noexcept
{
  if ((pool_ptr == nullptr) || !handle.IsValid()) {
    return;
  }
  try {
#if !defined(NDEBUG)
    if (const auto* comp = pool_ptr->GetUntyped(handle)) {
      DLOG_F(1, "Destroying pooled component(t={}/{}, h={})", comp->GetTypeId(),
        comp->GetTypeNamePretty(), oxygen::to_string_compact(handle).c_str());
    }
#endif
    pool_ptr->Deallocate(handle);
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "exception caught while freeing pooled component({}): {}",
      oxygen::to_string_compact(handle).c_str(), ex.what());
  }
}

auto Composition::PooledEntry::GetComponent() const -> Component*
{
  return (pool_ptr != nullptr) ? pool_ptr->GetUntyped(handle) : nullptr;
}

// --- Composition ---

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

// --- Topological Sort (DRY for both local and pooled) ---
auto Composition::TopologicallySortedPooledEntries() -> std::vector<TypeId>
{
  std::unordered_map<TypeId, size_t> in_degree;
  std::unordered_map<TypeId, std::vector<TypeId>> graph;

  auto add_dependencies = [&](const auto& comp, const TypeId type_id) {
    if (comp && comp->HasDependencies()) {
      for (auto dep : comp->Dependencies()) {
        graph[type_id].push_back(dep);
        ++in_degree[dep];
      }
    }
    in_degree.try_emplace(type_id, 0U);
  };

  for (const auto& local_comp : local_components_) {
    add_dependencies(local_comp, local_comp->GetTypeId());
  }

  for (const auto& [type_id, entry] : pooled_components_) {
    add_dependencies(entry->pool_ptr->GetUntyped(entry->handle), type_id);
  }

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
  std::vector<TypeId> sorted_ids;
  try {
    sorted_ids = TopologicallySortedPooledEntries();
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "Exception during component dependency sort: {}", ex.what());
    return;
  } catch (...) {
    LOG_F(ERROR, "Unknown exception during component dependency sort");
    return;
  }

  for (auto type_id : sorted_ids) {
    try {
      if (auto pooled_it = pooled_components_.find(type_id);
        pooled_it != pooled_components_.end()) {
        DCHECK_NOTNULL_F(pooled_it->second, "corrupted pooled entry, null");
        pooled_components_.erase(pooled_it);
        continue;
      }
      auto local_it = std::ranges::find_if(local_components_,
        [type_id](const auto& comp) { return comp->GetTypeId() == type_id; });
      DCHECK_NE_F(local_it, local_components_.end());
      if (local_it->use_count() == 1) {
        DLOG_F(1, "Destroying local component(t={}/{})",
          (*local_it)->GetTypeId(), (*local_it)->GetTypeNamePretty());
      }
      local_components_.erase(local_it);
    } catch (const std::exception& ex) {
      LOG_F(ERROR, "Exception while destroying component (type_id={}): {}",
        type_id, ex.what());
    } catch (...) {
      LOG_F(ERROR, "Unknown exception while destroying component (type_id={})",
        type_id);
    }
  }
}

// --- Copy/Move ---

Composition::Composition(const Composition& other)
  : local_components_(other.local_components_)
  , pooled_components_(other.pooled_components_)
{
}

auto Composition::operator=(const Composition& other) -> Composition&
{
  if (this != &other) {
    local_components_ = other.local_components_;
    pooled_components_ = other.pooled_components_;
  }
  return *this;
}

// NOLINTNEXTLINE(bugprone-exception-escape)
Composition::Composition(Composition&& other) noexcept
  : local_components_(std::move(other.local_components_))
  , pooled_components_(std::move(other.pooled_components_))
{
  static_assert(
    std::is_nothrow_move_constructible_v<decltype(local_components_)>,
    "local_components_ must be nothrow-move-constructible");
  // NOTE: The following static_assert fails on some standard libraries
  // due to conservative noexcept propagation for std::unordered_map,
  // even with the default allocator. See
  // https://en.cppreference.com/w/cpp/container/unordered_map/unordered_map
  // static_assert(
  //   std::is_nothrow_move_constructible_v<decltype(pooled_components_)>,
  //   "pooled_components_ must be nothrow-move-constructible");
}

// NOLINTNEXTLINE(bugprone-exception-escape)
auto Composition::operator=(Composition&& other) noexcept -> Composition&
{
  if (this != &other) {
    DestroyComponents();
    local_components_ = std::move(other.local_components_);
    pooled_components_ = std::move(other.pooled_components_);
  }
  return *this;
}

Composition::Composition(
  const LocalCapacity local_capacity, const PooledCapacity pooled_capacity)
{
  local_components_.reserve(local_capacity);
  pooled_components_.reserve(pooled_capacity);
}

// --- Dependency Validation ---

namespace {
auto ValidateDependencies(
  const TypeId comp_id, const std::span<const TypeId> dependencies) -> void
{
  DCHECK_F(!dependencies.empty(), "Dependencies must not be empty");
  for (size_t i = 0; i < dependencies.size(); ++i) {
    const auto dep_id = dependencies[i];
    if (dep_id == comp_id) {
      throw oxygen::ComponentError("Component cannot depend on itself");
    }
    for (size_t j = 0; j < i; ++j) {
      if (dependencies[j] == dep_id) {
        throw oxygen::ComponentError("Duplicate dependency detected");
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
    const bool found = pooled_components_.contains(dep)
      || std::ranges::any_of(local_components_,
        [&](const auto& c) { return c && c->GetTypeId() == dep; });
    if (!found) {
      throw ComponentError("Missing dependency component");
    }
  }
}

// --- Component Existence ---

auto Composition::HasLocalComponentImpl(TypeId id) const -> bool
{
  return std::ranges::any_of(local_components_,
    [id](const auto& comp) { return comp->GetTypeId() == id; });
}
auto Composition::HasPooledComponentImpl(const TypeId id) const -> bool
{
  return pooled_components_.contains(id);
}

// --- Dependency Update ---

auto Composition::UpdateComponentDependencies(Component& component) noexcept
  -> void
{
  if (component.HasDependencies()) {
    component.UpdateDependencies(
      [this](const TypeId id) -> Component& { return GetComponentImpl(id); });
  }
}

// --- Component Lookup ---

auto Composition::GetComponentImpl(TypeId type_id) const -> const Component&
{
  if (const auto pooled_it = pooled_components_.find(type_id);
    pooled_it != pooled_components_.end()) {
    auto* pool = pooled_it->second->pool_ptr;
    if (pool == nullptr) {
      throw ComponentError("Pooled component pool pointer is null");
    }
    auto* ptr = pool->GetUntyped(pooled_it->second->handle);
    if (ptr == nullptr) {
      throw ComponentError("Pooled component handle invalid");
    }
    return *ptr;
  }
  const auto it = std::ranges::find_if(local_components_,
    [type_id](const auto& comp) { return comp->GetTypeId() == type_id; });
  if (it == local_components_.end()) {
    throw ComponentError("Missing dependency component");
  }
  return **it;
}

//! Non-const overload implemented via const version.
/*!
 Safe because storage never holds const Component. In this implementation, both
 local and pooled components are stored as mutable objects (e.g.,
 std::shared_ptr<Component> and pointers from pools), so the returned reference
 is to a non-const object unless the Composition itself is const.
*/
auto Composition::GetComponentImpl(const TypeId type_id) -> Component&
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
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
  const auto* ptr = pool.GetUntyped(it->second->handle);
  DCHECK_NOTNULL_F(ptr, "unexpected invalid pooled component");
  return *ptr;
}

//! Non-const overload implemented via const version.
/*!
 Safe because storage never holds const Component. In this implementation, both
 local and pooled components are stored as mutable objects (e.g.,
 std::shared_ptr<Component> and pointers from pools), so the returned reference
 is to a non-const object unless the Composition itself is const.
*/
auto Composition::GetPooledComponentImpl(
  const composition::detail::ComponentPoolUntyped& pool, const TypeId type_id)
  -> Component&
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  return const_cast<Component&>(
    std::as_const(*this).GetPooledComponentImpl(pool, type_id));
}

// --- Deep Copy ---

auto Composition::DeepCopyComponentsFrom(const Composition& other) -> void
{
  std::unique_lock lock(mutex_);
  DeepCopyLocalComponentsFrom(other);
  DeepCopyPooledComponentsFrom(other);

  auto update_dependencies = [this](auto& container, auto get_comp) {
    for (const auto& entry : container) {
      if (auto* comp = get_comp(entry); comp && comp->HasDependencies()) {
        comp->UpdateDependencies([this](const TypeId id) -> Component& {
          // Pass the actual component pointer to the dependent
          return GetComponentImpl(id);
        });
      }
    }
  };
  update_dependencies(local_components_, [](const auto& sp) {
    // local components are stored as shared_ptr
    return sp.get();
  });
  update_dependencies(pooled_components_, [](const auto& pe) {
    // pooled components are stored as PooledEntry
    return pe.second->GetComponent();
  });
}

auto Composition::DeepCopyLocalComponentsFrom(const Composition& other) -> void
{
  local_components_.clear();
  local_components_.reserve(other.local_components_.size());
  for (const auto& entry : other.local_components_) {
    if (const Component* comp = entry.get(); comp->IsCloneable()) {
      auto clone = comp->Clone();
      if (!clone) {
        throw ComponentError("Failed to clone local component");
      }
      local_components_.push_back(std::move(clone));
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
    auto* pool = entry->pool_ptr;
    auto src_handle = entry->handle;
    if ((pool == nullptr) || !src_handle.IsValid()) {
      throw ComponentError("Invalid pooled entry in source composition");
    }
    const Component* src_comp = pool->GetUntyped(src_handle);
    DCHECK_NOTNULL_F(src_comp);
    if (!src_comp->IsCloneable()) {
      throw ComponentError("Pooled component must be cloneable");
    }
    ResourceHandle new_handle = pool->Allocate(src_comp->Clone());
    if (!new_handle.IsValid()) {
      throw ComponentError("Failed to allocate pooled component clone");
    }
    pooled_components_[type_id]
      = std::make_shared<PooledEntry>(new_handle, pool);
  }
}

// --- Dependency Check ---

namespace {
auto EnsureTypeIsNoInDependenciesOf(const Component& comp, TypeId type_id)
  -> void
{
  if (std::ranges::find(comp.Dependencies(), type_id)
    != comp.Dependencies().end()) {
    const auto& tr = oxygen::TypeRegistry::Get();
    throw oxygen::ComponentError(
      fmt::format("component({}/{}) is required by other "
                  "components, including at least ({}/{})",
        type_id, tr.GetTypeNamePretty(type_id), comp.GetTypeId(),
        comp.GetTypeNamePretty()));
  }
}
} // namespace

auto Composition::EnsureNotRequired(const TypeId type_id) const -> void
{
  for (const auto& comp : local_components_) {
    EnsureTypeIsNoInDependenciesOf(*comp, type_id);
  }
  for (const auto& entry : pooled_components_ | std::views::values) {
    const auto& [handle, pool_ptr] = *entry;
    DCHECK_F(handle.IsValid(), "pooled entry with invalid handle");
    DCHECK_NOTNULL_F(pool_ptr, "pooled entry with no pool");
    const auto* comp = pool_ptr->GetUntyped(handle);
    DCHECK_NOTNULL_F(comp, "pooled entry with no component");
    EnsureTypeIsNoInDependenciesOf(*comp, type_id);
  }
}

// --- Component Info Printing/Logging ---

auto Composition::PrintComponentInfo(std::ostream& out, const TypeId type_id,
  const std::string_view type_name, const std::string_view kind,
  const Component* comp) const -> void
{
  out << "   [" << type_id << "] " << type_name << " (" << kind << ")";
  if (comp == nullptr) {
    out << " [INVALID]\n";
    return;
  }
  if (comp->HasDependencies() && !comp->Dependencies().empty()) {
    out << " << Requires: ";
    for (size_t i = 0; i < comp->Dependencies().size(); ++i) {
      const auto dep_type_id = comp->Dependencies()[i];
      std::string_view dep_name;
      try {
        dep_name = GetComponentImpl(dep_type_id).GetTypeNamePretty();
      } catch (...) {
        dep_name = TryGetTypeNamePretty(dep_type_id);
      }
      out << dep_name;
      if (i + 1 < comp->Dependencies().size()) {
        out << ", ";
      }
    }
  }
  out << "\n";
}

auto Composition::GetDebugName() const -> std::string_view
{
  if (HasComponent<ObjectMetaData>()) {
    // We do not use RTTI, and thus cannot use dynamic_cast, but our type system
    // and the guarantees of Composition, ensure that if the Composition has a
    // component of a certain type, then that component can be safely cast to
    // that type.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    return static_cast<const ObjectMetaData&>(
      GetComponentImpl(ObjectMetaData::ClassTypeId()))
      .GetName();
  }
  return GetTypeNamePretty();
}

auto Composition::LogComponentInfo(const TypeId type_id,
  const std::string_view type_name, const std::string_view kind,
  const Component* comp) const -> void
{
  if (comp == nullptr) {
    LOG_F(INFO, "[{}] {} ({}) [INVALID]", type_id, type_name, kind);
    return;
  }
  LOG_F(INFO, "[{}] {} ({})", type_id, type_name, kind);
  if (comp->HasDependencies() && !comp->Dependencies().empty()) {
    LOG_SCOPE_F(INFO, "Requires");
    for (size_t i = 0; i < comp->Dependencies().size(); ++i) {
      const auto dep_type_id = comp->Dependencies()[i];
      std::string_view dep_name;
      try {
        dep_name = GetComponentImpl(dep_type_id).GetTypeNamePretty();
      } catch (...) {
        dep_name = TryGetTypeNamePretty(dep_type_id);
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
  for (const auto& entry : local_components_) {
    PrintComponentInfo(out, entry->GetTypeId(), entry->GetTypeNamePretty(),
      "Direct", entry.get());
  }
  for (const auto& [type_id, pooled_entry] : pooled_components_) {
    const Component* comp = (pooled_entry && (pooled_entry->pool_ptr != nullptr)
                              && pooled_entry->handle.IsValid())
      ? pooled_entry->GetComponent()
      : nullptr;
    PrintComponentInfo(
      out, type_id, TryGetTypeNamePretty(type_id), "Pooled", comp);
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
      const Component* comp
        = (pooled_entry && (pooled_entry->pool_ptr != nullptr)
            && pooled_entry->handle.IsValid())
        ? pooled_entry->GetComponent()
        : nullptr;
      LogComponentInfo(type_id, TryGetTypeNamePretty(type_id), "Pooled", comp);
    }
  }
}
