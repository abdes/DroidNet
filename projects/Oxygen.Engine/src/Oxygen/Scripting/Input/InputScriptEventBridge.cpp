//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Input/InputSnapshot.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
#include <Oxygen/Scripting/Input/InputScriptEventBridge.h>

namespace oxygen::scripting::input {

namespace {
  //! Logging settings for one frame's edge events.
  struct EdgeEventLog {
    bool enabled { false };
    int verbosity { 0 };
    frame::SequenceNumber frame_seq;
  };

  //! Queue `input.action.<key>.<edge>` for scripts in the gameplay phase.
  auto QueueEdgeEvent(lua_State* state, const EdgeEventLog& log,
    const std::string_view action_key, const std::string_view edge) -> void
  {
    std::string event_name("input.action.");
    event_name.append(action_key);
    event_name.push_back('.');
    event_name.append(edge);

    if (log.enabled) {
      VLOG_F(log.verbosity,
        "ScriptingModule: action edge detected "
        "(frame={} action='{}' edge='{}') -> queue event '{}'",
        log.frame_seq, action_key, edge, event_name);
    }
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

  // Scripts name actions by asset reference, so only actions hydrated from
  // assets raise events, one stream per asset key.
  const auto action_keys = snapshot->GetActionKeys();
  if (logging_enabled) {
    LogActionInventoryOnce(action_keys, log_verbosity);
  }
  const auto log = EdgeEventLog {
    .enabled = logging_enabled,
    .verbosity = log_verbosity,
    .frame_seq = context->GetFrameSequenceNumber(),
  };

  for (const auto& action_key : action_keys) {
    const auto key_text = data::to_string(action_key);
    if (snapshot->DidActionTrigger(action_key)) {
      QueueEdgeEvent(state, log, key_text, "triggered");
    }
    if (snapshot->DidActionComplete(action_key)) {
      QueueEdgeEvent(state, log, key_text, "completed");
    }
    if (snapshot->DidActionCancel(action_key)) {
      QueueEdgeEvent(state, log, key_text, "canceled");
    }
    if (snapshot->DidActionRelease(action_key)) {
      QueueEdgeEvent(state, log, key_text, "released");
    }
    if (snapshot->DidActionValueUpdate(action_key)) {
      QueueEdgeEvent(state, log, key_text, "value_updated");
    }
  }
}

auto InputScriptEventBridge::LogActionInventoryOnce(
  const std::vector<data::AssetKey>& action_keys, const int log_verbosity)
  -> void
{
  if (logged_action_inventory_) {
    return;
  }
  logged_action_inventory_ = true;

  std::string joined;
  for (const auto& key : action_keys) {
    if (!joined.empty()) {
      joined.append(", ");
    }
    joined.append(data::to_string(key));
  }

  VLOG_F(log_verbosity,
    "ScriptingModule: input snapshot asset actions discovered (count={}): {}",
    action_keys.size(), joined);
}

} // namespace oxygen::scripting::input
