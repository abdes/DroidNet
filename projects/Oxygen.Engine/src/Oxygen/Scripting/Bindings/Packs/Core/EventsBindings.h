//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>

struct lua_State;

namespace oxygen::scripting::bindings {

struct EventDispatchStatus {
  bool ok { true };
  std::string message;
};

auto RegisterEventsBindings(lua_State* state, int oxygen_table_index) -> void;

auto QueueEngineEvent(lua_State* state, std::string_view event_name,
  std::string_view phase_name) -> void;
auto QueueEngineEventWithPayload(lua_State* state, std::string_view event_name,
  std::string_view phase_name, int payload_index) -> void;

auto SetActiveEventPhase(lua_State* state, std::string_view phase_name) -> void;
auto GetActiveEventPhase(lua_State* state) -> std::string_view;

auto DispatchEventsForPhase(lua_State* state, std::string_view phase_name)
  -> EventDispatchStatus;

auto ShutdownEventsRuntime(lua_State* state) -> void;

} // namespace oxygen::scripting::bindings
