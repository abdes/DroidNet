//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>

struct lua_State;

namespace oxygen::scripting::bindings {

//! Zero denotes listeners registered by the global module environment.
using EventOwnerId = std::uint64_t;

struct EventListenerFailure {
  EventOwnerId owner { 0 };
  std::string message;
};

struct EventDispatchStatus {
  bool ok { true };
  std::string message;
  std::vector<EventListenerFailure> failures {};
};

//! Allocates a unique listener owner for one script-runtime incarnation.
//! Throws if owner identity space is exhausted; identities are never reused.
//! The liveness predicate must not retain the scene or references into slot
//! storage.
auto CreateEventListenerOwner(lua_State* state, std::function<bool()> is_live)
  -> EventOwnerId;

//! Retires a non-global owner, releasing listeners and rejecting new
//! registrations.
auto RetireEventListenerOwner(lua_State* state, EventOwnerId owner) -> void;

//! Attributes listener registration to the active script-runtime incarnation.
class ScopedEventListenerOwner final {
public:
  ScopedEventListenerOwner(lua_State* state, EventOwnerId owner);
  ~ScopedEventListenerOwner();

  OXYGEN_MAKE_NON_COPYABLE(ScopedEventListenerOwner)
  OXYGEN_MAKE_NON_MOVABLE(ScopedEventListenerOwner)

private:
  lua_State* state_;
  EventOwnerId previous_ { 0 };
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
