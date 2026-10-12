//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionState.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/api_export.h>

namespace oxygen::platform {
class InputEvent; // forward decl kept for compatibility if needed elsewhere
} // namespace oxygen::platform

namespace oxygen::input {

//! Names the action a snapshot query reads.
/*!
 An action created in code is found by its name. An action hydrated from an
 asset is found by its asset key; its name is only a label, which actions
 from different assets may share, and a name query reads the first of them.
 Converts implicitly from either, like a string view.
*/
class ActionLookup {
public:
  // NOLINTBEGIN(*-explicit-constructor)
  // Implicit by design: queries accept a name or a key directly.
  ActionLookup(const std::string_view name) noexcept
    : name_(name)
  {
  }
  ActionLookup(const char* name) noexcept
    : name_(name)
  {
  }
  ActionLookup(const std::string& name) noexcept
    : name_(name)
  {
  }
  ActionLookup(const data::AssetKey& key) noexcept
    : key_(key)
    , by_key_(true)
  {
  }
  // NOLINTEND(*-explicit-constructor)

  //! True if this lookup names an asset key rather than a name.
  [[nodiscard]] auto ByKey() const noexcept { return by_key_; }
  [[nodiscard]] auto Name() const noexcept -> std::string_view { return name_; }
  [[nodiscard]] auto Key() const noexcept -> const data::AssetKey&
  {
    return key_;
  }

private:
  std::string_view name_;
  data::AssetKey key_;
  bool by_key_ { false };
};

//! Consolidated input snapshot capturing all frame activity
/*!
 The InputSnapshot provides a read-only view of input state for a single frame,
 including action states and intra-frame transitions. It is constructed and
 frozen at the end of the kInput phase and consumed by subsequent phases
 (FixedSim, Gameplay, etc.) in the same frame, and later published at kSnapshot
 without being rebuilt.

 The snapshot copies each action's final state, value and transitions at
 construction, so it does not depend on the actions' lifetime or on later
 frames. Queries name an action by name or by asset key (ActionLookup).

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
  [[nodiscard]] OXGN_NPUT_API auto GetActionStateFlags(
    const ActionLookup& action) const -> ActionState;

  //! Check if action is in triggered state NOTE: Final snapshot flag (level at
  //! frame end). For edges, prefer DidActionTrigger().
  [[nodiscard]] OXGN_NPUT_API auto IsActionTriggered(
    const ActionLookup& action) const -> bool;

  //! Check if action is in ongoing state
  [[nodiscard]] OXGN_NPUT_API auto IsActionOngoing(
    const ActionLookup& action) const -> bool;

  //! Check if action is in completed state NOTE: Final snapshot flag (level at
  //! frame end). For edges, prefer DidActionComplete().
  [[nodiscard]] OXGN_NPUT_API auto IsActionCompleted(
    const ActionLookup& action) const -> bool;

  //! Check if action is in canceled state NOTE: Final snapshot flag (level at
  //! frame end). For edges, prefer DidActionCancel().
  [[nodiscard]] OXGN_NPUT_API auto IsActionCanceled(
    const ActionLookup& action) const -> bool;

  //! Check if action is in idle state
  [[nodiscard]] OXGN_NPUT_API auto IsActionIdle(
    const ActionLookup& action) const -> bool;

  //! Get current action value
  [[nodiscard]] OXGN_NPUT_API auto GetActionValue(
    const ActionLookup& action) const -> ActionValue;

  // -- Transition queries for animations --------------------------------------

  //! Check if action transitioned from idle/none to triggered this frame
  [[nodiscard]] OXGN_NPUT_API auto DidActionStart(
    const ActionLookup& action) const -> bool;

  //! EDGE: Did the action produce a Triggered transition in this frame
  [[nodiscard]] OXGN_NPUT_API auto DidActionTrigger(
    const ActionLookup& action) const -> bool;

  //! Check if action completed this frame
  [[nodiscard]] OXGN_NPUT_API auto DidActionComplete(
    const ActionLookup& action) const -> bool;

  //! Check if action was canceled this frame
  [[nodiscard]] OXGN_NPUT_API auto DidActionCancel(
    const ActionLookup& action) const -> bool;

  //! EDGE: Did the action release (Ongoing -> not Ongoing) this frame
  [[nodiscard]] OXGN_NPUT_API auto DidActionRelease(
    const ActionLookup& action) const -> bool;

  //! EDGE: Was the action value updated at least once this frame
  [[nodiscard]] OXGN_NPUT_API auto DidActionValueUpdate(
    const ActionLookup& action) const -> bool;

  //! Check if a specific state transition occurred this frame
  [[nodiscard]] OXGN_NPUT_API auto DidActionTransition(
    const ActionLookup& action, ActionState from, ActionState to) const -> bool;

  //! Get all transitions for an action during this frame
  [[nodiscard]] OXGN_NPUT_API auto GetActionTransitions(
    const ActionLookup& action) const
    -> std::span<const Action::FrameTransition>;

  //! Names of every action in this snapshot, in insertion order; actions
  //! hydrated from different assets may share a name.
  [[nodiscard]] OXGN_NPUT_NDAPI auto GetActionNames() const
    -> std::vector<std::string_view>;

  //! Asset keys of the actions hydrated from assets, in insertion order.
  [[nodiscard]] OXGN_NPUT_NDAPI auto GetActionKeys() const
    -> std::vector<data::AssetKey>;

  // -- Raw event access -------------------------------------------------------
  // Intentionally omitted: raw input events are not part of the snapshot.

  // -- Timing information -----------------------------------------------------
  // Timing is available via FrameContext; not exposed by this snapshot.

private:
  //! Frozen copy of one action at snapshot time.
  struct Entry {
    std::string name;
    data::AssetKey asset_key;
    Action::State state {};
    ActionValue value;
    bool value_updated { false };
    std::vector<Action::FrameTransition> transitions;
  };

  //! Find the action a lookup names; nullptr if there is none.
  [[nodiscard]] auto FindAction(const ActionLookup& action) const
    -> const Entry*;

  // Value copies: the snapshot does not depend on Action lifetime or on
  // later frames mutating the actions.
  std::vector<Entry> actions_;
};

} // namespace oxygen::input
