//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>
#include <span>
#include <string_view>

#include <Oxygen/Scripting/Bindings/BindingRegistry.h>
#include <Oxygen/Scripting/Bindings/Contracts/BindingDescription.h>
#include <Oxygen/Scripting/Bindings/Contracts/IScriptBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr auto kSceneNamespaces = std::to_array<BindingNamespace>({
    {
      .name = "scene",
      .register_fn = RegisterSceneBindings,
      .describe = DescribeSceneBindings,
    },
  });

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

    [[nodiscard]] auto Namespaces() const
      -> std::span<const BindingNamespace> override
    {
      return kSceneNamespaces;
    }
  };
} // namespace

auto CreateSceneBindingPack() -> contracts::ScriptBindingPackPtr
{
  return std::make_shared<SceneBindingPack>();
}

} // namespace oxygen::scripting::bindings
