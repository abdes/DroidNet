//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <mutex>
#include <span>
#include <stdexcept>

#include <Oxygen/Composition/Object.h>
#include <Oxygen/Composition/api_export.h>

namespace oxygen {

class ComponentError final : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

class Composition;

class Component : public virtual Object {
public:
  OXYGEN_COMP_API ~Component() override = default;

  // All components should implement proper copy and move semantics to handle
  // copying and moving as appropriate.
  OXYGEN_COMP_API Component(const Component&) = default;
  OXYGEN_COMP_API auto operator=(const Component&) -> Component& = default;
  OXYGEN_COMP_API Component(Component&&) = default;
  OXYGEN_COMP_API auto operator=(Component&&) -> Component& = default;

  [[nodiscard]] virtual auto IsCloneable() const noexcept -> bool
  {
    return false;
  }

  //! Create a clone of the component.
  /*!
   \note The clone will not have any dependencies updated until a call to its
   UpdateDependencies method is made.
  */
  [[nodiscard]] virtual auto Clone() const -> std::unique_ptr<Component>
  {
    throw ComponentError("Component is not cloneable");
  }

protected:
  OXYGEN_COMP_API Component() = default;
  OXYGEN_COMP_API virtual void UpdateDependencies(
    [[maybe_unused]] const Composition& composition)
  {
  }

private:
  friend class Composition;
  static constexpr auto ClassDependencies() -> std::span<const TypeId>
  {
    return {};
  }
  [[nodiscard]] virtual auto HasDependencies() const -> bool { return false; }
  [[nodiscard]] virtual auto Dependencies() const -> std::span<const TypeId>
  {
    return ClassDependencies();
  }
};

// Non-member swap (move semantics implementation in derived classes)
inline void swap(Component& /*lhs*/, Component& /*rhs*/) noexcept { }

//! Specifies the requirements on a type to be considered as a Component.
template <typename T>
concept IsComponent = std::is_base_of_v<Component, T>;

class Composition : public virtual Object {
  OXYGEN_TYPED(Composition)

  struct ComponentManager;
  // Implementation of the component storage, wrapped in a shared_ptr so we
  // can share it when shallow copying the composition.
  std::shared_ptr<ComponentManager> pimpl_;

  // Allow access to components_ from CloneableMixin to make a deep copy.
  template <typename T> friend class CloneableMixin;

public:
  OXYGEN_COMP_API ~Composition() noexcept override;

  //! Copy constructor, make a shallow copy of the composition.
  OXYGEN_COMP_API Composition(const Composition& other);

  //! Copy assignment operator, make a shallow copy of the composition.
  OXYGEN_COMP_API auto operator=(const Composition& rhs) -> Composition&;

  //! Moves the composition to the new object and leaves the original in an
  //! empty state.
  OXYGEN_COMP_API Composition(Composition&& other) noexcept;

  //! Moves the composition to the new object and leaves the original in an
  //! empty state.
  OXYGEN_COMP_API auto operator=(Composition&& rhs) noexcept -> Composition&;

  template <typename T> [[nodiscard]] auto HasComponent() const -> bool
  {
    return HasComponentImpl(T::ClassTypeId());
  }

  template <typename T> [[nodiscard]] auto GetComponent() const -> T&
  {
    return static_cast<T&>(GetComponentImpl(T::ClassTypeId()));
  }

  template <typename ValueType> class OXYGEN_COMP_API Iterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = ValueType;
    using difference_type = std::ptrdiff_t;
    using pointer = ValueType*;
    using reference = ValueType&;

    Iterator() = default;

    auto operator*() const -> reference;
    auto operator->() const -> pointer;
    auto operator++() -> Iterator&;
    auto operator++(int) -> Iterator;
    auto operator==(const Iterator& rhs) const -> bool;
    auto operator!=(const Iterator& rhs) const -> bool;

  private:
    friend class Composition;
    Iterator(ComponentManager* mgr, const size_t pos)
      : mgr_(mgr)
      , pos_(pos)
    {
    }
    ComponentManager* mgr_;
    size_t pos_;
  };

  using iterator = Iterator<Component>;
  using const_iterator = Iterator<const Component>;

  OXYGEN_COMP_API virtual auto begin() -> iterator;
  OXYGEN_COMP_API virtual auto end() -> iterator;
  OXYGEN_COMP_API virtual auto begin() const -> const_iterator;
  OXYGEN_COMP_API virtual auto end() const -> const_iterator;
  OXYGEN_COMP_API virtual auto cbegin() const -> const_iterator;
  OXYGEN_COMP_API virtual auto cend() const -> const_iterator;

  OXYGEN_COMP_API void PrintComponents(std::ostream& out) const;

protected:
  OXYGEN_COMP_API explicit Composition(std::size_t initial_capacity = 4);

  template <IsComponent T, typename... Args>
  auto AddComponent(Args&&... args) -> T&
  {
    std::lock_guard lock(mutex_);

    auto id = T::ClassTypeId();
    if (HasComponent<T>()) {
      throw ComponentError("Component already exists");
    }

    if (!T::ClassDependencies().empty()) {
      ValidateDependencies(id, T::ClassDependencies());
      EnsureDependencies(T::ClassDependencies());
    }

    auto component = std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    auto& component_ref
      = static_cast<T&>(AddComponentImpl(std::move(component)));
    if (!T::ClassDependencies().empty()) {
      component_ref.UpdateDependencies(*this);
    }
    return component_ref;
  }

  template <typename T> void RemoveComponent()
  {
    std::lock_guard lock(mutex_);

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
      std::lock_guard lock(mutex_);

      if (auto old_id = OldT::ClassTypeId(); ExpectExistingComponent(old_id)) {
        if (IsComponentRequired(old_id) && !std::is_same_v<OldT, NewT>) {
          throw ComponentError("Cannot replace component with a different "
                               "type; other components depend on it");
        }
        auto component
          = std::unique_ptr<NewT>(new NewT(std::forward<Args>(args)...));
        auto& component_ref = static_cast<NewT&>(
          ReplaceComponentImpl(old_id, std::move(component)));
        if (!NewT::ClassDependencies().empty()) {
          component_ref.UpdateDependencies(*this);
        }
        return component_ref;
      }
    }

    return AddComponent<NewT>(std::forward<Args>(args)...);
  }

  OXYGEN_COMP_API auto HasComponents() const noexcept -> bool;
  OXYGEN_COMP_API void DestroyComponents() noexcept;

private:
  OXYGEN_COMP_API static void ValidateDependencies(
    TypeId comp_id, std::span<const TypeId> dependencies);
  OXYGEN_COMP_API void EnsureDependencies(
    std::span<const TypeId> dependencies) const;
  [[nodiscard]] OXYGEN_COMP_API auto ExpectExistingComponent(TypeId id) const
    -> bool;
  [[nodiscard]] OXYGEN_COMP_API auto IsComponentRequired(TypeId id) const
    -> bool;
  [[nodiscard]] OXYGEN_COMP_API auto HasComponentImpl(TypeId id) const -> bool;
  [[nodiscard]] OXYGEN_COMP_API auto AddComponentImpl(
    std::unique_ptr<Component> component) const -> Component&;
  [[nodiscard]] OXYGEN_COMP_API auto ReplaceComponentImpl(TypeId old_id,
    std::unique_ptr<Component> new_component) const -> Component&;
  [[nodiscard]] OXYGEN_COMP_API auto GetComponentImpl(TypeId id) const
    -> Component&;
  OXYGEN_COMP_API void RemoveComponentImpl(
    TypeId id, bool update_indices = true) const;
  OXYGEN_COMP_API void DeepCopyComponentsFrom(const Composition& other);

  mutable std::mutex mutex_;
};
static_assert(std::forward_iterator<Composition::Iterator<Component>>);
static_assert(std::ranges::common_range<Composition>);

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
