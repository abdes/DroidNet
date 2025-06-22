//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <shared_mutex>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Component.h>
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

  // Single storage for components - optimal for <8 components
  std::vector<std::shared_ptr<Component>> components_;

  // Allow access to components_ from CloneableMixin to make a deep copy.
  template <typename T> friend class CloneableMixin;

public:
  OXGN_COM_API ~Composition() noexcept override;

  //! Copy constructor, make a shallow copy of the composition.
  OXGN_COM_API Composition(const Composition& other);

  //! Copy assignment operator, make a shallow copy of the composition.
  OXGN_COM_API auto operator=(const Composition& rhs) -> Composition&;

  //! Moves the composition to the new object and leaves the original in an
  //! empty state.
  OXGN_COM_API Composition(Composition&& other) noexcept;

  //! Moves the composition to the new object and leaves the original in an
  //! empty state.
  OXGN_COM_API auto operator=(Composition&& rhs) noexcept -> Composition&;

  template <typename T> [[nodiscard]] auto HasComponent() const -> bool
  {
    std::shared_lock lock(mutex_);
    return HasComponentImpl(T::ClassTypeId());
  }

  template <typename T> [[nodiscard]] auto GetComponent() const -> T&
  {
    std::shared_lock lock(mutex_);
    return static_cast<T&>(GetComponentImpl(T::ClassTypeId()));
  }

  OXGN_COM_API auto PrintComponents(std::ostream& out) const -> void;

protected:
  OXGN_COM_API explicit Composition(std::size_t initial_capacity = 4);

  template <IsComponent T, typename... Args>
  auto AddComponent(Args&&... args) -> T&
  {
    std::unique_lock lock(mutex_);

    auto id = T::ClassTypeId();
    if (HasComponentImpl(T::ClassTypeId())) {
      throw ComponentError("Component already exists");
    }

    if (!T::ClassDependencies().empty()) {
      ValidateDependencies(id, T::ClassDependencies());
      EnsureDependencies(T::ClassDependencies());
    }

    auto component = std::make_shared<T>(std::forward<Args>(args)...);
    if (component->HasDependencies()) {
      component->UpdateDependencies([this](TypeId id) -> Component& {
        // Nollocking needed here
        return GetComponentImpl(id);
      });
    }

    auto& component_ref = AddComponentImpl(std::move(component));
    return static_cast<T&>(component_ref);
  }

  template <typename T> auto RemoveComponent() -> void
  {
    std::unique_lock lock(mutex_);

    auto id = T::ClassTypeId();
    if (!ExpectExistingComponent(id)) {
      return;
    }
    if (IsComponentRequired(id)) {
      throw ComponentError(
        "Cannot remove component; other components depend on it");
    }

    RemoveComponentImpl(id);
  }

  template <typename OldT, typename NewT = OldT, typename... Args>
  auto ReplaceComponent(Args&&... args) -> NewT&
  {
    {
      std::unique_lock lock(mutex_);

      if (auto old_id = OldT::ClassTypeId(); ExpectExistingComponent(old_id)) {
        if (IsComponentRequired(old_id) && !std::is_same_v<OldT, NewT>) {
          throw ComponentError("Cannot replace component with a different "
                               "type; other components depend on it");
        }
        auto component = std::make_shared<NewT>(std::forward<Args>(args)...);
        if (component->HasDependencies()) {
          component->UpdateDependencies([this](TypeId id) -> Component& {
            // Nollocking needed here
            return GetComponentImpl(id);
          });
        }

        auto& component_ref
          = ReplaceComponentImpl(old_id, std::move(component));
        return static_cast<NewT&>(component_ref);
      }
    }

    return AddComponent<NewT>(std::forward<Args>(args)...);
  }

  OXGN_COM_API auto HasComponents() const noexcept -> bool;

private:
  OXGN_COM_API auto DestroyComponents() noexcept -> void;

  OXGN_COM_API static auto ValidateDependencies(
    TypeId comp_id, std::span<const TypeId> dependencies) -> void;
  OXGN_COM_API auto EnsureDependencies(
    std::span<const TypeId> dependencies) const -> void;
  OXGN_COM_NDAPI auto ExpectExistingComponent(TypeId id) const -> bool;
  OXGN_COM_NDAPI auto IsComponentRequired(TypeId id) const -> bool;
  OXGN_COM_NDAPI auto HasComponentImpl(TypeId id) const -> bool;
  OXGN_COM_NDAPI auto AddComponentImpl(std::shared_ptr<Component> component)
    -> Component&;
  OXGN_COM_NDAPI auto ReplaceComponentImpl(
    TypeId old_id, std::shared_ptr<Component> new_component) -> Component&;
  OXGN_COM_NDAPI auto GetComponentImpl(TypeId id) const -> Component&;
  OXGN_COM_API auto RemoveComponentImpl(TypeId id) -> void;
  OXGN_COM_API auto DeepCopyComponentsFrom(const Composition& other) -> void;

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
