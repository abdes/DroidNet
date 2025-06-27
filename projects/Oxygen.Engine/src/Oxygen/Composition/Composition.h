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

#include <fmt/format.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/ComponentPool.h>
#include <Oxygen/Composition/ComponentPoolRegistry.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Composition/api_export.h>

namespace oxygen {

//! Manages a collection of components, supporting both pooled and non-pooled
//! types.
/*!
 Composition is the core container for managing components within an object. It
 supports both direct (local) and pooled (shared) components, enforces
 dependency relationships, and provides robust copy, move, and deep clone
 semantics. Thread safety is provided for most operations except for copy/
 move/clone, which require external synchronization.

 ### Key Features
 - **Component Management**: Add, remove, replace, and query components by type.
 - **Dependency Enforcement**: Ensures required dependencies are present and not
   violated.
 - **Pooled Component Support**: Integrates with type-erased component pools for
   memory efficiency.
 - **Deep and Shallow Copy**: Supports both shallow and deep copying of
   component state.
 - **Thread Safety**: Uses shared mutex for safe concurrent access (except
   copy/move/clone).

 ### Usage Patterns
 - Use `AddComponent<T>()` to add a new component of type T.
 - Use `RemoveComponent<T>()` to remove a component.
 - Use `GetComponent<T>()` to access a component by type.
 - Use `ReplaceComponent<OldT, NewT>()` to swap component types.

 ### Architecture Notes
 - Local components are stored as shared pointers for flexible ownership.
 - Pooled components are managed via handles and type-erased pools.
 - Dependency checks are enforced at add/replace/remove time.

 @warning Not thread-safe during copy, move, or clone operations; caller must
          ensure exclusive access.
 @see Component, IComponentPoolUntyped, CloneableMixin
*/
class Composition : public virtual Object {
  OXYGEN_TYPED(Composition)

  // Storage for non-pooled components - optimal for <8 components
  std::vector<std::shared_ptr<Component>> local_components_;

  //! Entry in the pooled components map, storing a handle and a type-erased
  //! pointer to the component pool.
  struct PooledEntry {
    ResourceHandle handle { ResourceHandle::kInvalidIndex };
    //! Type-erased pointer to pool
    composition::detail::ComponentPoolUntyped* pool_ptr { nullptr };

    PooledEntry() = default;
    PooledEntry(ResourceHandle _handle,
      composition::detail::ComponentPoolUntyped* pool_ptr)
      : handle(std::move(_handle))
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

  //! Mutex for thread-safe access to components.
  mutable std::shared_mutex mutex_;

  // Allow access to components from CloneableMixin to make a deep copy.
  template <typename T> friend class CloneableMixin;

public:
  //! Destructor. Destroys all components after topological sort, ensuring
  //! dependencies are destroyed after their dependents.
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

  //! Checks if a component of type T exists in the composition.
  template <typename T> [[nodiscard]] auto HasComponent() const -> bool
  {
    std::shared_lock lock(mutex_);
    return IsPooledComponent<T> ? HasPooledComponentImpl(T::ClassTypeId())
                                : HasLocalComponentImpl(T::ClassTypeId());
  }

  //! Retrieves a component of type T from the composition.
  /*!
   Returns a reference to the component of type T, if present. For pooled
   components, the reference is obtained from the associated component pool;
   for non-pooled components, it is returned from local storage. Throws if the
   component does not exist.

   @tparam T The component type to retrieve.
   @return Reference to the component of type T.

   @see AddComponent, RemoveComponent, HasComponent
  */
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

  //! @copydoc GetComponent
  template <typename T> [[nodiscard]] auto GetComponent() -> T&
  {
    return const_cast<T&>(std::as_const(*this).GetComponent<T>());
  }

  //! Prints a summary of all components to the given output stream.
  OXGN_COM_API auto PrintComponents(std::ostream& out) const -> void;

  //! Logs a summary of all components using the logging system (using debug
  //! logs at INFO verbosity).
  OXGN_COM_API auto LogComponents() const -> void;

protected:
  //! Constructs a new empty composition.
  /*!
   @note Compositions can only be constructed from their concrete subclasses.
  */
  Composition() = default;

  //! Constructs a new empty composition with an initial capacity for local and
  //! pooled components.
  /*!
   @note Compositions can only be constructed from their concrete subclasses.

   @param local_capacity Initial capacity for local components.
   @param pooled_capacity Initial capacity for pooled components.
  */
  OXGN_COM_API explicit Composition(
    std::size_t local_capacity, std::size_t pooled_capacity);

  /*!
   Adds a new component of type `T` to the composition, constructing it in-place
   with the provided arguments. Supports both pooled and non-pooled components.
   Enforces dependency requirements and prevents duplicate components of the
   same type. For pooled components, allocates from the associated component
   pool; for non-pooled, stores a shared pointer locally.

   @tparam Args Argument types for the component's constructor.
   @tparam T    The component type to add. Must satisfy `IsComponent`.
   @param args  Arguments forwarded to the component's constructor.
   @return      Reference to the newly added component of type `T`.

   @throw ComponentError if the component already exists or if dependencies are
   missing.

   ### Performance Characteristics

   - Time Complexity: O(1) for both pooled and local components (amortized).
   - Memory: Allocates memory for the component (from pool or heap).
   - Optimization: Uses in-place construction and type-erased storage.

   ### Usage Examples
   ```cpp
   // Add a local component
   auto& comp = composition.AddComponent<MyComponent>(42, "init");
   // Add a pooled component
   auto& pooled = composition.AddComponent<PooledType>(param1, param2);
   ```

   @see RemoveComponent, ReplaceComponent, HasComponent, GetComponent
  */
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
      auto& pool = ComponentPoolRegistry::GetComponentPool<T>();
      auto handle
        = AllocatePooled<decltype(pool), T>(pool, std::forward<Args>(args)...);
      if (!handle.IsValid()) {
        throw ComponentError("Failed to allocate pooled component");
      }
      pooled_components_[T::ClassTypeId()]
        = std::make_shared<PooledEntry>(handle, &pool);
      component = pool.Get(handle);
    } else {
      auto component_ptr = MakeComponentPtr<T>(std::forward<Args>(args)...);
      local_components_.emplace_back(component_ptr);
      component = component_ptr.get();
    }
    DCHECK_NOTNULL_F(component);
    if constexpr (IsComponentWithDependencies<T>) {
      UpdateComponentDependencies(*component);
    }
    return static_cast<T&>(*component);
  }

  /*!
   Removes the component of type `T` from the composition, if present. For
   pooled components, releases the handle and removes the entry from the pooled
   components map. For non-pooled components, erases the shared pointer from
   local storage. If the component is required by another component, throws an
   exception and does not remove it.

   @tparam T   The component type to remove.

   @throw ComponentError if the component is required by another component.

   ### Performance Characteristics
   - Time Complexity: O(N).
   - Memory: Releases memory for the removed component.
   - Optimization: Uses type-erased lookup for pooled components.

   ### Usage Examples// Remove a local component
   composition.RemoveComponent<MyComponent>();
   // Remove a pooled component
   composition.RemoveComponent<PooledType>();
   @see AddComponent, ReplaceComponent, HasComponent, GetComponent
  */
  template <typename T> auto RemoveComponent() -> void
  {
    std::unique_lock lock(mutex_);
    auto type_id = T::ClassTypeId();
    auto remove_pooled = [&] {
      auto it = pooled_components_.find(type_id);
      if (it == pooled_components_.end()) {
        return;
      }
      EnsureNotRequired(type_id);
      pooled_components_.erase(it);
    };
    auto remove_local = [&] {
      auto it
        = std::ranges::find_if(local_components_, [&](const auto& local_comp) {
            return local_comp->GetTypeId() == type_id;
          });
      if (it == local_components_.end()) {
        return;
      }
      EnsureNotRequired(type_id);
      local_components_.erase(it);
    };
    if constexpr (IsPooledComponent<T>) {
      remove_pooled();
    } else {
      remove_local();
    }
  }

  /*!
   Replaces an existing component of type `OldT` with a new component of type
   `NewT`, constructing the new component in-place with the provided arguments.
   Supports both pooled and non-pooled components, but does not allow replacing
   a pooled component with a non-pooled one or vice versa. Ensures that the old
   component exists, the new component does not already exist, and that
   dependencies are not violated. Throws if the replacement would break
   dependency requirements or if allocation fails.

   @tparam OldT   The type of the component to be replaced.
   @tparam NewT   The type of the new component (defaults to OldT).
   @tparam Args   Argument types for the new component's constructor.

   @param args    Arguments forwarded to the new component's constructor.
   @return        Reference to the newly added component of type `NewT`.

   @throw ComponentError if replacement is invalid, dependencies are violated,
   the component to replace does not exist, or allocation fails.

   ### Performance Characteristics

   - Time Complexity: O(N).
   - Memory: Allocates memory for the new component and releases the old one.
   - Optimization: Uses in-place construction and type-erased storage.

   ### Usage Examples

   ```cpp
   // Replace a local component with another type
   auto& new_comp = composition.ReplaceComponent<OldType, NewType>(arg1, arg2);
   // Replace a pooled component with a new instance
   auto& pooled = composition.ReplaceComponent<PooledType>(param1, param2);
   ```

   @see AddComponent, RemoveComponent, HasComponent, GetComponent
  */
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
      if constexpr (IsComponentWithDependencies<NewT>) {
        if (std::ranges::find(NewT::ClassDependencies(), OldT::ClassTypeId())
          != NewT::ClassDependencies().end()) {
          throw ComponentError(
            "Cannot replace component; new component has dependencies on it");
        }
      }
    }

    Component* component;
    if constexpr (IsPooledComponent<OldT>) {
      auto& pool = ComponentPoolRegistry::GetComponentPool<NewT>();
      auto new_handle = AllocatePooled<decltype(pool), NewT>(
        pool, std::forward<Args>(args)...);
      if (!new_handle.IsValid()) {
        throw ComponentError("Failed to allocate pooled component");
      }
      pooled_components_.erase(old_id);
      pooled_components_[NewT::ClassTypeId()]
        = std::make_shared<PooledEntry>(new_handle, &pool);
      component = pool.Get(new_handle);
    } else {
      auto component_ptr = MakeComponentPtr<NewT>(std::forward<Args>(args)...);
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
  OXGN_COM_API auto DestroyComponents() noexcept -> void;

  template <IsComponent T> auto EnsureExistence(const bool state) -> void
  {
    auto type_id = T::ClassTypeId();
    const bool exists = IsPooledComponent<T> ? HasPooledComponentImpl(type_id)
                                             : HasLocalComponentImpl(type_id);
    if (exists != state) {
      throw ComponentError(fmt::format(
        "expecting component {}to be in the composition", state ? "" : "not "));
    }
  }

  template <IsComponent T> auto EnsureExists() -> void
  {
    EnsureExistence<T>(true);
  }
  template <IsComponent T> auto EnsureDoesNotExist() -> void
  {
    EnsureExistence<T>(false);
  }

  // --- DRY Helper: Forward unique_ptr<T> or construct in-place ---
  template <IsComponent T, typename... Args>
  static auto MakeComponentPtr(Args&&... args) -> std::shared_ptr<Component>
  {
    // Accept std::unique_ptr<T> or std::unique_ptr<Component>
    constexpr bool is_unique_ptr_derived
      = (std::is_same_v<std::decay_t<Args>, std::unique_ptr<T>> || ...);
    constexpr bool is_unique_ptr_base
      = (std::is_same_v<std::decay_t<Args>, std::unique_ptr<Component>> || ...);
    constexpr bool is_unique_ptr = is_unique_ptr_derived || is_unique_ptr_base;

    if constexpr (sizeof...(Args) == 1 && is_unique_ptr) {
      // Upcast unique_ptr<T> to shared_ptr<Component>
      auto&& tup = std::forward_as_tuple(std::forward<Args>(args)...);
      return std::shared_ptr<Component>(std::move(std::get<0>(tup)));
    } else {
      // Construct T and upcast to shared_ptr<Component>
      return std::make_shared<T>(std::forward<Args>(args)...);
    }
  }

  // --- DRY Helper: Forward unique_ptr<T> or construct in-place for pooled ---
  template <typename Pool, typename T, typename... Args>
  static auto AllocatePooled(Pool& pool, Args&&... args) -> ResourceHandle
  {
    if constexpr (sizeof...(Args) == 1
      && (std::is_same_v<std::decay_t<Args>, std::unique_ptr<T>> && ...)) {
      std::unique_ptr<Component> base_ptr
        = std::move(std::get<0>(std::forward_as_tuple(args...)));
      return pool.Allocate(std::move(base_ptr));
    } else {
      return pool.Allocate(std::forward<Args>(args)...);
    }
  }

  OXGN_COM_API auto EnsureDependencies(
    TypeId comp_id, std::span<const TypeId> dependencies) const -> void;
  OXGN_COM_API auto EnsureNotRequired(TypeId type_id) const -> void;
  OXGN_COM_NDAPI auto HasLocalComponentImpl(TypeId id) const -> bool;
  OXGN_COM_NDAPI auto HasPooledComponentImpl(TypeId id) const -> bool;
  OXGN_COM_API auto UpdateComponentDependencies(Component& component) noexcept
    -> void;
  OXGN_COM_NDAPI auto GetComponentImpl(TypeId type_id) const
    -> const Component&;
  OXGN_COM_NDAPI auto GetComponentImpl(TypeId type_id) -> Component&;
  OXGN_COM_NDAPI auto GetPooledComponentImpl(
    const composition::detail::ComponentPoolUntyped& pool, TypeId type_id) const
    -> const Component&;
  OXGN_COM_NDAPI auto GetPooledComponentImpl(
    const composition::detail::ComponentPoolUntyped& pool, TypeId type_id)
    -> Component&;
  OXGN_COM_API auto DeepCopyComponentsFrom(const Composition& other) -> void;
  OXGN_COM_API auto DeepCopyLocalComponentsFrom(const Composition& other)
    -> void;
  OXGN_COM_API auto DeepCopyPooledComponentsFrom(const Composition& other)
    -> void;

  auto GetDebugName() const -> std::string_view;
  auto PrintComponentInfo(std::ostream& out, TypeId type_id,
    std::string_view type_name, std::string_view kind,
    const Component* comp) const -> void;
  auto LogComponentInfo(TypeId type_id, std::string_view type_name,
    std::string_view kind, const Component* comp) const -> void;
  auto TopologicallySortedPooledEntries() -> std::vector<TypeId>;
};

//! Specifies the requirements on a type to be considered as a Composition.
template <typename T>
concept IsComposition = std::derived_from<T, Composition>;

//! Base class for cloneable objects, providing a virtual Clone method.
class Cloneable {
public:
  Cloneable() = default;
  virtual ~Cloneable() = default;

  OXYGEN_DEFAULT_COPYABLE(Cloneable)
  OXYGEN_DEFAULT_MOVABLE(Cloneable)

  [[nodiscard]] virtual auto Clone() const -> std::unique_ptr<Cloneable> = 0;
};

//! Mixin class for cloneable compositions using CRTP (Curiously Recurring
//! Template
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
    auto clone = std::unique_ptr<Derived>(new Derived());
    // Make a deep copy of the components
    clone->DeepCopyComponentsFrom(*original);
    return clone;
  }
};

} // namespace oxygen
