//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Input/InputSnapshot.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
#include <Oxygen/Scripting/Input/InputScriptEventBridge.h>

namespace oxygen::scripting::input {

namespace {
  auto BuildEventName(
    std::string_view action_name, std::string_view edge_suffix) -> std::string
  {
    std::string event_name("input.action.");
    event_name.append(action_name);
    event_name.push_back('.');
    event_name.append(edge_suffix);
    return event_name;
  }

  auto QueueEdgeEventNoLog(lua_State* state, std::string_view action_name,
    std::string_view edge_suffix) -> void
  {
    bindings::QueueEngineEvent(
      state, BuildEventName(action_name, edge_suffix), "gameplay");
  }

  auto QueueEdgeEvent(lua_State* state, const frame::SequenceNumber frame_seq,
    std::string_view action_name, std::string_view edge_suffix,
    std::string_view edge_label, const int log_verbosity) -> void
  {
    const auto event_name = BuildEventName(action_name, edge_suffix);

    VLOG_F(log_verbosity,
      "ScriptingModule: action edge detected "
      "(frame={} action='{}' edge='{}') -> queue event '{}'",
      frame_seq, action_name, edge_label, event_name);
    bindings::QueueEngineEvent(state, event_name, "gameplay");
  }
} // namespace

auto InputScriptEventBridge::QueueActionEdgeEvents(lua_State* state,
  const observer_ptr<engine::FrameContext> context, const bool logging_enabled,
  const int log_verbosity) -> void
{
  if (state == nullptr || context == nullptr) {
    return;
  }

  const auto input_blob = context->GetInputSnapshot();
  if (!input_blob) {
    return;
  }

  const auto snapshot
    = std::static_pointer_cast<const oxygen::input::InputSnapshot>(input_blob);
  if (!snapshot) {
    return;
  }

  const auto action_names = snapshot->GetActionNames();
  const bool should_log = logging_enabled;
  if (should_log) {
    LogActionInventoryOnce(action_names, log_verbosity);
  }
  const auto frame_seq = context->GetFrameSequenceNumber();

  for (const auto action_name : action_names) {
    if (snapshot->DidActionTrigger(action_name)) {
      if (should_log) {
        QueueEdgeEvent(state, frame_seq, action_name, "triggered", "triggered",
          log_verbosity);
      } else {
        QueueEdgeEventNoLog(state, action_name, "triggered");
      }
    }
    if (snapshot->DidActionComplete(action_name)) {
      if (should_log) {
        QueueEdgeEvent(state, frame_seq, action_name, "completed", "completed",
          log_verbosity);
      } else {
        QueueEdgeEventNoLog(state, action_name, "completed");
      }
    }
    if (snapshot->DidActionCancel(action_name)) {
      if (should_log) {
        QueueEdgeEvent(
          state, frame_seq, action_name, "canceled", "canceled", log_verbosity);
      } else {
        QueueEdgeEventNoLog(state, action_name, "canceled");
      }
    }
    if (snapshot->DidActionRelease(action_name)) {
      if (should_log) {
        QueueEdgeEvent(
          state, frame_seq, action_name, "released", "released", log_verbosity);
      } else {
        QueueEdgeEventNoLog(state, action_name, "released");
      }
    }
    if (snapshot->DidActionValueUpdate(action_name)) {
      if (should_log) {
        QueueEdgeEvent(state, frame_seq, action_name, "value_updated",
          "value_updated", log_verbosity);
      } else {
        QueueEdgeEventNoLog(state, action_name, "value_updated");
      }
    }
  }
}

auto InputScriptEventBridge::LogActionInventoryOnce(
  const std::vector<std::string_view>& action_names, const int log_verbosity)
  -> void
{
  if (logged_action_inventory_) {
    return;
  }
  logged_action_inventory_ = true;

  std::string joined;
  for (size_t i = 0; i < action_names.size(); ++i) {
    if (i > 0) {
      joined.append(", ");
    }
    joined.append(action_names[i]);
  }

  VLOG_F(log_verbosity,
    "ScriptingModule: input snapshot actions discovered (count={}): {}",
    action_names.size(), joined);
}

} // namespace oxygen::scripting::input
