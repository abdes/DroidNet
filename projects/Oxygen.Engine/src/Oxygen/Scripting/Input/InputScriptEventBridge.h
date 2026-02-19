//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>

struct lua_State;

namespace oxygen::engine {
class FrameContext;
}

namespace oxygen::scripting::input {

class InputScriptEventBridge final {
public:
  auto QueueActionEdgeEvents(lua_State* state,
    observer_ptr<engine::FrameContext> context, bool logging_enabled,
    int log_verbosity) -> void;

private:
  auto LogActionInventoryOnce(const std::vector<std::string_view>& action_names,
    int log_verbosity) -> void;

  bool logged_action_inventory_ { false };
};

} // namespace oxygen::scripting::input
