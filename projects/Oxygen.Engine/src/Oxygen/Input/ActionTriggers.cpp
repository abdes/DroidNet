//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>

#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionState.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>

using oxygen::input::ActionTrigger;
using oxygen::input::ActionTriggerChain;
using oxygen::input::ActionTriggerChord;
using oxygen::input::ActionTriggerCombo;
using oxygen::input::ActionTriggerDown;
using oxygen::input::ActionTriggerHold;
using oxygen::input::ActionTriggerHoldAndRelease;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::ActionTriggerPulse;
using oxygen::input::ActionTriggerReleased;
using oxygen::input::ActionTriggerTap;
using oxygen::input::ActionTriggerTimed;

//-- ActionTrigger -------------------------------------------------------------

void ActionTrigger::UpdateState(const ActionValue& action_value,
  const oxygen::time::CanonicalDuration delta_time)
{
  triggered_ = DoUpdateState(action_value, delta_time);
}

//-- ActionTriggerPressed ------------------------------------------------------

auto ActionTriggerPressed::DoUpdateState(const ActionValue& action_value,
  oxygen::time::CanonicalDuration /*delta_time*/) -> bool
{
  if (!depleted_ && action_value.IsActuated(GetActuationThreshold())) {
    SetTriggerState(State::kIdle);
    depleted_ = true;
    return true;
  }
  if (depleted_ && !action_value.IsActuated(GetActuationThreshold())) {
    depleted_ = false;
  }
  return false;
}

//-- ActionTriggerReleased -----------------------------------------------------

auto ActionTriggerReleased::DoUpdateState(const ActionValue& action_value,
  oxygen::time::CanonicalDuration /*delta_time*/) -> bool
{
  // We only support these button family of input events
  if (IsIdle() && action_value.IsActuated(GetActuationThreshold())) {
    SetTriggerState(State::kOngoing);
    return false;
  }
  if (IsOngoing() && !action_value.IsActuated(GetActuationThreshold())) {
    SetTriggerState(State::kIdle);
    return true;
  }
  return false;
}

//-- ActionTriggerDown ---------------------------------------------------------

ActionTriggerDown::ActionTriggerDown() { SetActuationThreshold(0.5F); }

auto ActionTriggerDown::DoUpdateState(const ActionValue& action_value,
  oxygen::time::CanonicalDuration /*delta_time*/) -> bool
{
  // We only support these button family of input events
  if (action_value.IsActuated(GetActuationThreshold())) {
    if (IsIdle()) {
      triggered_once_ = false;
    }
    SetTriggerState(State::kOngoing);
    triggered_once_ = true;
    return true;
  }
  if (!action_value.IsActuated(GetActuationThreshold())) {
    SetTriggerState(State::kIdle);
  }
  return false;
}

//-- ActionTriggerTimed --------------------------------------------------------

auto ActionTriggerTimed::DoUpdateState(const ActionValue& action_value,
  const oxygen::time::CanonicalDuration delta_time) -> bool
{
  if (action_value.IsActuated(GetActuationThreshold())) {
    if (IsIdle() || (IsTriggered() && IsOngoing())) {
      held_duration_ = {};
      SetTriggerState(State::kOngoing);
    }
    held_duration_ += delta_time;
  } else {
    SetTriggerState(State::kIdle);
  }
  return false;
}

//-- ActionTriggerHold ---------------------------------------------------------

auto ActionTriggerHold::DoUpdateState(const ActionValue& action_value,
  const oxygen::time::CanonicalDuration delta_time) -> bool
{
  if (IsCompleted()) {
    triggered_once_ = false;
  }
  ActionTriggerTimed::DoUpdateState(action_value, delta_time);
  if (GetHeldDuration() >= hold_duration_threshold_
    && (!triggered_once_ || !IsOneShot())) {
    triggered_once_ = true;
    return true;
  }
  return false;
}

//-- ActionTriggerHoldAndRelease -----------------------------------------------

auto ActionTriggerHoldAndRelease::DoUpdateState(const ActionValue& action_value,
  const oxygen::time::CanonicalDuration delta_time) -> bool
{
  ActionTriggerTimed::DoUpdateState(action_value, delta_time);
  return !action_value.IsActuated(GetActuationThreshold())
    && GetHeldDuration() >= hold_duration_threshold_;
}

//-- ActionTriggerPulse --------------------------------------------------------

auto ActionTriggerPulse::DoUpdateState(const ActionValue& action_value,
  const oxygen::time::CanonicalDuration delta_time) -> bool
{
  // Reset counters on full idle
  if (IsIdle() && GetPreviousState() == State::kIdle) {
    trigger_count_ = 0;
    leftover_ = {};
    time_since_actuation_ = {};
    accum_since_last_ = {};
  }

  // Update base timed state and timers
  ActionTriggerTimed::DoUpdateState(action_value, delta_time);

  // If not actuated, nothing to do (completed/canceled derived from state)
  if (!IsOngoing()) {
    return false;
  }

  // Respect trigger limit if any (0 means unlimited)
  const bool under_limit
    = (trigger_limit_ == 0) || (trigger_count_ < trigger_limit_);
  if (!under_limit) {
    return false;
  }

  // On transition Idle -> Ongoing, optionally trigger immediately
  const bool just_started = (GetPreviousState() == State::kIdle) && IsOngoing();
  if (just_started && trigger_on_start_) {
    ++trigger_count_;
    accum_since_last_ = {};
    return true;
  }

  // Compute current effective interval (apply optional ramp)
  time_since_actuation_ += delta_time;
  auto effective_interval = interval_;
  if (ramp_enabled_
    && static_cast<std::chrono::nanoseconds>(ramp_duration_)
      > std::chrono::nanoseconds::zero()) {
    const auto t = std::clamp(
      static_cast<float>(
        static_cast<std::chrono::nanoseconds>(time_since_actuation_).count())
        / static_cast<float>(
          static_cast<std::chrono::nanoseconds>(ramp_duration_).count()),
      0.0F, 1.0F);
    const auto start_ns = static_cast<float>(
      static_cast<std::chrono::nanoseconds>(ramp_start_).count());
    const auto end_ns = static_cast<float>(
      static_cast<std::chrono::nanoseconds>(ramp_end_).count());
    const auto lerp_ns = start_ns + ((end_ns - start_ns) * t);
    effective_interval = oxygen::time::CanonicalDuration {
      std::chrono::nanoseconds { static_cast<int64_t>(lerp_ns) }
    };
  }

  // Accumulate delta toward the next interval
  accum_since_last_ += delta_time;

  // Windowed triggering: fire if within [interval - tolerance, interval +
  // tolerance]
  const auto target = effective_interval;
  const auto tolerance = jitter_tolerance_;
  bool fired = false;
  if (accum_since_last_ + tolerance >= target) {
    // Fire if we're not far overdue. We drop pulses only when the frame is
    // significantly late (>= 2x the interval), which avoids bursty behavior
    // but keeps slightly-late frames responsive.
    const auto far_overdue = oxygen::time::CanonicalDuration {
      static_cast<std::chrono::nanoseconds>(target) * 2
    };
    if (accum_since_last_ < far_overdue) {
      // on-time or slightly late
      ++trigger_count_;
      fired = true;
      // Carry over overshoot if any
      if (phase_align_) {
        leftover_ = accum_since_last_ - target;
      } else {
        leftover_ = {};
      }
      accum_since_last_ = leftover_;
    } else {
      // Too late: drop overdue pulse(s) for this frame and re-quantize phase
      // without emitting a trigger now. This lets long frames advance
      // progression without causing an immediate tick; the next shorter frame
      // will fire if it reaches the (possibly reduced) interval.
      if (phase_align_) {
        leftover_ = accum_since_last_ % target;
      } else {
        leftover_ = {};
      }
      accum_since_last_ = leftover_;
    }
  }

  // Clamp to one trigger per update
  if (fired) {
    return true;
  }

  return false;
}

// --- ActionTriggerPulse optional controls ---------------------------------

void ActionTriggerPulse::SetJitterTolerance(const float seconds)
{
  jitter_tolerance_ = oxygen::time::CanonicalDuration {
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<float>(seconds))
  };
}

void ActionTriggerPulse::EnablePhaseAlignment(const bool enable)
{
  phase_align_ = enable;
}

void ActionTriggerPulse::SetRateRamp(const PulseRateRamp& ramp)
{
  ramp_start_ = oxygen::time::CanonicalDuration {
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<float>(ramp.start_interval_seconds))
  };
  ramp_end_ = oxygen::time::CanonicalDuration {
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<float>(ramp.end_interval_seconds))
  };
  ramp_duration_ = oxygen::time::CanonicalDuration {
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<float>(ramp.duration_seconds))
  };
  ramp_enabled_ = static_cast<std::chrono::nanoseconds>(ramp_duration_)
    > std::chrono::nanoseconds::zero();
}

//-- ActionTriggerTap ----------------------------------------------------------

auto ActionTriggerTap::DoUpdateState(const ActionValue& action_value,
  const oxygen::time::CanonicalDuration delta_time) -> bool
{
  ActionTriggerTimed::DoUpdateState(action_value, delta_time);
  // Trigger only on true release (Ongoing -> Idle) within the tap threshold.
  // Only consider as a tap if we were previously Ongoing (i.e., actually
  // pressed) before this release.
  return !action_value.IsActuated(GetActuationThreshold())
    && GetPreviousState() == State::kOngoing && GetHeldDuration() <= threshold_;
}

//-- ActionTriggerChord --------------------------------------------------------

void ActionTriggerChord::SetChordAction(std::shared_ptr<Action> action)
{
  chord_action_ = std::move(action);
}

auto ActionTriggerChord::DoUpdateState(const ActionValue& /*action_value*/,
  oxygen::time::CanonicalDuration /*delta_time*/) -> bool
{
  // The chord action is held while it is ongoing; it may also trigger
  // without holding (a press), which counts for that frame.
  if (chord_action_
    && (chord_action_->IsOngoing() || chord_action_->IsTriggered())) {
    SetTriggerState(State::kOngoing);
    return true;
  }
  SetTriggerState(State::kIdle);
  return false;
}

//-- ActionTriggerChain --------------------------------------------------------

void ActionTriggerChain::SetLinkedAction(std::shared_ptr<Action> action)
{
  linked_action_ = std::move(action);
}

auto ActionTriggerChain::DoUpdateState(const ActionValue& action_value,
  const oxygen::time::CanonicalDuration delta_time) -> bool
{
  // Chain is a gate: it becomes active only if the prerequisite
  // (linked_action_) is active. Once active, Chain evaluates its own condition
  // (local press edge).
  if (!linked_action_) {
    SetTriggerState(State::kIdle);
    prev_actuated_ = false;
    armed_ = false;
    window_elapsed_ = {};
    disarmed_until_idle_ = false;
    return false;
  }

  // If prerequisite is idle, chain is idle and does not evaluate
  if (linked_action_->IsIdle()) {
    SetTriggerState(State::kIdle);
    prev_actuated_ = false;
    armed_ = false;
    window_elapsed_ = {};
    disarmed_until_idle_ = false; // allow re-arming after going idle
    return false;
  }

  // Arm only once the prerequisite has actually triggered; holding it
  // without triggering keeps the gate as it is.
  if (!disarmed_until_idle_ && linked_action_->IsTriggered()) {
    if (!armed_) {
      window_elapsed_ = {};
    }
    armed_ = true;
  }

  // If not armed yet, remain ongoing but do not evaluate local press
  SetTriggerState(State::kOngoing);
  if (!armed_) {
    prev_actuated_ = false;
    return false;
  }

  // Track max-delay window if enabled
  if (static_cast<std::chrono::nanoseconds>(max_delay_)
    > std::chrono::nanoseconds::zero()) {
    window_elapsed_ += delta_time;
    if (static_cast<std::chrono::nanoseconds>(window_elapsed_)
      > static_cast<std::chrono::nanoseconds>(max_delay_)) {
      // Expire armed state until prerequisite triggers again
      armed_ = false;
      disarmed_until_idle_ = true;
      prev_actuated_ = false;
      window_elapsed_ = {};
      return false;
    }
  }

  // Local condition: simple press on this action's input value. We need the
  // value, so we cannot ignore the parameter. Re-query via a temporary; we'll
  // accept bool axis. Note: ActionValue is a value carrier; for press detection
  // we check actuation threshold. Since the signature ignored action_value
  // previously, adjust to use it. We treat a rising edge as trigger, do not
  // auto-repeat while held.
  const bool actuated = action_value.IsActuated(GetActuationThreshold());
  bool fired = false;
  // Optional: require prerequisite to still be held at the instant of press
  if (actuated && !prev_actuated_
    && (!require_prereq_held_ || linked_action_->IsOngoing())) {
    fired = true;
    // Once fired, reset the arm; require prerequisite to re-trigger for next
    // chain
    armed_ = false;
    window_elapsed_ = {};
  }
  prev_actuated_ = actuated;
  return fired;
}

void ActionTriggerChain::SetMaxDelaySeconds(const float seconds)
{
  max_delay_ = oxygen::time::CanonicalDuration {
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<float>(seconds))
  };
}

void ActionTriggerChain::RequirePrerequisiteHeld(const bool enable)
{
  require_prereq_held_ = enable;
}

//-- ActionTriggerCombo --------------------------------------------------------

void ActionTriggerCombo::AddComboStep(std::shared_ptr<Action> action,
  const ActionState completion_states, const float time_to_complete_seconds)
{
  assert(action);
  if (action) {
    combo_steps_.push_back({
      .action = std::move(action),
      .completion_states = completion_states,
      .time_to_complete
      = oxygen::time::CanonicalDuration { std::chrono::duration_cast<
        std::chrono::nanoseconds>(
        std::chrono::duration<float>(time_to_complete_seconds)) },
    });
  }
}

void ActionTriggerCombo::RemoveComboStep(const uint32_t index)
{
  assert(index < combo_steps_.size());
  if (index < combo_steps_.size()) {
    combo_steps_.erase(combo_steps_.begin() + index);
  }
}

void ActionTriggerCombo::ClearComboSteps() { combo_steps_.clear(); }

void ActionTriggerCombo::AddComboBreaker(
  std::shared_ptr<Action> action, const ActionState completion_states)
{
  assert(action);
  if (action) {
    combo_breakers_.push_back(
      { .action = std::move(action), .completion_states = completion_states });
  }
}

void ActionTriggerCombo::RemoveComboBreaker(const uint32_t index)
{
  assert(index < combo_breakers_.size());
  if (index < combo_breakers_.size()) {
    combo_breakers_.erase(combo_breakers_.begin() + index);
  }
}

void ActionTriggerCombo::ClearComboBreakers() { combo_breakers_.clear(); }

auto ActionTriggerCombo::DoUpdateState(const ActionValue& /*action_value*/,
  const oxygen::time::CanonicalDuration delta_time) -> bool
{
  if (combo_steps_.empty()) {
    return false;
  }

  // Helper: did the action produce any transition this frame to a state that
  // matches the requested mask? We use frame transitions (edges) rather than
  // sticky current states so that timeouts truly reset progress and require
  // new input events to advance.
  const auto cursor_for = [this](const Action& action) -> Cursor& {
    auto it = std::ranges::find_if(cursors_,
      [&action](const Cursor& c) -> bool { return c.action == &action; });
    if (it == cursors_.end()) {
      cursors_.push_back(Cursor { .action = &action });
      it = std::prev(cursors_.end());
    }
    if (it->frame_serial != action.GetFrameSerial()) {
      it->frame_serial = action.GetFrameSerial();
      it->seen = 0;
    }
    return *it;
  };
  // Only transitions not yet evaluated by this combo count.
  const auto occurred_this_frame
    = [&cursor_for](
        const std::shared_ptr<Action>& action, const ActionState mask) -> bool {
    const auto transitions = action->GetFrameTransitions();
    const auto& cursor = cursor_for(*action);
    return std::ranges::any_of(
      transitions.subspan(std::min(cursor.seen, transitions.size())),
      [mask](const auto& tr) -> bool {
        return (tr.to_state & mask) != ActionState::kNone;
      });
  };
  // After this evaluation, mark every observed transition as seen.
  const auto mark_seen = [&] -> void {
    for (const auto& step : combo_steps_) {
      cursor_for(*step.action).seen = step.action->GetFrameTransitions().size();
    }
    for (const auto& breaker : combo_breakers_) {
      cursor_for(*breaker.action).seen
        = breaker.action->GetFrameTransitions().size();
    }
  };

  // Check for any combo breaker that fired
  for (const auto& [action, completion_states] : combo_breakers_) {
    if (occurred_this_frame(action, completion_states)) {
      // Reset combo
      current_step_index_ = 0;
      break;
    }
  }
  auto current_step = combo_steps_.at(current_step_index_);

  // Check if a combo action fired out of order
  for (size_t i = 0; i < combo_steps_.size(); ++i) {
    if (i == current_step_index_) {
      continue; // ignore the current expected step
    }
    // Ignore already-completed previous steps; only future steps out-of-order
    // reset
    if (i < current_step_index_) {
      continue;
    }
    const auto& step = combo_steps_.at(i);
    if (occurred_this_frame(step.action, step.completion_states)) {
      // Reset combo on out-of-order future step
      current_step_index_ = 0;
      waited_time_ = {};
      current_step = combo_steps_.at(current_step_index_);
      break;
    }
  }

  // Reset the combo if the step took too long to complete; ignore timeout for
  // first step.
  if (current_step_index_ > 0) {
    waited_time_ += delta_time;
    if (waited_time_ > current_step.time_to_complete) {
      // Reset combo
      current_step_index_ = 0;
      waited_time_ = {};
      current_step = combo_steps_.at(current_step_index_);
    }
  }

  if (occurred_this_frame(
        current_step.action, current_step.completion_states)) {
    current_step_index_++;
    waited_time_ = {};
    if (current_step_index_ == combo_steps_.size()) {
      current_step_index_ = 0;
      SetTriggerState(State::kIdle);
      mark_seen();
      return true;
    }
  }

  SetTriggerState(State::kOngoing);
  mark_seen();
  return false;
}
