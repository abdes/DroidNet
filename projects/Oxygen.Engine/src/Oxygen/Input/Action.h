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
#include <vector>

#include <Oxygen/Input/ActionState.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/api_export.h>

namespace oxygen::engine {
class InputSystem;
} // namespace oxygen::engine

namespace oxygen::input {

class ActionTrigger;

//=== Action -----------------------------------------------------------------//

//! High-level input Action with per-frame edges and persistent level state.
/*!
 Maintains a persistent "level" state (ongoing/value) across frames and
 exposes per-frame edges (triggered, completed, canceled, started/released
 derived from transitions) that reset every frame.

### Invariants

- Edge flags are non-sticky: `triggered`, `completed`, and `canceled` are
  cleared at the beginning of each frame by BeginFrameTracking().
- Level state persists: `ongoing` persists across frames and reflects the
  current held/active condition of the action.
- Transitions are per-frame: only transitions that occur after
  BeginFrameTracking() and before EndFrameTracking() are visible via
  GetFrameTransitions() and Was*ThisFrame() helpers.
- Value persistence: `value_` persists as the last known value across frames.
  Use WasValueUpdatedThisFrame() to detect updates in the current frame.

### Contract

- Inputs: State snapshots (from mappings/triggers) and an ActionValue.
- Outputs: Per-frame transitions and edge helpers for the current frame.
- Error modes: Duplicate updates with identical flags produce no transitions.

### Frame Lifecycle

- BeginFrameTracking():
  - Clears per-frame edges and transitions: sets `triggered`, `completed`,
    and `canceled` to false; preserves `ongoing`.
  - Clears the per-frame value-updated flag.
- UpdateState(state, value):
  - Updates `state_` and `value_`.
  - Records a transition when the snapshot flags change.
- EndFrameTracking():
  - No hard reset. Level state (`ongoing` and `value_`) persists into the
    next frame. Per-frame transitions remain as the snapshot of this frame.

### Usage Notes

- Use IsOngoing()/IsIdle() for level conditions.
- Use Was*ThisFrame() helpers for edges (Started/Released/etc.).
- Consumers that need precise timing should read GetFrameTransitions().

 @see BeginFrameTracking, EndFrameTracking, GetFrameTransitions,
 WasTriggeredThisFrame, WasCompletedThisFrame, WasCanceledThisFrame,
 WasReleasedThisFrame, WasValueUpdatedThisFrame
*/
class Action {
public:
  //! Update action state and record transitions
  // New: update using a scoped State struct to avoid error-prone bool args
  struct State {
    bool triggered { false };
    bool ongoing { false };
    bool completed { false };
    bool canceled { false };

    //! Convert this State to the ActionState bitfield
    OXGN_NPUT_NDAPI auto ToActionState() const -> ActionState;

    //! Construct a State from an ActionState bitfield
    OXGN_NPUT_NDAPI static auto FromActionState(ActionState states) -> State;
  };

  //! Represents a single action state transition within a frame
  struct FrameTransition {
    ActionState from_state;
    ActionState to_state;
    std::chrono::steady_clock::time_point timestamp;
    ActionValue value_at_transition;
  };

  OXGN_NPUT_API Action(std::string name, ActionValueType value_type);

  [[nodiscard]] auto GetName() const -> const std::string& { return name_; }

  [[nodiscard]] auto GetValueType() const -> ActionValueType
  {
    return value_type_;
  }

  [[nodiscard]] auto GetValue() const -> const ActionValue& { return value_; }

  //! Value lifetime semantics:
  //! - `value_` persists as the last known value across frames.
  //! - Use WasValueUpdatedThisFrame() to detect value updates in the frame.
  OXGN_NPUT_NDAPI auto WasValueUpdatedThisFrame() const -> bool;

  [[nodiscard]] auto ConsumesInput() const { return consume_input_; }
  void SetConsumesInput(bool consume) { consume_input_ = consume; }

  // -- Action state queries ---------------------------------------------------

  [[nodiscard]] auto IsCanceled() const { return state_.canceled; }
  [[nodiscard]] auto IsCompleted() const { return state_.completed; }
  [[nodiscard]] auto IsOngoing() const { return state_.ongoing; }
  [[nodiscard]] auto IsIdle() const
  {
    return !(state_.ongoing || state_.triggered);
  }
  [[nodiscard]] auto IsTriggered() const { return state_.triggered; }

  OXGN_NPUT_API void UpdateState(const State& state, const ActionValue& value);

  // -- Frame transition tracking ----------------------------------------------

  //! Begin tracking transitions for a new frame
  OXGN_NPUT_API void BeginFrameTracking();

  //! End frame tracking and finalize transition history
  OXGN_NPUT_API void EndFrameTracking();

  //! Get all transitions that occurred during the current frame
  [[nodiscard]] auto GetFrameTransitions() const
    -> std::span<const FrameTransition>
  {
    return frame_transitions_;
  }

  // -- Convenience per-frame edge queries -----------------------------------

  //! True if the action transitioned Idle->Ongoing in this frame
  OXGN_NPUT_NDAPI auto WasStartedThisFrame() const -> bool;

  //! True if any transition in this frame included the Triggered edge
  OXGN_NPUT_NDAPI auto WasTriggeredThisFrame() const -> bool;

  //! True if any transition in this frame included the Completed edge
  OXGN_NPUT_NDAPI auto WasCompletedThisFrame() const -> bool;

  //! True if any transition in this frame included the Canceled edge
  OXGN_NPUT_NDAPI auto WasCanceledThisFrame() const -> bool;

  //! True if the action transitioned Ongoing->Idle in this frame
  OXGN_NPUT_NDAPI auto WasReleasedThisFrame() const -> bool;

private:
  std::string name_;
  ActionValueType value_type_;
  ActionValue value_;
  bool consume_input_ { false };

  // Current action state
  State state_ {
    .triggered = false,
    .ongoing = false,
    .completed = false,
    .canceled = false,
  };

  // Frame transition tracking
  std::vector<FrameTransition> frame_transitions_;

  // Per-frame flag indicating whether value_ was updated in the frame
  bool value_updated_this_frame_ { false };

  //! Helper to record a state transition
  void RecordTransition(
    ActionState from, ActionState to, const ActionValue& value);

  // TODO(abdes): Timebase consistency: use engine time (see TimeUtils.h) for
  // timestamps in transitions for consistent combo/chain timing.
};

} // namespace oxygen::input

// TODO(abdes) ass some debugging counters for the action.

// ElapsedProcessedTime:	How long it took from started to
// completed/canceled.

// LastTriggeredWorldTime:	last time it evaluated to a triggered state.
