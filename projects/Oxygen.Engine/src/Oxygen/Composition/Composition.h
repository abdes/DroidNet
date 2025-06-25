//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <concepts>
#include <shared_mutex>
#include <span>
#include <unordered_map>
#include <utility> // std::as_const
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/ComponentPool.h>
#include <Oxygen/Composition/ComponentPoolRegistry.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Composition/api_export.h>

namespace oxygen {

// NOTE: The Composition class is not thread-safe during cloning, shallow
// copying, moving or assignment. The standard C++ practice is for the caller to
// ensure exclusive access to the source object being copied (e.g., by
// externally synchronizing access, or by not sharing the object across threads
// during copy).

class Composition : public virtual Object {
  OXYGEN_TYPED(Composition)

  // Single storage for non-pooled components - optimal for <8 components
  std::vector<std::shared_ptr<Component>> local_components_;

  struct PooledEntry {
    ResourceHandle handle { ResourceHandle::kInvalidIndex };
    IComponentPoolUntyped* pool_ptr { nullptr }; // type-erased pointer to pool

    PooledEntry() = default;
    PooledEntry(const ResourceHandle& handle, IComponentPoolUntyped* pool_ptr)
      : handle(handle)
      , pool_ptr(pool_ptr)
    {
      DCHECK_F(handle.IsValid());
      DCHECK_NOTNULL_F(pool_ptr);
    }
    OXGN_COM_API ~PooledEntry() noexcept;

    OXYGEN_DEFAULT_COPYABLE(PooledEntry)
    OXYGEN_DEFAULT_MOVABLE(PooledEntry)

    // Helper to get the component from the pool
    OXGN_COM_NDAPI auto GetComponent() const -> Component*;
  };
  // Storage for pooled components, using the type-erased pool interface for a
  // generic lookup
  std::unordered_map<TypeId, std::shared_ptr<PooledEntry>> pooled_components_;

  // Allow access to components from CloneableMixin to make a deep copy.
  template <typename T> friend class CloneableMixin;

public:
  OXGN_COM_API ~Composition() noexcept override;

  //! Copy constructor, make a shallow copy of the composition.
  OXGN_COM_API Composition(const Composition& other);

  //! Copy assignment operator, make a shallow copy of the composition.
  OXGN_COM_API auto operator=(const Composition& other) -> Composition&;

  //! Moves the composition to the new object and leaves the original in an
  //! empty state.
  OXGN_COM_API Composition(Composition&& other) noexcept;

  //! Moves the composition to the new object and leaves the original in an
  //! empty state.
  OXGN_COM_API auto operator=(Composition&& other) noexcept -> Composition&;

  template <typename T> [[nodiscard]] auto HasComponent() const -> bool
  {
    std::shared_lock lock(mutex_);
    if constexpr (IsPooledComponent<T>) {
      return HasPooledComponentImpl(T::ClassTypeId());
    } else {
      return HasLocalComponentImpl(T::ClassTypeId());
    }
  }

  template <typename T> [[nodiscard]] auto GetComponent() const -> const T&
  {
    std::shared_lock lock(mutex_);
    if constexpr (IsPooledComponent<T>) {
      auto& pool = ComponentPoolRegistry::GetComponentPool<T>();
      return static_cast<const T&>(
        GetPooledComponentImpl(pool, T::ClassTypeId()));
    } else {
      return static_cast<const T&>(GetComponentImpl(T::ClassTypeId()));
    }
  }
  template <typename T> [[nodiscard]] auto GetComponent() -> T&
  {
    return const_cast<T&>(std::as_const(*this).GetComponent<T>());
  }

  OXGN_COM_API auto PrintComponents(std::ostream& out) const -> void;

protected:
  OXGN_COM_API explicit Composition(std::size_t initial_capacity = 4);

  template <IsComponent T, typename... Args>
  auto AddComponent(Args&&... args) -> T&
  {
    std::unique_lock lock(mutex_);

    EnsureDoesNotExist<T>();

    if constexpr (IsComponentWithDependencies<T>) {
      EnsureDependencies(T::ClassTypeId(), T::ClassDependencies());
    }

    Component* component;
    if constexpr (IsPooledComponent<T>) {
      // Pooled component: allocate from pool and store handle and pool pointer
      auto& pool = ComponentPoolRegistry::GetComponentPool<T>();
      auto handle = pool.Allocate(std::forward<Args>(args)...);
      if (!handle.IsValid()) {
        throw ComponentError("Failed to allocate pooled component");
      }
      pooled_components_[T::ClassTypeId()]
        = std::make_shared<PooledEntry>(handle, &pool);
      component = pool.Get(handle);
    } else {
      auto component_ptr = std::make_shared<T>(std::forward<Args>(args)...);
      local_components_.emplace_back(component_ptr);
      component = component_ptr.get();
    }
    DCHECK_NOTNULL_F(component);
    if constexpr (IsComponentWithDependencies<T>) {
      UpdateComponentDependencies(*component);
    }
    return static_cast<T&>(*component);
  }

  template <typename T> auto RemoveComponent() -> void
  {
    std::unique_lock lock(mutex_);
    auto type_id = T::ClassTypeId();
    if constexpr (IsPooledComponent<T>) {
      auto it = pooled_components_.find(type_id);
      if (it == pooled_components_.end()) {
        return;
      }
      EnsureNotRequired(type_id);
      pooled_components_.erase(it);
    } else {
      auto it
        = std::ranges::find_if(local_components_, [&](const auto& local_comp) {
            return local_comp->GetTypeId() == type_id;
          });
      if (it == local_components_.end()) {
        return;
      }
      EnsureNotRequired(type_id);
      local_components_.erase(it);
    }
  }

  template <typename OldT, typename NewT = OldT, typename... Args>
  auto ReplaceComponent(Args&&... args) -> NewT&
  {
    static_assert(IsPooledComponent<OldT> == IsPooledComponent<NewT>,
      "Cannot replace pooled with non-pooled or vice versa");

    std::unique_lock lock(mutex_);

    EnsureExists<OldT>();

    auto old_id = OldT::ClassTypeId();
    if constexpr (!std::is_same_v<OldT, NewT>) {
      EnsureDoesNotExist<NewT>();
      EnsureNotRequired(old_id);
      // Also ensure not required by the new component
      if constexpr (IsComponentWithDependencies<NewT>) {
        if (std::ranges::find(NewT::ClassDependencies(), OldT::ClassTypeId())
          != NewT::ClassDependencies().end()) {
          throw ComponentError("Cannot replace component; new component has "
                               "dependencies on it");
        }
      }
    }

    Component* component;
    if constexpr (IsPooledComponent<OldT>) {
      // Pre-allocate new pooled component (do not register handle yet)
      auto& pool = ComponentPoolRegistry::GetComponentPool<NewT>();
      auto new_handle = pool.Allocate(std::forward<Args>(args)...);
      if (!new_handle.IsValid()) {
        throw ComponentError("Failed to allocate pooled component");
      }
      // Remove old pooled handle and deallocate from pool
      auto it = pooled_components_.find(old_id);
      if (it != pooled_components_.end()) {
        pooled_components_.erase(it);
      }
      // Register new pooled handle
      pooled_components_[NewT::ClassTypeId()]
        = std::make_shared<PooledEntry>(new_handle, &pool);
      component = pool.Get(new_handle);
    } else {
      // Non-pooled: construct new component first
      auto component_ptr = std::make_shared<NewT>(std::forward<Args>(args)...);
      std::replace_if(
        local_components_.begin(), local_components_.end(),
        [old_id](const auto& comp) { return comp->GetTypeId() == old_id; },
        component_ptr);
      component = component_ptr.get();
    }
    DCHECK_NOTNULL_F(component);
    if constexpr (IsComponentWithDependencies<NewT>) {
      UpdateComponentDependencies(*component);
    }
    return static_cast<NewT&>(*component);
  }

  OXGN_COM_API auto HasComponents() const noexcept -> bool;

private:
  template <IsComponent T> auto EnsureExistence(bool state) -> void
  {
    auto type_id = T::ClassTypeId();
    const bool exists = [&] {
      if constexpr (IsPooledComponent<T>) {
        return HasPooledComponentImpl(type_id);
      } else {
        return HasLocalComponentImpl(type_id);
      }
    }();
    if (exists != state) {
      throw ComponentError(fmt::format(
        "expecting component {}to be in the composition", state ? "" : "not "));
    }
  }

  template <IsComponent T> auto EnsureExists() -> void
  {
    return EnsureExistence<T>(true);
  }

  template <IsComponent T> auto EnsureDoesNotExist() -> void
  {
    return EnsureExistence<T>(false);
  }

  OXGN_COM_API auto DestroyComponents() noexcept -> void;

  OXGN_COM_NDAPI auto HasLocalComponentImpl(TypeId id) const -> bool;
  OXGN_COM_NDAPI auto HasPooledComponentImpl(TypeId id) const -> bool;
  OXGN_COM_API auto UpdateComponentDependencies(Component& component) noexcept
    -> void;

  OXGN_COM_NDAPI auto ReplaceComponentImpl(
    TypeId old_id, std::shared_ptr<Component> new_component) -> Component&;
  // Unified: GetComponentImpl handles both pooled and non-pooled components
  OXGN_COM_NDAPI auto GetComponentImpl(TypeId type_id) const
    -> const Component&;
  OXGN_COM_NDAPI auto GetComponentImpl(TypeId type_id) -> Component&;
  OXGN_COM_NDAPI auto GetPooledComponentImpl(const IComponentPoolUntyped& pool,
    TypeId type_id) const -> const Component&;
  OXGN_COM_NDAPI auto GetPooledComponentImpl(
    const IComponentPoolUntyped& pool, TypeId type_id) -> Component&;

  OXGN_COM_API auto EnsureDependencies(
    TypeId comp_id, std::span<const TypeId> dependencies) const -> void;
  OXGN_COM_API auto EnsureNotRequired(TypeId type_id) const -> void;

  OXGN_COM_API auto DeepCopyComponentsFrom(const Composition& other) -> void;

  auto TopologicallySortedPooledEntries() -> std::vector<TypeId>;

  // Helper to print a component's one-line info, and dependencies if present
  auto PrintComponentInfo(std::ostream& out, TypeId type_id,
    std::string_view type_name, std::string_view kind,
    const Component* comp) const -> void;

  mutable std::shared_mutex mutex_;
};

template <typename T>
concept IsComposition = std::derived_from<T, Composition>;

class Cloneable {
public:
  Cloneable() = default;
  virtual ~Cloneable() = default;

  OXYGEN_DEFAULT_COPYABLE(Cloneable)
  OXYGEN_DEFAULT_MOVABLE(Cloneable)

  [[nodiscard]] virtual auto Clone() const -> std::unique_ptr<Cloneable> = 0;
};

template <typename Derived> class CloneableMixin {
  friend Derived;
  CloneableMixin() = default; // prevent instantiation (CRTP)
  CloneableMixin(const CloneableMixin&) = default;
  CloneableMixin(CloneableMixin&&) = default;

public:
  auto operator=(const CloneableMixin&) -> CloneableMixin& = default;
  auto operator=(CloneableMixin&&) -> CloneableMixin& = default;

protected:
  ~CloneableMixin() = default;

public:
  [[nodiscard]] auto Clone() const -> std::unique_ptr<Derived>
  {
    // We have to delay the type check until the derived class is fully defined.
    static_assert(
      IsComposition<Derived>, "Derived must satisfy the IsComposition concept");

    auto* original = static_cast<const Derived*>(this);
    // Make a shallow copy of the object
    auto clone = std::make_unique<Derived>(*original);
    // Make a deep copy of the components
    clone->DeepCopyComponentsFrom(*original);
    return clone;
  }
};

} // namespace oxygen
