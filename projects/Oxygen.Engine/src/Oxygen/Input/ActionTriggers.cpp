//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Input/ActionTriggers.h>

#include <cassert>

#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionValue.h>

using oxygen::input::ActionTrigger;
using oxygen::input::ActionTriggerChain;
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

void ActionTrigger::UpdateState(
  const ActionValue& action_value, const Duration delta_time)
{
  triggered_ = DoUpdateState(action_value, delta_time);
}

//-- ActionTriggerPressed ------------------------------------------------------

auto ActionTriggerPressed::DoUpdateState(
  const ActionValue& action_value, Duration /*delta_time*/) -> bool
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

auto ActionTriggerReleased::DoUpdateState(
  const ActionValue& action_value, Duration /*delta_time*/) -> bool
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

auto ActionTriggerDown::DoUpdateState(
  const ActionValue& action_value, Duration /*delta_time*/) -> bool
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

auto ActionTriggerTimed::DoUpdateState(
  const ActionValue& action_value, const Duration delta_time) -> bool
{
  if (action_value.IsActuated(GetActuationThreshold())) {
    if (IsIdle() || (IsTriggered() && IsOngoing())) {
      held_duration_ = Duration::zero();
      SetTriggerState(State::kOngoing);
    }
    held_duration_ += delta_time;
  } else {
    SetTriggerState(State::kIdle);
  }
  return false;
}

//-- ActionTriggerHold ---------------------------------------------------------

auto ActionTriggerHold::DoUpdateState(
  const ActionValue& action_value, const Duration delta_time) -> bool
{
  if (IsCompleted()) {
    triggered_once_ = false;
  }
  ActionTriggerTimed::DoUpdateState(action_value, delta_time);
  if (GetHeldDuration() >= hold_duration_threshold_) {
    if (!triggered_once_ || !IsOneShot()) {
      triggered_once_ = true;
      return true;
    }
  }
  return false;
}

//-- ActionTriggerHoldAndRelease -----------------------------------------------

auto ActionTriggerHoldAndRelease::DoUpdateState(
  const ActionValue& action_value, const Duration delta_time) -> bool
{
  ActionTriggerTimed::DoUpdateState(action_value, delta_time);
  if (!action_value.IsActuated(GetActuationThreshold())) {
    if (GetHeldDuration() >= hold_duration_threshold_) {
      return true;
    }
  }
  return false;
}

//-- ActionTriggerPulse --------------------------------------------------------

auto ActionTriggerPulse::DoUpdateState(
  const ActionValue& action_value, const Duration delta_time) -> bool
{
  if (IsIdle() && GetPreviousState() == State::kIdle) {
    trigger_count_ = 0;
  }
  if (trigger_on_start_) {
    ++trigger_count_;
    return true;
  }
  ActionTriggerTimed::DoUpdateState(action_value, delta_time);
  if (IsOngoing() && (GetHeldDuration() >= interval_)
    && ((trigger_limit_ == 0) || (trigger_count_ < trigger_limit_))) {
    ++trigger_count_;
    return true;
  }
  return false;
}

//-- ActionTriggerTap ----------------------------------------------------------

auto ActionTriggerTap::DoUpdateState(
  const ActionValue& action_value, const Duration delta_time) -> bool
{
  ActionTriggerTimed::DoUpdateState(action_value, delta_time);
  if (!action_value.IsActuated(GetActuationThreshold())) {
    if (GetHeldDuration() <= threshold_) {
      return true;
    }
  }
  return false;
}

//-- ActionTriggerChain --------------------------------------------------------

void ActionTriggerChain::SetLinkedAction(std::shared_ptr<Action> action)
{
  linked_action_ = std::move(action);
}

auto ActionTriggerChain::DoUpdateState(
  const ActionValue& /*action_value*/, Duration /*delta_time*/) -> bool
{
  if (linked_action_) {
    const bool triggered = linked_action_->IsTriggered();
    if (linked_action_->IsIdle() || triggered) {
      SetTriggerState(State::kIdle);
    } else if (linked_action_->IsOngoing()) {
      SetTriggerState(State::kOngoing);
    }
    return triggered;
  }
  return false;
}

//-- ActionTriggerCombo --------------------------------------------------------

void ActionTriggerCombo::AddComboStep(std::shared_ptr<Action> action,
  const ActionState completion_states, const float time_to_complete_seconds)
{
  assert(action);
  if (action) {
    combo_steps_.push_back({ .action = std::move(action),
      .completion_states = completion_states,
      .time_to_complete = SecondsToDuration(time_to_complete_seconds) });
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

auto ActionTriggerCombo::DoUpdateState(
  const ActionValue& /*action_value*/, const Duration delta_time) -> bool
{
  if (combo_steps_.empty()) {
    return false;
  }

  // Check for any combo breaker that fired
  for (const auto& [action, completion_states] : combo_breakers_) {
    if ((action->GetCurrentStates() & completion_states)
      != ActionState::kNone) {
      // Reset combo
      current_step_index_ = 0;
      break;
    }
  }
  auto current_step = combo_steps_[current_step_index_];

  // Check if a combo action fired out of order
  for (const auto& [action, completion_states, time_to_complete] :
    combo_steps_) {
    if ((action != current_step.action)
      && ((action->GetCurrentStates() & completion_states)
        != ActionState::kNone)) {
      // Reset combo
      current_step_index_ = 0;
      current_step = combo_steps_[current_step_index_];
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
      current_step = combo_steps_[current_step_index_];
    }
  }

  if ((current_step.action->GetCurrentStates() & current_step.completion_states)
    != ActionState::kNone) {
    current_step_index_++;
    waited_time_ = Duration::zero();
    if (current_step_index_ == combo_steps_.size()) {
      current_step_index_ = 0;
      SetTriggerState(State::kIdle);
      return true;
    }
  }

  SetTriggerState(State::kOngoing);
  return false;
}
