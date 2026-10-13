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
#include <Oxygen/Scripting/Bindings/Packs/Core/AppBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/ConventionsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/CoreBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/IdsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/LogBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/MathBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/ScriptDeclarationBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/TimeBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr std::array<BindingNamespace, 9> kCoreNamespaces = {
    {
      {
        .name = "app",
        .register_fn = RegisterAppBindings,
        .describe = DescribeAppBindings,
      },
      {
        .name = "conventions",
        .register_fn = RegisterConventionsBindings,
        .describe = DescribeConventionsBindings,
      },
      {
        .name = "events",
        .register_fn = RegisterEventsBindings,
        .describe = DescribeEventsBindings,
      },
      {
        .name = "hash",
        .register_fn = RegisterHashBindings,
        .describe = DescribeHashBindings,
      },
      {
        .name = "log",
        .register_fn = RegisterLogBindings,
        .describe = DescribeLogBindings,
      },
      {
        .name = "math",
        .register_fn = RegisterMathBindings,
        .describe = DescribeMathBindings,
      },
      {
        .name = "script",
        .register_fn = RegisterScriptDeclarationBindings,
        .describe = DescribeScriptDeclarationBindings,
      },
      {
        .name = "time",
        .register_fn = RegisterTimeBindings,
        .describe = DescribeTimeBindings,
      },
      {
        .name = "uuid",
        .register_fn = RegisterUuidBindings,
        .describe = DescribeUuidBindings,
      },
    },
  };

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

    [[nodiscard]] auto Namespaces() const
      -> std::span<const BindingNamespace> override
    {
      return kCoreNamespaces;
    }
  };
} // namespace

auto CreateCoreBindingPack() -> contracts::ScriptBindingPackPtr
{
  return std::make_shared<CoreBindingPack>();
}

} // namespace oxygen::scripting::bindings
