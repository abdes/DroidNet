//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>

#include <Oxygen/Scripting/Bindings/BindingRegistry.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr std::array<BindingNamespace, 1> kSceneNamespaces
    = { { { .name = "scene", .register_fn = RegisterSceneBindings } } };

  class SceneBindingPack final : public contracts::IScriptBindingPack {
  public:
    [[nodiscard]] auto Name() const noexcept -> std::string_view override
    {
      return "oxygen.scene";
    }

    [[nodiscard]] auto Register(
      const contracts::ScriptBindingPackContext& context) const -> bool override
    {
      return RegisterBindingNamespaces(
        context.lua_state, context.runtime_env_ref, kSceneNamespaces);
    }
  };
} // namespace

auto CreateSceneBindingPack() -> contracts::ScriptBindingPackPtr
{
  return std::make_shared<SceneBindingPack>();
}

} // namespace oxygen::scripting::bindings
