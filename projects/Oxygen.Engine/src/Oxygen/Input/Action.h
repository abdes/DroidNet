//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Input/ActionState.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/api_export.h>

namespace oxygen::engine {
class InputSystem;
} // namespace oxygen::engine

namespace oxygen::input {

class InputActionMapping;

namespace testing {
  class ActionTestAccess;
} // namespace testing

//! How an action combines the values of its bindings.
enum class ActionAccumulation : uint8_t {
  //! Per component, keep the value with the largest magnitude. Duplicate
  //! bindings for one direction do not stack (W and Up give 1); opposing
  //! bindings do not cancel.
  kHighestAbsolute,
  //! Per component, sum the bindings. Opposing bindings cancel (W and S give
  //! 0); duplicate bindings for one direction stack (W and Up give 2).
  kCumulative,
};

OXGN_NPUT_NDAPI auto to_string(ActionAccumulation value) noexcept
  -> std::string_view;

//! High-level input action with per-frame edges and persistent level state.
/*!
 An action is driven by one or more bindings (InputActionMapping), possibly in
 several mapping contexts. Each binding contributes its own evaluation; the
 action combines them:

 - Ongoing and Triggered when any binding is.
 - Canceled only when a binding canceled and no binding is active.
 - Value per component according to GetAccumulation().
 - Completed when an activation that triggered at least once ends without a
   cancel. Activations that never hold (Pressed, Tap, Released) complete on
   the frame after they trigger, so every activation that triggered is closed
   by exactly one Completed edge.

 ### Frame model

 Level state (`ongoing`, value) persists across frames. Edges (`triggered`,
 `completed`, `canceled`) and the frame's transitions are cleared when the
 InputSystem starts a frame. Use the Was*ThisFrame() queries for edges and
 Is*() for levels.

 ### Ownership

 Gameplay reads actions; only the input system changes them. State mutation
 is private to InputSystem and InputActionMapping. Engine phases after kInput
 should read the published InputSnapshot, which is immutable.

 @see InputActionMapping, InputSnapshot
*/
class Action {
public:
  //! Level and edge flags of an action or of one binding's evaluation.
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
    ActionState from_state { ActionState::kNone };
    ActionState to_state { ActionState::kNone };
    ActionValue value_at_transition;
  };

  OXGN_NPUT_API Action(std::string name, ActionValueType value_type);

  [[nodiscard]] auto GetName() const -> const std::string& { return name_; }

  [[nodiscard]] auto GetValueType() const -> ActionValueType
  {
    return value_type_;
  }

  //! Current value, combined from all bindings. Relative inputs (mouse
  //! motion, wheel) hold the sum of the current frame's motion.
  [[nodiscard]] auto GetValue() const -> const ActionValue& { return value_; }

  //! True if the value was written at least once in the current frame.
  OXGN_NPUT_NDAPI auto WasValueUpdatedThisFrame() const -> bool;

  // -- Configuration ----------------------------------------------------------

  [[nodiscard]] auto ConsumesInput() const { return consume_input_; }
  void SetConsumesInput(bool consume) { consume_input_ = consume; }

  [[nodiscard]] auto GetAccumulation() const { return accumulation_; }
  void SetAccumulation(const ActionAccumulation mode) { accumulation_ = mode; }

  // -- Level queries ----------------------------------------------------------

  [[nodiscard]] auto IsCanceled() const { return state_.canceled; }
  [[nodiscard]] auto IsCompleted() const { return state_.completed; }
  [[nodiscard]] auto IsOngoing() const { return state_.ongoing; }
  [[nodiscard]] auto IsIdle() const
  {
    return !(state_.ongoing || state_.triggered);
  }
  [[nodiscard]] auto IsTriggered() const { return state_.triggered; }

  // -- Edge queries (current frame) -------------------------------------------

  //! All transitions recorded in the current frame, in order.
  [[nodiscard]] auto GetFrameTransitions() const
    -> std::span<const FrameTransition>
  {
    return frame_transitions_;
  }

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

  //! Identifies the current frame; advances each time a frame starts. Lets
  //! observers tell this frame's transitions from a previous frame's.
  [[nodiscard]] auto GetFrameSerial() const noexcept { return frame_serial_; }

  // -- Fixed-step consumption -------------------------------------------------

  //! Consume one Triggered edge for a fixed-step consumer.
  /*!
   Every rising Triggered edge is latched until consumed, independently of the
   per-frame edge window. A fixed-step simulation calls this once per substep:
   one press yields exactly one `true`, whether the frame runs zero, one or
   several substeps. Actions that trigger every frame while held (Down, Pulse)
   latch one edge per frame in which they trigger.
  */
  OXGN_NPUT_NDAPI auto ConsumeFixedStepTrigger() -> bool;

  //! Number of latched Triggered edges not yet consumed.
  [[nodiscard]] auto GetPendingFixedStepTriggers() const noexcept
  {
    return pending_fixed_triggers_;
  }

private:
  friend class engine::InputSystem;
  friend class InputActionMapping;
  friend class testing::ActionTestAccess;

  //! One binding's latest evaluation of this action.
  struct Contribution {
    const void* binding { nullptr };
    State state {};
    ActionValue value;
    //! Frame serial at which `state` was written; edges from earlier frames
    //! are ignored when combining.
    uint64_t frame_serial { 0 };
  };

  //! Record a binding's evaluation and recompute the action.
  OXGN_NPUT_API void Contribute(
    const void* binding, const State& state, const ActionValue& value);

  //! Forget a binding (its context was deactivated or removed).
  OXGN_NPUT_API void RemoveContribution(const void* binding);

  //! Start a frame: clear edges and transitions; close non-held activations
  //! that triggered in the previous frame with a Completed edge.
  OXGN_NPUT_API void BeginFrameTracking();

  //! End a frame. Level state persists; transitions stay readable until the
  //! next BeginFrameTracking().
  OXGN_NPUT_API void EndFrameTracking();

  //! Replace the state and value and record a transition if the flags change.
  OXGN_NPUT_API void UpdateState(const State& state, const ActionValue& value);

  void Recombine();
  void RecordTransition(
    ActionState from, ActionState to, const ActionValue& value);
  [[nodiscard]] auto ZeroValue() const -> ActionValue;

  std::string name_;
  ActionValueType value_type_;
  ActionValue value_;
  bool consume_input_ { false };
  ActionAccumulation accumulation_ { ActionAccumulation::kHighestAbsolute };

  State state_ {};
  std::vector<FrameTransition> frame_transitions_;
  bool value_updated_this_frame_ { false };

  std::vector<Contribution> contributions_;
  uint64_t frame_serial_ { 1 };
  bool triggered_this_activation_ { false };
  uint32_t pending_fixed_triggers_ { 0 };
};

} // namespace oxygen::input
