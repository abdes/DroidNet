//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>

#include <Oxygen/Scripting/Bindings/BindingRegistry.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsAggregateBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsArticulationBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsBodyBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsCharacterBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsConstantsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsEventsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsQueryBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsSoftBodyBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsVehicleBindings.h>

namespace oxygen::scripting::bindings {

namespace {

  auto RegisterPhysicsBindings(lua_State* state, const int oxygen_table_index)
    -> void
  {
    RegisterBodyBindings(state, oxygen_table_index);
    RegisterCharacterBindings(state, oxygen_table_index);
    RegisterQueryBindings(state, oxygen_table_index);
    RegisterPhysicsEventsBindings(state, oxygen_table_index);
    RegisterPhysicsConstantsBindings(state, oxygen_table_index);
    RegisterPhysicsAggregateBindings(state, oxygen_table_index);
    RegisterPhysicsArticulationBindings(state, oxygen_table_index);
    RegisterPhysicsVehicleBindings(state, oxygen_table_index);
    RegisterPhysicsSoftBodyBindings(state, oxygen_table_index);
  }

  constexpr std::array<BindingNamespace, 1> kPhysicsNamespaces = { {
    { .name = "physics", .register_fn = RegisterPhysicsBindings },
  } };

  class PhysicsBindingPack final : public contracts::IScriptBindingPack {
  public:
    [[nodiscard]] auto Name() const noexcept -> std::string_view override
    {
      return "oxygen.physics";
    }

    [[nodiscard]] auto Register(
      const contracts::ScriptBindingPackContext& context) const -> bool override
    {
      return RegisterBindingNamespaces(
        context.lua_state, context.runtime_env_ref, kPhysicsNamespaces);
    }
  };

} // namespace

auto CreatePhysicsBindingPack() -> contracts::ScriptBindingPackPtr
{
  return std::make_shared<PhysicsBindingPack>();
}

} // namespace oxygen::scripting::bindings
