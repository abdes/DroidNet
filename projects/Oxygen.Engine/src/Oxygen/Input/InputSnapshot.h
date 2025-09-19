//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionState.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/api_export.h>

namespace oxygen::platform {
class InputEvent; // forward decl kept for compatibility if needed elsewhere
} // namespace oxygen::platform

namespace oxygen::input {

//! Consolidated input snapshot capturing all frame activity
/*!
 The InputSnapshot provides a read-only view of input state for a single frame,
 including action states and intra-frame transitions. It is constructed and
 frozen at the end of the kInput phase and consumed by subsequent phases
 (FixedSim, Gameplay, etc.) in the same frame, and later published at kSnapshot
 without being rebuilt.

 Thin-view design: this snapshot does NOT duplicate per-action state or raw
 input events. It holds a name->Action pointer lookup and answers queries by
 reading the captured Actions directly. Actions are not modified after the end
 of kInput, so the view is stable for the remainder of the frame. The snapshot
 becomes invalid once the next frame begins.

 This design eliminates the need for signal/slot callbacks and avoids data
 redundancy, providing a clean, query-based API.
*/
class InputSnapshot {
public:
  //! Construct snapshot from current action states
  /*!
   @param actions All actions managed by the InputSystem
  */
  OXGN_NPUT_API explicit InputSnapshot(
    const std::vector<std::shared_ptr<Action>>& actions);

  // -- Action state queries ---------------------------------------------------

  //! LEVEL query: final snapshot flags at frame end
  /*! Returns the bitfield of the final action state at the end of the frame.
   Prefer DidAction* methods for edge queries within the frame window. */
  [[nodiscard]] OXGN_NPUT_API ActionState GetActionStateFlags(
    std::string_view action_name) const;

  //! Check if action is in triggered state NOTE: Final snapshot flag (level at
  //! frame end). For edges, prefer DidActionTrigger().
  [[nodiscard]] OXGN_NPUT_API bool IsActionTriggered(
    std::string_view action_name) const;

  //! Check if action is in ongoing state
  [[nodiscard]] OXGN_NPUT_API bool IsActionOngoing(
    std::string_view action_name) const;

  //! Check if action is in completed state NOTE: Final snapshot flag (level at
  //! frame end). For edges, prefer DidActionComplete().
  [[nodiscard]] OXGN_NPUT_API bool IsActionCompleted(
    std::string_view action_name) const;

  //! Check if action is in canceled state NOTE: Final snapshot flag (level at
  //! frame end). For edges, prefer DidActionCancel().
  [[nodiscard]] OXGN_NPUT_API bool IsActionCanceled(
    std::string_view action_name) const;

  //! Check if action is in idle state
  [[nodiscard]] OXGN_NPUT_API bool IsActionIdle(
    std::string_view action_name) const;

  //! Get current action value
  [[nodiscard]] OXGN_NPUT_API ActionValue GetActionValue(
    std::string_view action_name) const;

  // -- Transition queries for animations --------------------------------------

  //! Check if action transitioned from idle/none to triggered this frame
  [[nodiscard]] OXGN_NPUT_API bool DidActionStart(
    std::string_view action_name) const;

  //! EDGE: Did the action produce a Triggered transition in this frame
  [[nodiscard]] OXGN_NPUT_API bool DidActionTrigger(
    std::string_view action_name) const;

  //! Check if action completed this frame
  [[nodiscard]] OXGN_NPUT_API bool DidActionComplete(
    std::string_view action_name) const;

  //! Check if action was canceled this frame
  [[nodiscard]] OXGN_NPUT_API bool DidActionCancel(
    std::string_view action_name) const;

  //! EDGE: Did the action release (Ongoing -> not Ongoing) this frame
  [[nodiscard]] OXGN_NPUT_API bool DidActionRelease(
    std::string_view action_name) const;

  //! EDGE: Was the action value updated at least once this frame
  [[nodiscard]] OXGN_NPUT_API bool DidActionValueUpdate(
    std::string_view action_name) const;

  //! Check if a specific state transition occurred this frame
  [[nodiscard]] OXGN_NPUT_API bool DidActionTransition(
    std::string_view action_name, ActionState from, ActionState to) const;

  //! Get all transitions for an action during this frame
  [[nodiscard]] OXGN_NPUT_API auto GetActionTransitions(
    std::string_view action_name) const
    -> std::span<const Action::FrameTransition>;

  // -- Raw event access -------------------------------------------------------
  // Intentionally omitted: raw input events are not part of the snapshot.

  // -- Timing information -----------------------------------------------------
  // Timing is available via FrameContext; not exposed by this snapshot.

private:
  //! Find action by name, returns nullptr if not found
  [[nodiscard]] auto FindAction(std::string_view action_name) const
    -> const Action*;

  // Name -> action pointer (lifetime owned by InputSystem)
  std::unordered_map<std::string, const Action*> actions_;
};

} // namespace oxygen::input
