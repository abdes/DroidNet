//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Scene/Environment/EnvironmentSystem.h>

namespace oxygen::scene {

//! Concept for types that can be hosted as environment systems.
template <typename T>
concept EnvironmentSystemType
  = std::is_base_of_v<environment::EnvironmentSystem, T>;

//! Scene-global environment composition.
/*!
 `SceneEnvironment` is a standalone `oxygen::Composition` that hosts a variable
 set of environment systems (components) such as sky, fog, clouds, IBL, and post
 processing.

 Ownership is intended to be explicit: a `Scene` (later) will own a
 `std::unique_ptr<SceneEnvironment>`, and will expose only non-owning access.

 @warning This class is intentionally non-copyable. Copying environment state
          can be done by cloning/authoring at a higher level when required.
*/
class SceneEnvironment final : public Composition {
public:
  //! Non-owning view of an environment system stored in this environment.
  struct SystemEntry {
    TypeId type_id = kInvalidTypeId;
    observer_ptr<const environment::EnvironmentSystem> system = nullptr;

    auto operator==(const SystemEntry&) const -> bool = default;
  };

  //! Constructs an empty environment.
  SceneEnvironment() = default;

  //! Virtual destructor.
  ~SceneEnvironment() override = default;

  OXYGEN_MAKE_NON_COPYABLE(SceneEnvironment)
  OXYGEN_DEFAULT_MOVABLE(SceneEnvironment)

  //! Adds an environment system component of type `T`.
  /*!
   @tparam T Environment system component type.
   @tparam Args Constructor argument types.
   @param args Arguments forwarded to `T`'s constructor.
   @return Reference to the newly added system.
   @throw oxygen::ComponentError if a system of the same type already exists.
  */
  template <EnvironmentSystemType T, typename... Args>
  auto AddSystem(Args&&... args) -> T&
  {
    auto& system = AddComponent<T>(std::forward<Args>(args)...);
    systems_.push_back(SystemEntry {
      .type_id = T::ClassTypeId(),
      .system = observer_ptr<const environment::EnvironmentSystem>(&system),
    });
    return system;
  }

  //! Removes an environment system component of type `T` (if present).
  /*!
   @tparam T Environment system component type.
   @throw oxygen::ComponentError if the system is required by another one.
  */
  template <EnvironmentSystemType T> auto RemoveSystem() -> void
  {
    if (!HasComponent<T>()) {
      return;
    }

    RemoveComponent<T>();
    EraseSystemEntry(T::ClassTypeId());
  }

  //! Replaces an existing environment system component.
  /*!
   If `OldT` is not present, this throws.

   @tparam OldT Existing system type.
   @tparam NewT New system type (defaults to OldT).
   @tparam Args Constructor argument types.
   @param args Arguments forwarded to `NewT`'s constructor.
   @return Reference to the newly constructed `NewT`.
  */
  template <EnvironmentSystemType OldT, EnvironmentSystemType NewT = OldT,
    typename... Args>
  auto ReplaceSystem(Args&&... args) -> NewT&
  {
    auto& system = ReplaceComponent<OldT, NewT>(std::forward<Args>(args)...);

    UpsertSystemEntry(OldT::ClassTypeId(), NewT::ClassTypeId(), system);
    return system;
  }

  //! Returns the number of systems currently present.
  [[nodiscard]] auto GetSystemCount() const noexcept -> std::size_t
  {
    return systems_.size();
  }

  //! Returns a stable view over all systems.
  /*!
   The returned list is suitable for persistence and introspection.

   @return A span of SystemEntry records.

   @warning Entries contain non-owning pointers; they are only valid while the
            corresponding systems remain present in this environment.
  */
  [[nodiscard]] auto GetSystems() const noexcept
    -> std::span<const SystemEntry>
  {
    return systems_;
  }

  //! Returns true if a system of type `T` exists.
  template <EnvironmentSystemType T>
  [[nodiscard]] auto HasSystem() const -> bool
  {
    return HasComponent<T>();
  }

  //! Non-throwing access to a system of type `T`.
  template <EnvironmentSystemType T>
  [[nodiscard]] auto TryGetSystem() noexcept -> observer_ptr<T>
  {
    if (!HasComponent<T>()) {
      return nullptr;
    }
    return observer_ptr<T>(&GetComponent<T>());
  }

  //! @copydoc TryGetSystem
  template <EnvironmentSystemType T>
  [[nodiscard]] auto TryGetSystem() const noexcept -> observer_ptr<const T>
  {
    if (!HasComponent<T>()) {
      return nullptr;
    }
    return observer_ptr<const T>(&GetComponent<T>());
  }

private:
  template <typename SystemT>
  auto UpsertSystemEntry(
    const TypeId old_type, const TypeId new_type, const SystemT& system) -> void
  {
    auto it = FindSystemEntry(old_type);
    const auto entry = SystemEntry {
      .type_id = new_type,
      .system = observer_ptr<const environment::EnvironmentSystem>(&system),
    };

    if (it == systems_.end()) {
      systems_.push_back(entry);
      return;
    }
    *it = entry;
  }

  auto FindSystemEntry(const TypeId type_id) noexcept
    -> std::vector<SystemEntry>::iterator
  {
    return std::ranges::find_if(systems_,
      [&](const SystemEntry& entry) { return entry.type_id == type_id; });
  }

  auto FindSystemEntry(const TypeId type_id) const noexcept
    -> std::vector<SystemEntry>::const_iterator
  {
    return std::ranges::find_if(systems_,
      [&](const SystemEntry& entry) { return entry.type_id == type_id; });
  }

  auto EraseSystemEntry(const TypeId type_id) noexcept -> void
  {
    auto it = FindSystemEntry(type_id);
    if (it == systems_.end()) {
      return;
    }
    systems_.erase(it);
  }

  std::vector<SystemEntry> systems_;
};

} // namespace oxygen::scene
