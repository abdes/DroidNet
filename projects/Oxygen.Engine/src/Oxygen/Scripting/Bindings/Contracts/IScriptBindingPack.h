//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

struct lua_State;

namespace oxygen::scripting::bindings::contracts {

struct ScriptBindingPackContext {
  lua_State* lua_state { nullptr };
  int runtime_env_ref { -1 };
};

class IScriptBindingPack {
public:
  virtual ~IScriptBindingPack() = default;

  [[nodiscard]] virtual auto Name() const noexcept -> std::string_view = 0;

  [[nodiscard]] virtual auto Register(
    const ScriptBindingPackContext& context) const -> bool
    = 0;
};

using ScriptBindingPackPtr = std::shared_ptr<const IScriptBindingPack>;

} // namespace oxygen::scripting::bindings::contracts
