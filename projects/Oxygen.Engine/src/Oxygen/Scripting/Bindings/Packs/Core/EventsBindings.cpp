//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr int kLuaArg1 = 1;
  constexpr int kLuaArg2 = 2;
  constexpr int kLuaArg3 = 3;

  [[maybe_unused]] constexpr int kLuaNoArgs = 0;
  constexpr int kLuaNoResults = 0;

  constexpr int kLuaNoRef = -1;

  constexpr const char* kEventsRuntimeFieldName = "__oxgn_events_runtime";
  constexpr const char* kEventsRuntimeMetatableName = "oxygen.events.runtime";
  constexpr const char* kLuaTracebackFnName = "EventsTraceback";
  constexpr const char* kEventConnectionIdFieldName = "__id";
  constexpr const char* kDefaultPhaseName = "gameplay";

  struct EventListener {
    std::uint64_t id { 0 };
    std::uint64_t sequence { 0 };
    // Event/Phase are implicitly handled by the bucket
    int priority { 0 };
    int callback_ref { kLuaNoRef };
    bool once { false };
    bool connected { true };
  };

  struct EventBucket {
    std::vector<EventListener> listeners;
  };

  struct ListenerLocation {
    std::string event_name;
    std::string phase_name;
  };

  struct QueuedEvent {
    std::string event_name;
    std::string phase_name;
    int payload_ref { kLuaNoRef };
  };

  struct EventStats {
    std::uint64_t fired { 0 };
    std::uint64_t errors { 0 };
    std::uint64_t dropped { 0 };
  };

  struct EventRuntime {
    std::uint64_t next_listener_id { 1 };
    std::uint64_t next_sequence { 1 };
    std::string current_phase { kDefaultPhaseName };

    // Primary Storage: EventName -> PhaseName -> Bucket (Sorted Listeners)
    std::unordered_map<std::string,
      std::unordered_map<std::string, EventBucket>>
      buckets;

    // Fast Lookup: ListenerID -> Location
    std::unordered_map<std::uint64_t, ListenerLocation> id_map;

    std::vector<QueuedEvent> queue;
    std::unordered_map<std::string, EventStats> stats_by_event;
  };

  auto IsValidLuaRef(const int ref) noexcept -> bool { return ref >= 0; }

  auto IsReservedEventName(std::string_view event_name) -> bool
  {
    constexpr auto kReservedPrefixes = std::to_array<std::string_view>({
      "app.",
      "frame.",
      "input.",
      "scene.",
      "render.",
      "physics.",
    });
    return std::ranges::any_of(
      kReservedPrefixes, [event_name](const std::string_view prefix) {
        return event_name.starts_with(prefix);
      });
  }

  auto LuaTraceback(lua_State* state) -> int
  {
    constexpr int kMessageIndex = 1;
    const auto* message = lua_tostring(state, kMessageIndex);
    luaL_traceback(state, state, message, kMessageIndex);
    return kMessageIndex;
  }

  auto LuaToString(lua_State* state, const int index) -> std::string
  {
    if (const auto* text = lua_tostring(state, index); text != nullptr) {
      return text;
    }
    return "unknown lua error";
  }

  auto FindRuntime(lua_State* state) -> EventRuntime*
  {
    if (state == nullptr) {
      return nullptr;
    }

    lua_getfield(state, LUA_REGISTRYINDEX, kEventsRuntimeFieldName);
    if (lua_userdatatag(state, -1) != kTagEventRuntime) {
      lua_pop(state, 1);
      return nullptr;
    }
    auto* runtime = static_cast<EventRuntime*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    return runtime;
  }

  void LuaEventsRuntimeDtor(lua_State* /*state*/, void* p)
  {
    static_cast<EventRuntime*>(p)->~EventRuntime();
  }

  auto EnsureRuntime(lua_State* state) -> EventRuntime*
  {
    if (state == nullptr) {
      return nullptr;
    }
    if (auto* runtime = FindRuntime(state); runtime != nullptr) {
      return runtime;
    }

    auto* runtime = static_cast<EventRuntime*>(
      lua_newuserdatatagged(state, sizeof(EventRuntime), kTagEventRuntime));
    lua_setuserdatadtor(state, kTagEventRuntime, LuaEventsRuntimeDtor);

    std::construct_at(runtime);

    (void)luaL_newmetatable(state, kEventsRuntimeMetatableName);
    lua_setmetatable(state, -2);
    lua_setfield(state, LUA_REGISTRYINDEX, kEventsRuntimeFieldName);
    return runtime;
  }

  auto RequireRuntime(lua_State* state) -> EventRuntime*
  {
    auto* runtime = EnsureRuntime(state);
    if (runtime == nullptr) {
      (void)luaL_error(state, "oxygen.events runtime is unavailable");
      return nullptr;
    }
    return runtime;
  }

  auto ParsePhaseName(lua_State* state, const int opts_index,
    const std::string_view fallback) -> std::string
  {
    if (!lua_istable(state, opts_index)) {
      return std::string(fallback);
    }

    lua_getfield(state, opts_index, "phase");
    const auto* phase = lua_tostring(state, -1);
    const std::string phase_name
      = phase == nullptr ? std::string(fallback) : std::string(phase);
    lua_pop(state, 1);
    return phase_name;
  }

  auto ParsePriority(lua_State* state, const int opts_index) -> int
  {
    if (!lua_istable(state, opts_index)) {
      return 0;
    }

    lua_getfield(state, opts_index, "priority");
    const int priority
      = lua_isnumber(state, -1) != 0 ? lua_tointeger(state, -1) : 0;
    lua_pop(state, 1);
    return priority;
  }

  auto FindListenerLocation(const EventRuntime& runtime,
    std::uint64_t listener_id) -> const ListenerLocation*
  {
    const auto it = runtime.id_map.find(listener_id);
    return it == runtime.id_map.end() ? nullptr : &it->second;
  }

  auto UnbindListenerById(
    lua_State* state, EventRuntime& runtime, std::uint64_t listener_id) -> void
  {
    const auto* loc = FindListenerLocation(runtime, listener_id);
    if (loc == nullptr) {
      return;
    }

    auto& phase_map = runtime.buckets[loc->event_name];
    auto& bucket = phase_map[loc->phase_name];

    for (auto& listener : bucket.listeners) {
      if (listener.id == listener_id) {
        if (listener.connected) {
          listener.connected = false;
          if (IsValidLuaRef(listener.callback_ref)) {
            lua_unref(state, listener.callback_ref);
            listener.callback_ref = kLuaNoRef;
          }
        }
        break;
      }
    }

    runtime.id_map.erase(listener_id);
  }

  auto CompactBucket(EventBucket& bucket) -> void
  {
    std::erase_if(bucket.listeners,
      [](const EventListener& listener) { return !listener.connected; });
  }

  auto LuaConnectionDisconnect(lua_State* state) -> int
  {
    if (!lua_istable(state, kLuaArg1)) {
      return 0;
    }

    lua_getfield(state, kLuaArg1, kEventConnectionIdFieldName);
    const auto listener_id
      = static_cast<std::uint64_t>(lua_tointeger(state, -1));
    lua_pop(state, 1);

    auto* runtime = FindRuntime(state);
    if (runtime == nullptr) {
      return 0;
    }

    UnbindListenerById(state, *runtime, listener_id);
    return 0;
  }

  auto LuaConnectionConnected(lua_State* state) -> int
  {
    if (!lua_istable(state, kLuaArg1)) {
      lua_pushboolean(state, 0);
      return 1;
    }

    lua_getfield(state, kLuaArg1, kEventConnectionIdFieldName);
    const auto listener_id
      = static_cast<std::uint64_t>(lua_tointeger(state, -1));
    lua_pop(state, 1);

    const auto* runtime = FindRuntime(state);
    bool is_connected = false;
    if (runtime != nullptr) {
      is_connected = runtime->id_map.contains(listener_id);
    }
    lua_pushboolean(state, is_connected ? 1 : 0);
    return 1;
  }

  auto PushConnectionObject(lua_State* state, std::uint64_t listener_id) -> int
  {
    lua_newtable(state);
    lua_pushinteger(state, static_cast<lua_Integer>(listener_id));
    lua_setfield(state, -2, kEventConnectionIdFieldName);

    lua_pushcfunction(
      state, LuaConnectionDisconnect, "events.connection.disconnect");
    lua_setfield(state, -2, "disconnect");

    lua_pushcfunction(
      state, LuaConnectionConnected, "events.connection.connected");
    lua_setfield(state, -2, "connected");
    return 1;
  }

  auto QueueEvent(EventRuntime& runtime, const std::string_view event_name,
    std::string phase_name, const int payload_ref) -> int
  {
    runtime.queue.push_back(QueuedEvent {
      .event_name = std::string(event_name),
      .phase_name = std::move(phase_name),
      .payload_ref = payload_ref,
    });
    return 0;
  }

  auto InsertListenerSorted(std::vector<EventListener>& listeners,
    const EventListener& new_listener) -> void
  {
    const auto compare = [](const EventListener& a, const EventListener& b) {
      if (a.priority != b.priority) {
        return a.priority > b.priority;
      }
      return a.sequence < b.sequence;
    };

    auto it = std::upper_bound(
      listeners.begin(), listeners.end(), new_listener, compare);
    listeners.insert(it, new_listener);
  }

  auto LuaEventsOnImpl(lua_State* state, const bool once) -> int
  {
    const auto* event_name = lua_tostring(state, kLuaArg1);
    const std::string_view event_name_sv = event_name == nullptr
      ? std::string_view {}
      : std::string_view(event_name);
    if (event_name_sv.empty()) {
      (void)luaL_error(
        state, "oxygen.events.on expects a non-empty event name");
      return 0;
    }
    if (lua_isfunction(state, kLuaArg2) == 0) {
      (void)luaL_error(
        state, "oxygen.events.on expects callback function as arg #2");
      return 0;
    }

    auto* runtime = RequireRuntime(state);
    if (runtime == nullptr) {
      return 0;
    }
    const std::string phase_name
      = ParsePhaseName(state, kLuaArg3, runtime->current_phase);
    const int priority = ParsePriority(state, kLuaArg3);

    lua_pushvalue(state, kLuaArg2);
    const int callback_ref = lua_ref(state, -1);

    const std::uint64_t listener_id = runtime->next_listener_id++;
    const std::uint64_t seq = runtime->next_sequence++;

    EventListener listener {
      .id = listener_id,
      .sequence = seq,
      .priority = priority,
      .callback_ref = callback_ref,
      .once = once,
      .connected = true,
    };

    auto& bucket = runtime->buckets[std::string(event_name_sv)][phase_name];
    InsertListenerSorted(bucket.listeners, listener);

    runtime->id_map[listener_id]
      = ListenerLocation { .event_name = std::string(event_name_sv),
          .phase_name = phase_name };

    return PushConnectionObject(state, listener_id);
  }

  auto LuaEventsOn(lua_State* state) -> int
  {
    return LuaEventsOnImpl(state, false);
  }

  auto LuaEventsOnce(lua_State* state) -> int
  {
    return LuaEventsOnImpl(state, true);
  }

  auto LuaEventsEmit(lua_State* state) -> int
  {
    const auto* event_name = lua_tostring(state, kLuaArg1);
    const std::string_view event_name_sv = event_name == nullptr
      ? std::string_view {}
      : std::string_view(event_name);
    if (event_name_sv.empty()) {
      (void)luaL_error(
        state, "oxygen.events.emit expects a non-empty event name");
      return 0;
    }
    if (IsReservedEventName(event_name_sv)) {
      (void)luaL_error(state,
        "oxygen.events.emit cannot publish reserved engine event '%s'",
        event_name);
      return 0;
    }

    auto* runtime = RequireRuntime(state);
    if (runtime == nullptr) {
      return 0;
    }
    std::string phase_name = runtime->current_phase;
    if (phase_name.empty()) {
      phase_name = kDefaultPhaseName;
    }
    if (lua_istable(state, kLuaArg3)) {
      phase_name = ParsePhaseName(state, kLuaArg3, phase_name);
    }

    int payload_ref = kLuaNoRef;
    if (!lua_isnoneornil(state, kLuaArg2)) {
      lua_pushvalue(state, kLuaArg2);
      payload_ref = lua_ref(state, -1);
    }

    return QueueEvent(
      *runtime, event_name_sv, std::move(phase_name), payload_ref);
  }

  auto LuaEventsListenerCount(lua_State* state) -> int
  {
    const auto* event_name = lua_tostring(state, kLuaArg1);
    const std::string_view event_name_sv = event_name == nullptr
      ? std::string_view {}
      : std::string_view(event_name);
    if (event_name_sv.empty()) {
      (void)luaL_error(
        state, "oxygen.events.listener_count expects a non-empty event name");
      return 0;
    }

    auto* runtime = RequireRuntime(state);
    if (runtime == nullptr) {
      return 0;
    }

    lua_Integer count = 0;
    const auto ev_it = runtime->buckets.find(std::string(event_name_sv));
    if (ev_it != runtime->buckets.end()) {
      for (const auto& [phase, bucket] : ev_it->second) {
        count
          += static_cast<lua_Integer>(std::ranges::count_if(bucket.listeners,
            [](const EventListener& l) { return l.connected; }));
      }
    }

    lua_pushinteger(state, count);
    return 1;
  }

  auto LuaEventsStats(lua_State* state) -> int
  {
    const auto* event_name = lua_tostring(state, kLuaArg1);
    const std::string_view event_name_sv = event_name == nullptr
      ? std::string_view {}
      : std::string_view(event_name);
    if (event_name_sv.empty()) {
      (void)luaL_error(
        state, "oxygen.events.stats expects a non-empty event name");
      return 0;
    }

    auto* runtime = RequireRuntime(state);
    if (runtime == nullptr) {
      return 0;
    }
    const auto it = runtime->stats_by_event.find(std::string(event_name_sv));
    const EventStats stats
      = it == runtime->stats_by_event.end() ? EventStats {} : it->second;

    lua_Integer listeners = 0;
    const auto ev_it = runtime->buckets.find(std::string(event_name_sv));
    if (ev_it != runtime->buckets.end()) {
      for (const auto& [phase, bucket] : ev_it->second) {
        listeners
          += static_cast<lua_Integer>(std::ranges::count_if(bucket.listeners,
            [](const EventListener& l) { return l.connected; }));
      }
    }

    lua_newtable(state);
    lua_pushinteger(state, static_cast<lua_Integer>(stats.fired));
    lua_setfield(state, -2, "fired");
    lua_pushinteger(state, listeners);
    lua_setfield(state, -2, "listeners");
    lua_pushinteger(state, static_cast<lua_Integer>(stats.errors));
    lua_setfield(state, -2, "errors");
    lua_pushinteger(state, static_cast<lua_Integer>(stats.dropped));
    lua_setfield(state, -2, "dropped");
    return 1;
  }

  auto InvokeListener(lua_State* state, const EventListener& listener,
    const int payload_ref, std::string& out_error) -> bool
  {
    lua_getref(state, listener.callback_ref);
    if (!lua_isfunction(state, -1)) {
      lua_pop(state, 1);
      out_error = "event callback is not callable";
      return false;
    }

    // Push traceback before payload
    lua_pushcfunction(state, LuaTraceback, kLuaTracebackFnName);
    lua_insert(state, -2); // [ traceback, fn ]
    const int traceback_index = lua_gettop(state) - 1;

    if (IsValidLuaRef(payload_ref)) {
      lua_getref(state, payload_ref);
    } else {
      lua_pushnil(state);
    }

    const auto status = lua_pcall(state, 1, kLuaNoResults, traceback_index);
    if (status != LUA_OK) {
      out_error = LuaToString(state, -1);
      lua_pop(state, 2); // error + traceback
      return false;
    }

    lua_remove(state, traceback_index); // traceback
    return true;
  }
} // namespace

auto RegisterEventsBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
  const int module_index
    = PushOxygenSubtable(state, oxygen_table_index, "events");

  lua_pushcfunction(state, LuaEventsOn, "events.on");
  lua_setfield(state, module_index, "on");

  lua_pushcfunction(state, LuaEventsOnce, "events.once");
  lua_setfield(state, module_index, "once");

  lua_pushcfunction(state, LuaEventsEmit, "events.emit");
  lua_setfield(state, module_index, "emit");

  lua_pushcfunction(state, LuaEventsListenerCount, "events.listener_count");
  lua_setfield(state, module_index, "listener_count");

  lua_pushcfunction(state, LuaEventsStats, "events.stats");
  lua_setfield(state, module_index, "stats");

  lua_pop(state, 1);
}

auto QueueEngineEvent(lua_State* state, const std::string_view event_name,
  std::string_view phase_name) -> void
{
  auto* runtime = EnsureRuntime(state);
  if (runtime == nullptr) {
    return;
  }
  runtime->queue.push_back(QueuedEvent {
    .event_name = std::string(event_name),
    .phase_name = std::string(phase_name),
    .payload_ref = kLuaNoRef,
  });
}

auto QueueEngineEventWithPayload(lua_State* state,
  const std::string_view event_name, const std::string_view phase_name,
  const int payload_index) -> void
{
  auto* runtime = EnsureRuntime(state);
  if (runtime == nullptr) {
    return;
  }

  int payload_ref = kLuaNoRef;
  if (payload_index != 0) {
    const int abs_payload_index = lua_absindex(state, payload_index);
    lua_pushvalue(state, abs_payload_index);
    payload_ref = lua_ref(state, -1);
  }

  runtime->queue.push_back(QueuedEvent {
    .event_name = std::string(event_name),
    .phase_name = std::string(phase_name),
    .payload_ref = payload_ref,
  });
}

auto SetActiveEventPhase(lua_State* state, const std::string_view phase_name)
  -> void
{
  auto* runtime = EnsureRuntime(state);
  if (runtime == nullptr) {
    return;
  }
  runtime->current_phase = std::string(phase_name);
}

auto GetActiveEventPhase(lua_State* state) -> std::string_view
{
  auto* runtime = EnsureRuntime(state);
  if (runtime == nullptr) {
    return {};
  }
  return runtime->current_phase;
}

auto DispatchEventsForPhase(lua_State* state, const std::string_view phase_name)
  -> EventDispatchStatus
{
  auto* runtime = EnsureRuntime(state);
  if (runtime == nullptr) {
    return EventDispatchStatus {
      .ok = false,
      .message = "events runtime is unavailable",
    };
  }

  runtime->current_phase = std::string(phase_name);

  // Extract dispatch list locally
  const size_t initial_queue_size = runtime->queue.size();
  std::vector<size_t> dispatch_indices;
  dispatch_indices.reserve(initial_queue_size);

  for (size_t i = 0; i < initial_queue_size; ++i) {
    if (runtime->queue[i].phase_name == phase_name) {
      dispatch_indices.push_back(i);
    }
  }

  EventDispatchStatus status {};
  for (const size_t queue_index : dispatch_indices) {
    auto& queued = runtime->queue[queue_index];
    auto& stats = runtime->stats_by_event[queued.event_name];

    auto& phase_map = runtime->buckets[queued.event_name];
    auto& bucket = phase_map[std::string(phase_name)];

    size_t count = bucket.listeners.size();
    for (size_t i = 0; i < count; ++i) {
      auto& listener = bucket.listeners[i];
      if (!listener.connected) {
        continue;
      }

      std::string callback_error;
      if (!InvokeListener(
            state, listener, queued.payload_ref, callback_error)) {
        ++stats.errors;
        if (status.ok) {
          status.ok = false;
          status.message = std::string("event '")
                             .append(queued.event_name)
                             .append("' listener #")
                             .append(std::to_string(listener.id))
                             .append(" failed: ")
                             .append(callback_error);
        }
      } else {
        ++stats.fired;
      }

      if (listener.once) {
        UnbindListenerById(state, *runtime, listener.id);
      }
    }

    if (IsValidLuaRef(queued.payload_ref)) {
      lua_unref(state, queued.payload_ref);
      queued.payload_ref = kLuaNoRef;
    }

    CompactBucket(bucket);
  }

  std::erase_if(runtime->queue, [phase_name](const QueuedEvent& event) {
    return event.phase_name == phase_name;
  });

  return status;
}

auto ShutdownEventsRuntime(lua_State* state) -> void
{
  if (state == nullptr) {
    return;
  }

  lua_getfield(state, LUA_REGISTRYINDEX, kEventsRuntimeFieldName);
  auto* runtime = static_cast<EventRuntime*>(lua_touserdata(state, -1));
  if (runtime == nullptr) {
    lua_pop(state, 1);
    return;
  }

  for (auto& [event_name_ignored, phase_map] : runtime->buckets) {
    (void)event_name_ignored;
    for (auto& [phase_name_ignored, bucket] : phase_map) {
      (void)phase_name_ignored;
      for (auto& listener : bucket.listeners) {
        if (IsValidLuaRef(listener.callback_ref)) {
          lua_unref(state, listener.callback_ref);
          listener.callback_ref = kLuaNoRef;
        }
      }
    }
  }

  for (auto& queued : runtime->queue) {
    if (IsValidLuaRef(queued.payload_ref)) {
      lua_unref(state, queued.payload_ref);
      queued.payload_ref = kLuaNoRef;
    }
  }

  lua_pushnil(state);
  lua_setfield(state, LUA_REGISTRYINDEX, kEventsRuntimeFieldName);

  lua_pushnil(state);
  lua_setmetatable(state, -2);

  // Runtime userdata is tagged with a Luau destructor (LuaEventsRuntimeDtor).
  // Do not manually destroy here; lua_close/GC will invoke the destructor.
  lua_pop(state, 1);
}

} // namespace oxygen::scripting::bindings
