//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "oxygen/input/input_action_mapping.h"

#include <chrono>
#include <utility>

#include "oxygen/base/logging.h"
#include "oxygen/input/action.h"
#include "oxygen/input/types.h"
#include "oxygen/platform/input.h"
#include "oxygen/platform/input_event.h"

using oxygen::input::InputActionMapping;
using oxygen::platform::InputSlots;

InputActionMapping::InputActionMapping(std::shared_ptr<Action> action,
                                       const platform::InputSlot& input_slot)
  : action_(std::move(action)), slot_(input_slot) {
}

void InputActionMapping::StartEvaluation() {
  // Reset Action value
  switch (action_->GetValueType()) {
  case ActionValueType::kBool:
    action_value_.Set(false);
    break;
  case ActionValueType::kAxis1D:
    action_value_.Set(Axis1D{ 0.0F });
    break;
  case ActionValueType::kAxis2D:
    action_value_.Set(Axis2D{ 0.0F, 0.0F });
    break;
  }
  DLOG_F(2, "action {} triggers evaluation started", action_->GetName());
  action_->OnStarted()(*action_);
  evaluation_ongoing_ = true;
  found_explicit_trigger_ = false;
  any_explicit_triggered_ = false;
  all_implicits_triggered_ = true;
  blocked_ = false;
}

void InputActionMapping::NotifyActionCanceled() {
  DLOG_F(2, "action {} cancelled", action_->GetName());
  action_->OnCanceled()(*action_);
  CompleteEvaluation();
}
void InputActionMapping::NotifyActionTriggered() {
  LOG_F(INFO, "===> action triggered : {}", action_->GetName());
  action_->OnTriggered()(*action_, action_value_);
  any_explicit_triggered_ = false;
  all_implicits_triggered_ = true;
}
void InputActionMapping::NotifyActionOngoing() {
  DLOG_F(2, "action {} trigger evaluation ongoing", action_->GetName());
  action_->OnOngoing()(*action_);
  action_ongoing_ = true;
}

void InputActionMapping::CompleteEvaluation() {
  DLOG_F(2, "action {} trigger evaluation completed", action_->GetName());
  action_->OnCompleted()(*action_);
  evaluation_ongoing_ = false;
  action_ongoing_ = false;
}

void InputActionMapping::AddTrigger(std::shared_ptr<ActionTrigger> trigger) {
  triggers_.push_back(std::move(trigger));
}

void InputActionMapping::HandleInput(const platform::InputEvent& event) {
  // If the mapping has no triggers, it cannot and should not do anything with
  // the input events and state updates.
  if (triggers_.empty()) {
    return;
  }

  event_processing_ = true;

  if (!evaluation_ongoing_) {
    StartEvaluation();
  }

  // save action value in case we need to cancel the input
  last_action_value_ = action_value_;

  switch (event.GetType()) {
  case platform::InputEventType::kKeyEvent: {
    const auto& k_event = dynamic_cast<const platform::KeyEvent&>(event);
    action_value_.Update(k_event.GetButtonState()
                         == platform::ButtonState::kPressed);
  } break;
  case platform::InputEventType::kMouseButtonEvent: {
    const auto& mb_event =
      dynamic_cast<const platform::MouseButtonEvent&>(event);
    action_value_.Update(mb_event.GetButtonState()
                         == platform::ButtonState::kPressed);
  } break;
  case platform::InputEventType::kMouseMotionEvent: {
    const auto& mm_event =
      dynamic_cast<const platform::MouseMotionEvent&>(event);
    action_value_.Update(
      { .x = mm_event.GetMotion().dx, .y = mm_event.GetMotion().dy });
    clear_value_after_update_ = true;
  } break;
  case platform::InputEventType::kMouseWheelEvent: {
    const auto& mw_event =
      dynamic_cast<const platform::MouseWheelEvent&>(event);
    if (slot_ == InputSlots::MouseWheelXY) {
      action_value_.Update({ .x = mw_event.GetScrollAmount().dx,
                            .y = mw_event.GetScrollAmount().dy });
    }
    else if (slot_ == InputSlots::MouseWheelX
             || slot_ == InputSlots::MouseWheelLeft
             || slot_ == InputSlots::MouseWheelRight) {
      action_value_.Update(Axis1D{ mw_event.GetScrollAmount().dx });
    }
    else if (slot_ == InputSlots::MouseWheelY
             || slot_ == InputSlots::MouseWheelUp
             || slot_ == InputSlots::MouseWheelDown) {
      action_value_.Update(Axis1D{ mw_event.GetScrollAmount().dy });
    }
    clear_value_after_update_ = true;
  } break;
  }

  // TODO(abdes) Call any value transformers on the initial value
}

void InputActionMapping::CancelInput() {
  event_processing_ = false;
  action_value_ = last_action_value_;
  CompleteEvaluation();
}

auto InputActionMapping::Update(oxygen::Duration delta_time) -> bool {
  const auto input_consumed = DoUpdate(delta_time);

  if (clear_value_after_update_) {
    action_value_.Update({ 0.0F, 0.0F });
    clear_value_after_update_ = false;
  }

  return input_consumed;
}

auto InputActionMapping::DoUpdate(Duration delta_time) -> bool {
  // If the mapping has no triggers, it cannot and should not do anything with
  // the input events and state updates.
  if (triggers_.empty() || !evaluation_ongoing_) {
    return false;
  }

  trigger_ongoing_ = false;
  any_explicit_ongoing_ = false;

  for (const auto& trigger : triggers_) {
    if (!event_processing_ && !trigger->IsOngoing()) {
      continue;
    }

    trigger->UpdateState(action_value_, delta_time);

    if (trigger->IsExplicit()) {
      found_explicit_trigger_ = true;
      any_explicit_triggered_ |= trigger->IsTriggered();
      any_explicit_ongoing_ |= trigger->IsOngoing();
      if (trigger->IsCanceled())
        NotifyActionCanceled();
    }
    else if (trigger->IsImplicit()) {
      all_implicits_triggered_ &= trigger->IsTriggered();
    }
    else if (trigger->IsBlocker()) {
      blocked_ |= trigger->IsTriggered();
    }
    trigger_ongoing_ |= trigger->IsOngoing();
  }

  const bool handling_input = event_processing_;
  event_processing_ = false;

  if (blocked_) {
    CompleteEvaluation();
    return false;
  }

  bool input_consumed{ false };

  if ((!found_explicit_trigger_ || any_explicit_triggered_)
      && all_implicits_triggered_) {
    NotifyActionTriggered();
    input_consumed = (handling_input && action_->ConsumesInput());
    if (input_consumed) {
      DLOG_F(2, "Input was consumed by action: {}", action_->GetName());
    }
  }

  if (!any_explicit_ongoing_) {
    CompleteEvaluation();
  }
  else {
    NotifyActionOngoing();
  }
  return input_consumed;
}
