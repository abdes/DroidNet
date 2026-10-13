//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>
#include <string_view>

#include <Oxygen/Scripting/Bindings/Contracts/BindingDescription.h>

struct lua_State;

namespace oxygen::scripting::bindings::contracts {

struct ScriptBindingPackContext {
  lua_State* lua_state { nullptr };
  int runtime_env_ref { -1 };
};

class IScriptBindingPack {
public:
  IScriptBindingPack() = default;
  virtual ~IScriptBindingPack() = default;
  IScriptBindingPack(const IScriptBindingPack&) = delete;
  auto operator=(const IScriptBindingPack&) -> IScriptBindingPack& = delete;
  IScriptBindingPack(IScriptBindingPack&&) = delete;
  auto operator=(IScriptBindingPack&&) -> IScriptBindingPack& = delete;

  [[nodiscard]] virtual auto Name() const noexcept -> std::string_view = 0;

  [[nodiscard]] virtual auto Register(
    const ScriptBindingPackContext& context) const -> bool = 0;

  //! The namespaces the pack registers, with their descriptions; the
  //! generated Luau definitions are built from them. A pack that returns none
  //! adds nothing to the definitions.
  [[nodiscard]] virtual auto Namespaces() const
    -> std::span<const BindingNamespace>
  {
    return {};
  }
};

using ScriptBindingPackPtr = std::shared_ptr<const IScriptBindingPack>;

} // namespace oxygen::scripting::bindings::contracts
