//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <memory>
#include <unordered_map>

#include <Commands/IComponentPropertyApplier.h>
#include <Commands/PropertyKeys.h>

namespace oxygen::interop::module {

  //! Process-wide registry of component appliers (property pipeline §5.3).
  /*!
   The registry is the single point where SetPropertiesCommand looks up
   how to mutate a component class. Population happens once per process
   via Bootstrap(); the registry is otherwise read-only.

   Lifetime: the registry is a Meyers singleton owned by the editor
   interop module. Appliers are stateless functor-tables, so the
   singleton is safe to share across all SetPropertiesCommand
   invocations.
  */
  class PropertyApplierRegistry {
  public:
    PropertyApplierRegistry(const PropertyApplierRegistry&) = delete;
    auto operator=(const PropertyApplierRegistry&)
      -> PropertyApplierRegistry& = delete;
    PropertyApplierRegistry(PropertyApplierRegistry&&) = delete;
    auto operator=(PropertyApplierRegistry&&)
      -> PropertyApplierRegistry& = delete;

    [[nodiscard]] static auto Instance() -> PropertyApplierRegistry&;

    //! Registers all built-in appliers (Transform, …). Idempotent.
    static void Bootstrap();

    //! Adds an applier; later registrations for the same component id
    //! replace earlier ones.
    void Register(std::unique_ptr<IComponentPropertyApplier> applier);

    //! Returns the applier for `id`, or nullptr if none is registered.
    [[nodiscard]] auto Find(ComponentId id) const noexcept
      -> IComponentPropertyApplier*;

  private:
    PropertyApplierRegistry() = default;
    ~PropertyApplierRegistry() = default;

    std::unordered_map<ComponentId,
      std::unique_ptr<IComponentPropertyApplier>>
      appliers_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
