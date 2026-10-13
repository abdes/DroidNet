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
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr auto kContentNamespaces = std::to_array<BindingNamespace>({
    {
      .name = "assets",
      .register_fn = RegisterContentBindings,
      .describe = DescribeContentBindings,
    },
  });

  class ContentBindingPack final : public contracts::IScriptBindingPack {
  public:
    [[nodiscard]] auto Name() const noexcept -> std::string_view override
    {
      return "oxygen.assets";
    }

    [[nodiscard]] auto Register(
      const contracts::ScriptBindingPackContext& context) const -> bool override
    {
      return RegisterBindingNamespaces(
        context.lua_state, context.runtime_env_ref, kContentNamespaces);
    }

    [[nodiscard]] auto Namespaces() const
      -> std::span<const BindingNamespace> override
    {
      return kContentNamespaces;
    }
  };
} // namespace

auto CreateContentBindingPack() -> contracts::ScriptBindingPackPtr
{
  return std::make_shared<ContentBindingPack>();
}

} // namespace oxygen::scripting::bindings
