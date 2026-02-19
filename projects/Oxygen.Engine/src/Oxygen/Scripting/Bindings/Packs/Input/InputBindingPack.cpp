//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>

#include <Oxygen/Scripting/Bindings/BindingRegistry.h>
#include <Oxygen/Scripting/Bindings/Packs/Input/InputBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Input/InputBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr std::array<BindingNamespace, 1> kInputNamespaces = { {
    { .name = "input", .register_fn = RegisterInputBindings },
  } };

  class InputBindingPack final : public contracts::IScriptBindingPack {
  public:
    [[nodiscard]] auto Name() const noexcept -> std::string_view override
    {
      return "oxygen.input";
    }

    [[nodiscard]] auto Register(
      const contracts::ScriptBindingPackContext& context) const -> bool override
    {
      return RegisterBindingNamespaces(
        context.lua_state, context.runtime_env_ref, kInputNamespaces);
    }
  };
} // namespace

auto CreateInputBindingPack() -> contracts::ScriptBindingPackPtr
{
  return std::make_shared<InputBindingPack>();
}

} // namespace oxygen::scripting::bindings
