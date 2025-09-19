//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionState.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/api_export.h>

namespace oxygen::input {

//-- ActionTrigger -------------------------------------------------------------

enum class ActionTriggerType : uint8_t {
  kPressed,
  kReleased,
  kDown,
  kHold,
  kHoldAndRelease,
  kPulse,
  kTap,
  kChord,
  kActionChain,
  kCombo,
};

class ActionTrigger {
public:
  enum class Behavior : uint8_t {
    kExplicit, // Input may trigger if any explicit trigger is triggered.
    kImplicit, // Input may trigger only if all implicit triggers are
    // triggered.
    kBlocker, // Inverted trigger that will block all other triggers if it
    // is triggered.
  };

  explicit ActionTrigger() = default;
  virtual ~ActionTrigger() = default;

  OXYGEN_DEFAULT_COPYABLE(ActionTrigger);
  OXYGEN_DEFAULT_MOVABLE(ActionTrigger);

  [[nodiscard]] virtual auto GetType() const -> ActionTriggerType = 0;

  [[nodiscard]] auto IsExplicit() const
  {
    return behavior_ == Behavior::kExplicit;
  }

  void MakeExplicit() { behavior_ = Behavior::kExplicit; }

  [[nodiscard]] auto IsImplicit() const
  {
    return behavior_ == Behavior::kImplicit;
  }

  void MakeImplicit() { behavior_ = Behavior::kImplicit; }

  [[nodiscard]] auto IsBlocker() const
  {
    return behavior_ == Behavior::kBlocker;
  }

  void MakeBlocker() { behavior_ = Behavior::kBlocker; }

  void SetActuationThreshold(float threshold)
  {
    actuation_threshold_ = threshold;
  }

  [[nodiscard]] auto IsIdle() const { return state_ == State::kIdle; }

  [[nodiscard]] auto IsOngoing() const { return state_ == State::kOngoing; }

  [[nodiscard]] virtual auto IsTriggered() const -> bool { return triggered_; }

  [[nodiscard]] virtual auto IsCanceled() const -> bool
  {
    return !triggered_ && (previous_state_ == State::kOngoing)
      && (state_ == State::kIdle);
  }

  [[nodiscard]] virtual auto IsCompleted() const -> bool
  {
    return triggered_ && (state_ == State::kIdle);
  }

  OXGN_NPUT_API void UpdateState(const ActionValue& action_value,
    oxygen::time::CanonicalDuration delta_time);

protected:
  enum class State : uint8_t {
    kIdle,
    kOngoing,
  };
  virtual auto DoUpdateState(const ActionValue& action_value,
    oxygen::time::CanonicalDuration delta_time) -> bool
    = 0;

  void SetTriggerState(State state)
  {
    previous_state_ = state_;
    state_ = state;
  }

  [[nodiscard]] auto GetActuationThreshold() const
  {
    return actuation_threshold_;
  }

  [[nodiscard]] auto GetPreviousState() const { return previous_state_; }

private:
  Behavior behavior_ { Behavior::kImplicit };
  float actuation_threshold_ { 0.5F };
  State state_ { State::kIdle };
  State previous_state_ { State::kIdle };
  bool triggered_ { false };
};

//-- ActionTriggerPressed ------------------------------------------------------

// Trigger fires once only when input exceeds the actuation threshold. Holding
// the input will not cause further triggers.
class ActionTriggerPressed : public ActionTrigger {
public:
  ActionTriggerPressed() = default;
  ~ActionTriggerPressed() override = default;

  OXYGEN_DEFAULT_COPYABLE(ActionTriggerPressed);
  OXYGEN_DEFAULT_MOVABLE(ActionTriggerPressed);

  [[nodiscard]] auto GetType() const -> ActionTriggerType override
  {
    return ActionTriggerType::kPressed;
  }

protected:
  OXGN_NPUT_API auto DoUpdateState(const ActionValue& action_value,
    oxygen::time::CanonicalDuration delta_time) -> bool override;

private:
  bool depleted_ { false };
};

//-- ActionTriggerReleased -----------------------------------------------------

class ActionTriggerReleased : public ActionTrigger {
public:
  ActionTriggerReleased() = default;
  ~ActionTriggerReleased() override = default;

  OXYGEN_DEFAULT_COPYABLE(ActionTriggerReleased);
  OXYGEN_DEFAULT_MOVABLE(ActionTriggerReleased);

  [[nodiscard]] auto GetType() const -> ActionTriggerType override
  {
    return ActionTriggerType::kReleased;
  }

protected:
  OXGN_NPUT_API auto DoUpdateState(const ActionValue& action_value,
    oxygen::time::CanonicalDuration delta_time [[maybe_unused]])
    -> bool override;
};

//-- ActionTriggerDown ---------------------------------------------------------

// Trigger fires when input exceeds the actuation threshold. Holding
// the input will cause further triggers.
class ActionTriggerDown : public ActionTrigger {
public:
  OXGN_NPUT_API ActionTriggerDown();
  ~ActionTriggerDown() override = default;

  OXYGEN_DEFAULT_COPYABLE(ActionTriggerDown);
  OXYGEN_DEFAULT_MOVABLE(ActionTriggerDown);

  [[nodiscard]] auto GetType() const -> ActionTriggerType override
  {
    return ActionTriggerType::kDown;
  }

  [[nodiscard]] auto IsCompleted() const -> bool override
  {
    return triggered_once_ && IsIdle();
  }

  [[nodiscard]] auto IsCanceled() const -> bool override
  {
    return ActionTrigger::IsCanceled() && !triggered_once_;
  }

protected:
  OXGN_NPUT_API auto DoUpdateState(const ActionValue& action_value,
    oxygen::time::CanonicalDuration delta_time) -> bool override;

private:
  bool triggered_once_ { false };
};

//-- ActionTriggerTimed --------------------------------------------------------

// Base class for building triggers that have firing conditions governed by
// elapsed time. This class transitions state to Ongoing once input is actuated,
// and will track Ongoing input time until input is released. Inheriting classes
// should provide the logic for Triggered transitions.
class ActionTriggerTimed : public ActionTrigger {
public:
  explicit ActionTriggerTimed() = default;
  ~ActionTriggerTimed() override = default;

  OXYGEN_DEFAULT_COPYABLE(ActionTriggerTimed);
  OXYGEN_DEFAULT_MOVABLE(ActionTriggerTimed);

protected:
  OXGN_NPUT_API auto DoUpdateState(const ActionValue& action_value,
    oxygen::time::CanonicalDuration delta_time) -> bool override;

  [[nodiscard]] virtual auto GetHeldDuration() const
    -> oxygen::time::CanonicalDuration
  {
    return held_duration_;
  }

private:
  oxygen::time::CanonicalDuration held_duration_ {};
};

//-- ActionTriggerHold ---------------------------------------------------------

// Trigger fires once input has remained actuated for HoldTimeThreshold seconds.
// Trigger may optionally fire once, or repeatedly fire.
class ActionTriggerHold : public ActionTriggerTimed {
public:
  explicit ActionTriggerHold() = default;
  ~ActionTriggerHold() override = default;

  OXYGEN_DEFAULT_COPYABLE(ActionTriggerHold);
  OXYGEN_DEFAULT_MOVABLE(ActionTriggerHold);

  [[nodiscard]] auto GetType() const -> ActionTriggerType override
  {
    return ActionTriggerType::kHold;
  }

  void SetHoldDurationThreshold(const float threshold_seconds)
  {
    hold_duration_threshold_ = oxygen::time::CanonicalDuration {
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<float>(threshold_seconds))
    };
  }

  [[nodiscard]] auto GetHoldDurationThreshold() const
  {
    return hold_duration_threshold_;
  }

  [[nodiscard]] auto IsOneShot() const { return one_shot_; }
  void OneShot(bool enable = true) { one_shot_ = enable; }

  [[nodiscard]] auto IsCompleted() const -> bool override
  {
    return triggered_once_ && (IsIdle());
  }

protected:
  OXGN_NPUT_API auto DoUpdateState(const ActionValue& action_value,
    oxygen::time::CanonicalDuration delta_time) -> bool override;

private:
  oxygen::time::CanonicalDuration hold_duration_threshold_ {};
  bool one_shot_ { true };
  bool triggered_once_ { false };
};

//-- ActionTriggerHoldAndRelease -----------------------------------------------

// Trigger fires when input is released after having been actuated for at least
// HoldTimeThreshold seconds.
class ActionTriggerHoldAndRelease : public ActionTriggerTimed {
public:
  explicit ActionTriggerHoldAndRelease() = default;
  ~ActionTriggerHoldAndRelease() override = default;

  OXYGEN_DEFAULT_COPYABLE(ActionTriggerHoldAndRelease);
  OXYGEN_DEFAULT_MOVABLE(ActionTriggerHoldAndRelease);

  [[nodiscard]] auto GetType() const -> ActionTriggerType override
  {
    return ActionTriggerType::kHoldAndRelease;
  }

  void SetHoldDurationThreshold(const float threshold_seconds)
  {
    hold_duration_threshold_ = oxygen::time::CanonicalDuration {
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<float>(threshold_seconds))
    };
  }

  [[nodiscard]] auto GetHoldDurationThreshold() const
  {
    return hold_duration_threshold_;
  }

protected:
  OXGN_NPUT_API auto DoUpdateState(const ActionValue& action_value,
    oxygen::time::CanonicalDuration delta_time) -> bool override;

private:
  oxygen::time::CanonicalDuration hold_duration_threshold_ {};
};

//-- ActionTriggerPulse --------------------------------------------------------

// Trigger that fires at a fixed interval while the input remains actuated.
// Behavior:
// - Enters Ongoing as soon as the input actuates (press/down).
// - Emits Triggered events each time the configured interval elapses while
//   held (metronome-like behavior).
// - Cancels when the input is released (Idle after having been Ongoing).
// Notes:
// - There is no "completed" terminal state for Pulse; releasing input ends the
//   pulse sequence via cancellation.
class ActionTriggerPulse : public ActionTriggerTimed {
public:
  explicit ActionTriggerPulse() = default;
  ~ActionTriggerPulse() override = default;

  OXYGEN_DEFAULT_COPYABLE(ActionTriggerPulse);
  OXYGEN_DEFAULT_MOVABLE(ActionTriggerPulse);

  [[nodiscard]] auto GetType() const -> ActionTriggerType override
  {
    return ActionTriggerType::kPulse;
  }

  void SetInterval(const float interval_seconds)
  {
    interval_ = oxygen::time::CanonicalDuration {
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<float>(interval_seconds))
    };
  }

  [[nodiscard]] auto GetInterval() const { return interval_; }

  [[nodiscard]] auto TriggerOnStart() const { return trigger_on_start_; }
  void TriggerOnStart(bool enable = true) { trigger_on_start_ = enable; }

  [[nodiscard]] auto GetTriggerLimit() const { return trigger_limit_; }
  void SetTriggerLimit(uint32_t trigger_limit = 0)
  {
    trigger_limit_ = trigger_limit;
  }

  [[nodiscard]] auto IsCompleted() const -> bool override { return false; }

  [[nodiscard]] auto IsCanceled() const -> bool override
  {
    return IsIdle() && (GetPreviousState() == State::kOngoing);
  }

  //=== Optional stability controls ===---------------------------------------//

  //! Allow slightly late frames to count as on-time pulses
  OXGN_NPUT_API void SetJitterTolerance(float seconds);

  //! When enabled, carry over overshoot so pulses stay phase-aligned to start
  OXGN_NPUT_API void EnablePhaseAlignment(bool enable = true);

  //! Linearly ramp the interval from start->end over ramp_duration seconds
  OXGN_NPUT_API void SetRateRamp(float start_interval_seconds,
    float end_interval_seconds, float ramp_duration_seconds);

protected:
  OXGN_NPUT_API auto DoUpdateState(const ActionValue& action_value,
    oxygen::time::CanonicalDuration delta_time) -> bool override;

private:
  oxygen::time::CanonicalDuration interval_ { std::chrono::seconds { 1 } };
  bool trigger_on_start_ { false };
  uint32_t trigger_limit_ { 0 };
  uint32_t trigger_count_ { 0 };

  // Stability controls
  oxygen::time::CanonicalDuration jitter_tolerance_ { {} };
  bool phase_align_ { true };
  oxygen::time::CanonicalDuration ramp_start_ { {} };
  oxygen::time::CanonicalDuration ramp_end_ { {} };
  oxygen::time::CanonicalDuration ramp_duration_ { {} };
  bool ramp_enabled_ { false };

  // Internal accumulators
  oxygen::time::CanonicalDuration leftover_ { {} }; // carry-over past interval
  oxygen::time::CanonicalDuration time_since_actuation_ {
    {}
  }; // absolute since press
  oxygen::time::CanonicalDuration accum_since_last_ { {} }; // since last pulse
};

//-- ActionTriggerTap ----------------------------------------------------------

// Input must be actuated then released within tap release time threshold
// seconds to trigger.
class ActionTriggerTap : public ActionTriggerTimed {
public:
  explicit ActionTriggerTap() = default;
  ~ActionTriggerTap() override = default;

  OXYGEN_DEFAULT_COPYABLE(ActionTriggerTap);
  OXYGEN_DEFAULT_MOVABLE(ActionTriggerTap);

  [[nodiscard]] auto GetType() const -> ActionTriggerType override
  {
    return ActionTriggerType::kTap;
  }

  void SetTapTimeThreshold(const float threshold_seconds)
  {
    threshold_ = oxygen::time::CanonicalDuration {
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<float>(threshold_seconds))
    };
  }

  [[nodiscard]] auto GetTapTimeThreshold() const { return threshold_; }

  // Cancel only when released after a press that exceeded the tap window
  [[nodiscard]] auto IsCanceled() const -> bool override
  {
    // We consider it a cancel if we just transitioned Ongoing -> Idle,
    // did not trigger, and the held duration exceeded the tap threshold.
    return IsIdle() && (GetPreviousState() == State::kOngoing) && !IsTriggered()
      && (GetHeldDuration() > threshold_);
  }

protected:
  OXGN_NPUT_API auto DoUpdateState(const ActionValue& action_value,
    oxygen::time::CanonicalDuration delta_time) -> bool override;

private:
  oxygen::time::CanonicalDuration threshold_ {};
};

//-- ActionTriggerChain --------------------------------------------------------

// Links this trigger to an action that must trigger for this one to trigger.
// Note that when this trigger is associated with an action, no other action
// with the same input slot should trigger when the former does. It is therefore
// important to consume input from an action that has a trigger chain.
class ActionTriggerChain : public ActionTrigger {
public:
  ActionTriggerChain() = default;
  ~ActionTriggerChain() override = default;

  OXYGEN_DEFAULT_COPYABLE(ActionTriggerChain);
  OXYGEN_DEFAULT_MOVABLE(ActionTriggerChain);

  [[nodiscard]] auto GetType() const -> ActionTriggerType override
  {
    return ActionTriggerType::kActionChain;
  }

  OXGN_NPUT_API void SetLinkedAction(std::shared_ptr<Action> action);
  [[nodiscard]] auto GetLinkedAction() const -> std::weak_ptr<const Action>
  {
    return linked_action_;
  }

  //=== Optional temporal/strict controls ===--------------------------------//
  //! Expire the armed gate if local condition doesn't occur in time.
  //! 0 seconds disables the window (default disabled).
  OXGN_NPUT_API void SetMaxDelaySeconds(float seconds);

  //! Require prerequisite to be Ongoing at the instant of local press.
  OXGN_NPUT_API void RequirePrerequisiteHeld(bool enable = true);

protected:
  OXGN_NPUT_API auto DoUpdateState(const ActionValue& action_value,
    oxygen::time::CanonicalDuration delta_time) -> bool override;

private:
  std::shared_ptr<Action> linked_action_;
  // Tracks local input edge to implement a simple "press" condition
  bool prev_actuated_ { false };
  // Armed once prerequisite has triggered; reset when prerequisite
  // idles/cancels
  bool armed_ { false };
  // Max delay window after arming; 0 disables
  oxygen::time::CanonicalDuration max_delay_ { {} };
  oxygen::time::CanonicalDuration window_elapsed_ { {} };
  // Require prerequisite to be ongoing at the moment of local press
  bool require_prereq_held_ { false };
  // If we expire due to max-delay, don't re-arm until prerequisite idles
  bool disarmed_until_idle_ { false };
};

//-- ActionTriggerCombo --------------------------------------------------------

struct InputComboStep {
  std::shared_ptr<Action> action;
  ActionState completion_states;
  oxygen::time::CanonicalDuration time_to_complete;
};

struct InputComboBreaker {
  std::shared_ptr<Action> action;
  ActionState completion_states;
};

// A sequence of actions that must enter a certain state (triggered, completed,
// etc) in the order they are specified in the combo array for this trigger to
// fire.
class ActionTriggerCombo : public ActionTrigger {
public:
  ActionTriggerCombo() = default;
  ~ActionTriggerCombo() override = default;

  OXYGEN_DEFAULT_COPYABLE(ActionTriggerCombo);
  OXYGEN_DEFAULT_MOVABLE(ActionTriggerCombo);

  [[nodiscard]] auto GetType() const -> ActionTriggerType override
  {
    return ActionTriggerType::kCombo;
  }

  OXGN_NPUT_API void AddComboStep(std::shared_ptr<Action> action,
    ActionState completion_states = ActionState::kTriggered,
    float time_to_complete_seconds = 0.5F);
  OXGN_NPUT_API void RemoveComboStep(uint32_t index);
  OXGN_NPUT_API void ClearComboSteps();
  [[nodiscard]] auto GetComboSteps() const { return combo_steps_; }

  OXGN_NPUT_API void AddComboBreaker(std::shared_ptr<Action> action,
    ActionState completion_states = ActionState::kTriggered);
  OXGN_NPUT_API void RemoveComboBreaker(uint32_t index);
  OXGN_NPUT_API void ClearComboBreakers();
  [[nodiscard]] auto GetComboBreakers() const { return combo_breakers_; }

protected:
  OXGN_NPUT_API auto DoUpdateState(const ActionValue& action_value,
    oxygen::time::CanonicalDuration delta_time) -> bool override;

private:
  std::vector<InputComboStep> combo_steps_;
  std::vector<InputComboBreaker> combo_breakers_;

  oxygen::time::CanonicalDuration waited_time_ {};
  size_t current_step_index_ { 0 };
};

} // namespace oxygen::input

// TODO(abdes) trigger has supported event types:
// None               = (0x0),
// Instant            = (1 << 0),
// Uninterruptible    = (1 << 1),
// Ongoing            = (1 << 2),
// All                = (Instant | Uninterruptible | Ongoing),
