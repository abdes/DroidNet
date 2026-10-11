//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/api_export.h>
#include <Oxygen/Platform/Input.h>

namespace oxygen {

namespace platform {
  class InputEvent;
} // namespace platform

namespace input {
  class ActionTrigger;
  class Action;

  //! Binds one input slot to an action, with triggers and a value transform.
  /*!
   A mapping turns platform input on its slot into an evaluation of its
   action. Several mappings may drive the same action; each contributes its
   own evaluation and the action combines them (see Action).

   ### Value transform

   The device sample is first expressed as a 2D vector. A scalar source (a key
   or mouse button: 0 or 1; a single mouse or wheel axis) feeds both
   components; a 2D source (MouseXY, MouseWheelXY) feeds x and y. The mapping
   then applies `value * scale + bias` per component and converts the result
   to the action's value type (Bool: any non-zero component; Axis1D: x).
   With scale (0, 1), W drives the y axis of a 2D move action; with
   (0, -1), S drives it negatively.

   Relative sources (mouse motion and wheel) report deltas: the scaled deltas
   of every event in a frame are summed, and `bias` does not apply to them.
   Triggers evaluate each event's transformed value.
  */
  class InputActionMapping {
  public:
    OXGN_NPUT_API InputActionMapping(
      std::shared_ptr<Action> action, platform::InputSlot input_slot);

    [[nodiscard]] auto GetAction() const { return action_; }

    [[nodiscard]] auto GetSlot() const -> const auto& { return slot_; }

    OXGN_NPUT_API void AddTrigger(std::shared_ptr<ActionTrigger> trigger);
    [[nodiscard]] auto GetTriggers() const -> const auto& { return triggers_; }

    //! Per-component multiplier applied to the device value (default 1, 1).
    void SetScale(const Axis2D& scale) noexcept { scale_ = scale; }
    [[nodiscard]] auto GetScale() const noexcept { return scale_; }

    //! Per-component offset added to absolute device values (default 0, 0).
    void SetBias(const Axis2D& bias) noexcept { bias_ = bias; }
    [[nodiscard]] auto GetBias() const noexcept { return bias_; }

    //! True for mouse motion and wheel slots, whose events are deltas.
    [[nodiscard]] OXGN_NPUT_API auto IsRelative() const -> bool;

    //! True while this binding holds its action Triggered or Ongoing.
    [[nodiscard]] auto IsActive() const noexcept { return active_; }

    OXGN_NPUT_API void HandleInput(const platform::InputEvent& event);

    //! Cancel this binding's activation (Canceled edge if it was active) and
    //! reset its triggers.
    OXGN_NPUT_API void CancelInput();

    //! Drop staged input without producing edges; resets the triggers.
    OXGN_NPUT_API void AbortStaged() noexcept;

    //! End any activation (Canceled edge if active), reset the triggers and
    //! withdraw the contribution. Called when the owning context stops
    //! receiving input.
    OXGN_NPUT_API void Deactivate();

    //! Evaluate triggers and advance time-based logic.
    /*!
     @param delta_time Elapsed time since the previous call. Time-based
     triggers (Hold, Pulse, Tap, Chain and Combo windows) advance by it.
     @return true if the action triggered from input of this update and
     consumes input.
    */
    OXGN_NPUT_NDAPI auto Update(oxygen::time::CanonicalDuration delta_time)
      -> bool;

  private:
    [[nodiscard]] auto DoUpdate(oxygen::time::CanonicalDuration delta_time)
      -> bool;
    void StartEvaluation();
    void SetSample(const Axis2D& sample);
    void Contribute(bool triggered, bool ongoing, bool canceled);
    void NotifyActionCanceled();

    std::shared_ptr<Action> action_;
    platform::InputSlot slot_;
    std::vector<std::shared_ptr<ActionTrigger>> triggers_;
    Axis2D scale_ { .x = 1.0F, .y = 1.0F };
    Axis2D bias_ { .x = 0.0F, .y = 0.0F };

    // Transformed value of the latest event, seen by the triggers.
    ActionValue action_value_;
    ActionValue last_action_value_;
    // Relative slots: transformed deltas summed over the current frame.
    Axis2D relative_sum_ { .x = 0.0F, .y = 0.0F };
    uint64_t relative_serial_ { 0 };

    bool evaluation_ongoing_ { false };
    bool event_processing_ { false };
    bool clear_value_after_update_ { false };
    bool active_ { false };
  };

} // namespace input
} // namespace oxygen
