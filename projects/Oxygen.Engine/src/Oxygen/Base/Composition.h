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

#include "Oxygen/Base/Object.h"
#include "Oxygen/Base/api_export.h"

namespace oxygen {

class ComponentError final : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Composition;

class Component : public virtual Object {
public:
    OXYGEN_BASE_API ~Component() override = default;

    // All components should implement proper copy and move semantics to handle
    // copying and moving as appropriate.
    OXYGEN_BASE_API Component(const Component&) = default;
    OXYGEN_BASE_API Component& operator=(const Component&) = default;
    OXYGEN_BASE_API Component(Component&&) = default;
    OXYGEN_BASE_API Component& operator=(Component&&) = default;

    [[nodiscard]] virtual auto IsCloneable() const noexcept -> bool { return false; }

    //! Create a clone of the component.
    /*!
     \note The clone will not have its dependencies updated until a call to its
     UpdateDependencies method is made.
    */
    [[nodiscard]] virtual auto Clone() const -> std::unique_ptr<Component>
    {
        throw ComponentError("Component is not cloneable");
    }

protected:
    OXYGEN_BASE_API Component() = default;
    OXYGEN_BASE_API virtual void UpdateDependencies([[maybe_unused]] const Composition& composition) { }

private:
    friend class Composition;
    static constexpr auto ClassDependencies() -> std::span<const oxygen::TypeId> { return {}; }
    [[nodiscard]] virtual auto HasDependencies() const -> bool { return false; }
    [[nodiscard]] virtual auto Dependencies() const -> std::span<const oxygen::TypeId> { return ClassDependencies(); }
};

//! Specifies the requirements on a type to be considered as a Component.
template <typename T>
concept IsComponent = std::is_base_of_v<Component, T>;

class Composition : public virtual Object {
    OXYGEN_TYPED(Composition)

    struct ComponentManager;
    // Implementation of the components storage, wrapped in a shared_ptr so we
    // can share it when shallow copying the composition.
    std::shared_ptr<ComponentManager> pimpl_;

    // Allow access to components_ from CloneableMixin to make a deep copy.
    template <typename T>
    friend class CloneableMixin;

public:
    OXYGEN_BASE_API ~Composition() noexcept override;

    //! Copy constructor, make a shallow copy of the composition.
    OXYGEN_BASE_API Composition(const Composition&);

    //! Copy assignment operator, make a shallow copy of the composition.
    OXYGEN_BASE_API Composition& operator=(const Composition&);

    //! Moves the composition to the new object and leaves the original in an empty state.
    OXYGEN_BASE_API Composition(Composition&& other) noexcept;

    //! Moves the composition to the new object and leaves the original in an empty state.
    OXYGEN_BASE_API Composition& operator=(Composition&& other) noexcept;

    template <typename T>
    [[nodiscard]] auto HasComponent() const -> bool
    {
        return HasComponentImpl(T::ClassTypeId());
    }

    template <typename T>
    [[nodiscard]] auto GetComponent() const -> T&
    {
        return static_cast<T&>(GetComponentImpl(T::ClassTypeId()));
    }

    template <typename ValueType>
    class OXYGEN_BASE_API Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = ValueType;
        using difference_type = std::ptrdiff_t;
        using pointer = ValueType*;
        using reference = ValueType&;

        Iterator() = default;

        reference operator*() const;
        pointer operator->() const;
        Iterator& operator++();
        Iterator operator++(int);
        bool operator==(const Iterator&) const;
        bool operator!=(const Iterator&) const;

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

    OXYGEN_BASE_API virtual auto begin() -> iterator;
    OXYGEN_BASE_API virtual auto end() -> iterator;
    OXYGEN_BASE_API virtual auto begin() const -> const_iterator;
    OXYGEN_BASE_API virtual auto end() const -> const_iterator;
    OXYGEN_BASE_API virtual auto cbegin() const -> const_iterator;
    OXYGEN_BASE_API virtual auto cend() const -> const_iterator;

    OXYGEN_BASE_API void PrintComponents(std::ostream& out) const;

protected:
    OXYGEN_BASE_API Composition();

    template <IsComponent T, typename... Args>
    T& AddComponent(Args&&... args)
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
        if (!T::ClassDependencies().empty()) {
            component->UpdateDependencies(*this);
        }
        return static_cast<T&>(AddComponentImpl(std::move(component)));
    }

    template <typename T>
    void RemoveComponent()
    {
        std::lock_guard lock(mutex_);

        auto id = T::ClassTypeId();
        if (!ExpectExistingComponent(id))
            return;
        if (IsComponentRequired(id))
            throw ComponentError("Cannot remove component; other components depend on it");
        RemoveComponentImpl(id);
    }

    template <typename OldT, typename NewT = OldT, typename... Args>
    NewT& ReplaceComponent(Args&&... args)
    {
        {
            std::lock_guard lock(mutex_);

            if (auto old_id = OldT::ClassTypeId(); ExpectExistingComponent(old_id)) {
                if (IsComponentRequired(old_id) && !std::is_same_v<OldT, NewT>) {
                    throw ComponentError("Cannot replace component with a different type; other components depend on it");
                }
                auto component = std::unique_ptr<NewT>(new NewT(std::forward<Args>(args)...));
                if (!NewT::ClassDependencies().empty()) {
                    component->UpdateDependencies(*this);
                }
                return static_cast<NewT&>(ReplaceComponentImpl(old_id, std::move(component)));
            }
        }

        return AddComponent<NewT>(std::forward<Args>(args)...);
    }

private:
    OXYGEN_BASE_API static void ValidateDependencies(TypeId comp_id, std::span<const TypeId> dependencies);
    OXYGEN_BASE_API void EnsureDependencies(std::span<const TypeId> dependencies) const;
    OXYGEN_BASE_API [[nodiscard]] auto ExpectExistingComponent(TypeId id) const -> bool;
    OXYGEN_BASE_API [[nodiscard]] auto IsComponentRequired(TypeId id) const -> bool;
    OXYGEN_BASE_API [[nodiscard]] auto HasComponentImpl(TypeId id) const -> bool;
    OXYGEN_BASE_API [[nodiscard]] auto AddComponentImpl(std::unique_ptr<Component> component) const -> Component&;
    OXYGEN_BASE_API [[nodiscard]] auto ReplaceComponentImpl(TypeId old_id, std::unique_ptr<Component> new_component) const -> Component&;
    OXYGEN_BASE_API [[nodiscard]] auto GetComponentImpl(TypeId id) const -> Component&;
    OXYGEN_BASE_API void RemoveComponentImpl(TypeId id, bool update_indices = true) const;
    OXYGEN_BASE_API void DeepCopyComponentsFrom(const Composition& other);

    mutable std::mutex mutex_ {};
};
static_assert(std::forward_iterator<Composition::Iterator<Component>>);
static_assert(std::ranges::common_range<Composition>);

template <typename T>
concept IsComposition = std::derived_from<T, Composition>;

template <typename Derived>
class CloneableMixin {
public:
    OXYGEN_DEFAULT_COPYABLE(CloneableMixin)
    OXYGEN_DEFAULT_MOVABLE(CloneableMixin)

protected:
    CloneableMixin() = default;
    ~CloneableMixin() = default;

public:
    [[nodiscard]] virtual auto Clone() const -> std::unique_ptr<Derived>
    {
        // We have to delay the type check until the derived class is fully defined.
        static_assert(IsComposition<Derived>, "Derived must satisfy the IsComposition concept");

        auto* original = static_cast<const Derived*>(this);
        // Make a shallow copy of the object
        auto clone = std::make_unique<Derived>(*original);
        // Make a deep copy of the components
        clone->DeepCopyComponentsFrom(*original);
        return clone;
    }
};

} // namespace oxygen

#include "./ComponentMacros.h"
