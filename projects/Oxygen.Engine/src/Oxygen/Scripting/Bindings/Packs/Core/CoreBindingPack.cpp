//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>

#include <Oxygen/Scripting/Bindings/BindingRegistry.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/CoreBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/TimeBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr std::array<BindingNamespace, 1> kCoreNamespaces = { {
    { .name = "time", .register_fn = RegisterTimeBindings },
  } };

  class CoreBindingPack final : public contracts::IScriptBindingPack {
  public:
    [[nodiscard]] auto Name() const noexcept -> std::string_view override
    {
      return "oxygen.core";
    }

    [[nodiscard]] auto Register(
      const contracts::ScriptBindingPackContext& context) const -> bool override
    {
      return RegisterBindingNamespaces(
        context.lua_state, context.runtime_env_ref, kCoreNamespaces);
    }
  };
} // namespace

auto CreateCoreBindingPack() -> contracts::ScriptBindingPackPtr
{
  return std::make_shared<CoreBindingPack>();
}

} // namespace oxygen::scripting::bindings
