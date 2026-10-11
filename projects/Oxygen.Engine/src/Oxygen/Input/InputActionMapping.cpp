//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>

using oxygen::Axis1D;
using oxygen::Axis2D;
using oxygen::input::Action;
using oxygen::input::ActionValue;
using oxygen::input::ActionValueType;
using oxygen::input::InputActionMapping;
using oxygen::platform::InputSlots;

namespace {

//! Convert a transformed 2D sample to the action's value type.
auto ToActionValue(const Axis2D& v, const ActionValueType type) -> ActionValue
{
  switch (type) {
  case ActionValueType::kAxis1D:
    return ActionValue { Axis1D { .x = v.x } };
  case ActionValueType::kAxis2D:
    return ActionValue { v };
  case ActionValueType::kBool:
    break;
  }
  return ActionValue { v.x != 0.0F || v.y != 0.0F };
}

} // namespace

InputActionMapping::InputActionMapping(
  std::shared_ptr<Action> action, platform::InputSlot input_slot)
  : action_(std::move(action))
  , slot_(std::move(input_slot))
  , action_value_(ToActionValue({}, action_->GetValueType()))
  , last_action_value_(action_value_)
{
}

auto InputActionMapping::IsRelative() const -> bool
{
  return slot_ == InputSlots::MouseX || slot_ == InputSlots::MouseY
    || slot_ == InputSlots::MouseXY || slot_ == InputSlots::MouseWheelX
    || slot_ == InputSlots::MouseWheelY || slot_ == InputSlots::MouseWheelXY
    || slot_ == InputSlots::MouseWheelUp || slot_ == InputSlots::MouseWheelDown
    || slot_ == InputSlots::MouseWheelLeft
    || slot_ == InputSlots::MouseWheelRight;
}

void InputActionMapping::AddTrigger(std::shared_ptr<ActionTrigger> trigger)
{
  triggers_.push_back(std::move(trigger));
}

void InputActionMapping::StartEvaluation()
{
  action_value_ = ToActionValue({}, action_->GetValueType());
  evaluation_ongoing_ = true;
}

void InputActionMapping::SetSample(const Axis2D& sample)
{
  const bool relative = IsRelative();
  Axis2D v { .x = sample.x * scale_.x, .y = sample.y * scale_.y };
  if (!relative) {
    v.x += bias_.x;
    v.y += bias_.y;
  }
  action_value_ = ToActionValue(v, action_->GetValueType());

  if (relative) {
    SyncRelativeFrame();
    relative_pending_.x += v.x;
    relative_pending_.y += v.y;
    clear_value_after_update_ = true;
  }
}

void InputActionMapping::SyncRelativeFrame()
{
  if (relative_serial_ != action_->GetFrameSerial()) {
    relative_sum_ = {};
    relative_pending_ = {};
    relative_serial_ = action_->GetFrameSerial();
  }
}

void InputActionMapping::HandleInput(const platform::InputEvent& event)
{
  // A mapping without triggers has nothing to evaluate.
  if (triggers_.empty()) {
    return;
  }

  event_processing_ = true;
  if (!evaluation_ongoing_) {
    StartEvaluation();
  }
  // Saved so a cancel can restore the value before this event.
  last_action_value_ = action_value_;

  using platform::ButtonState;
  using platform::input::ButtonStateComponent;
  using platform::input::MouseMotionComponent;
  using platform::input::MouseWheelComponent;

  const auto event_type = event.GetTypeId();
  if (event_type == platform::KeyEvent::ClassTypeId()
    || event_type == platform::MouseButtonEvent::ClassTypeId()) {
    DCHECK_F(event.HasComponent<ButtonStateComponent>());
    const float s = event.GetComponent<ButtonStateComponent>().GetState()
        == ButtonState::kPressed
      ? 1.0F
      : 0.0F;
    SetSample({ .x = s, .y = s });
  } else if (event_type == platform::MouseMotionEvent::ClassTypeId()) {
    DCHECK_F(event.HasComponent<MouseMotionComponent>());
    const auto [dx, dy]
      = event.GetComponent<MouseMotionComponent>().GetMotion();
    if (slot_ == InputSlots::MouseX) {
      SetSample({ .x = dx, .y = dx });
    } else if (slot_ == InputSlots::MouseY) {
      SetSample({ .x = dy, .y = dy });
    } else {
      SetSample({ .x = dx, .y = dy });
    }
  } else if (event_type == platform::MouseWheelEvent::ClassTypeId()) {
    DCHECK_F(event.HasComponent<MouseWheelComponent>());
    const auto [dx, dy]
      = event.GetComponent<MouseWheelComponent>().GetScrollAmount();
    if (slot_ == InputSlots::MouseWheelXY) {
      SetSample({ .x = dx, .y = dy });
    } else if (slot_ == InputSlots::MouseWheelX
      || slot_ == InputSlots::MouseWheelLeft
      || slot_ == InputSlots::MouseWheelRight) {
      SetSample({ .x = dx, .y = dx });
    } else {
      SetSample({ .x = dy, .y = dy });
    }
  }
}

void InputActionMapping::Contribute(
  const bool triggered, const bool ongoing, const bool canceled)
{
  const auto type = action_->GetValueType();
  const bool active = triggered || ongoing;
  ActionValue value = action_value_;
  if (IsRelative() && !canceled) {
    // A relative binding reports the motion its triggers accepted this
    // frame: motion they reject (a look without its chord) moves nothing.
    SyncRelativeFrame();
    if (active) {
      relative_sum_.x += relative_pending_.x;
      relative_sum_.y += relative_pending_.y;
    }
    relative_pending_ = {};
    value = ToActionValue(relative_sum_, type);
  } else if (canceled || !active) {
    // An absolute binding contributes its value only while it is active.
    value = ToActionValue({}, type);
  }
  active_ = active;
  action_->Contribute(this,
    Action::State {
      .triggered = triggered,
      .ongoing = ongoing,
      .completed = false,
      .canceled = canceled,
    },
    value);
}

void InputActionMapping::NotifyActionCanceled()
{
  // Only an activation this binding held can be canceled.
  if (active_) {
    DLOG_F(2, "action {} canceled", action_->GetName());
    Contribute(false, false, true);
  }
  evaluation_ongoing_ = false;
}

void InputActionMapping::CancelInput()
{
  event_processing_ = false;
  action_value_ = last_action_value_;
  for (const auto& trigger : triggers_) {
    trigger->Reset();
  }
  NotifyActionCanceled();
}

void InputActionMapping::AbortStaged() noexcept
{
  event_processing_ = false;
  evaluation_ongoing_ = false;
  clear_value_after_update_ = false;
  for (const auto& trigger : triggers_) {
    trigger->Reset();
  }
}

void InputActionMapping::Deactivate()
{
  for (const auto& trigger : triggers_) {
    trigger->Reset();
  }
  if (active_) {
    Contribute(false, false, true);
  }
  event_processing_ = false;
  evaluation_ongoing_ = false;
  clear_value_after_update_ = false;
  active_ = false;
  action_value_ = ToActionValue({}, action_->GetValueType());
  action_->RemoveContribution(this);
}

auto InputActionMapping::Update(
  const oxygen::time::CanonicalDuration delta_time) -> bool
{
  const auto input_consumed = DoUpdate(delta_time);
  if (clear_value_after_update_) {
    // Relative events are deltas: the next update without an event sees zero.
    action_value_ = ToActionValue({}, action_->GetValueType());
    clear_value_after_update_ = false;
  }
  return input_consumed;
}

auto InputActionMapping::DoUpdate(
  const oxygen::time::CanonicalDuration delta_time) -> bool
{
  if (triggers_.empty()) {
    return false;
  }
  if (!evaluation_ongoing_) {
    // Chain gates watch their prerequisite and their delay window while this
    // binding has no input; otherwise the window would only start counting
    // when the chained key is pressed, and could never expire.
    for (const auto& trigger : triggers_) {
      if (trigger->GetType() == ActionTriggerType::kActionChain) {
        trigger->UpdateState(action_value_, delta_time);
      }
    }
    return false;
  }

  // With explicit triggers, at least one of them must trigger.
  const bool has_explicit_triggers = std::ranges::any_of(
    triggers_, [](const auto& t) -> bool { return t->IsExplicit(); });

  bool any_explicit_triggered = false;
  bool any_explicit_ongoing = false;
  bool blocked = false;
  bool any_trigger_ongoing = false;
  // Implicit gating: time/value implicits (Hold, Tap...) must trigger in this
  // update; chains are prerequisite gates satisfied while armed (ongoing).
  bool all_non_chain_implicits_triggered = true;
  bool all_chain_implicits_ok = true;
  bool canceled = false;
  for (const auto& trigger : triggers_) {
    // Implicit and blocker triggers are evaluated without fresh input so they
    // can react to time and to other actions' state.
    const bool should_evaluate = event_processing_ || trigger->IsOngoing()
      || trigger->IsImplicit() || trigger->IsBlocker();
    if (!should_evaluate) {
      continue;
    }

    trigger->UpdateState(action_value_, delta_time);

    if (trigger->IsExplicit()) {
      any_explicit_triggered |= trigger->IsTriggered();
      any_explicit_ongoing |= trigger->IsOngoing();
      canceled |= trigger->IsCanceled();
    } else if (trigger->IsImplicit()) {
      const bool triggered = trigger->IsTriggered();
      if (trigger->GetType() == ActionTriggerType::kActionChain) {
        all_chain_implicits_ok &= triggered || trigger->IsOngoing();
      } else {
        all_non_chain_implicits_triggered &= triggered;
      }
    } else if (trigger->IsBlocker()) {
      blocked |= trigger->IsTriggered();
    }
    any_trigger_ongoing |= trigger->IsOngoing();
  }

  const bool handling_input = event_processing_;
  event_processing_ = false;

  if (canceled) {
    NotifyActionCanceled();
    return false;
  }

  if (blocked) {
    Contribute(false, false, false);
    evaluation_ongoing_ = false;
    return false;
  }

  const bool allow_explicit = !has_explicit_triggers || any_explicit_triggered;
  const bool implicits_ok
    = all_non_chain_implicits_triggered && all_chain_implicits_ok;
  const bool should_trigger = allow_explicit && implicits_ok;
  // Ongoing only while explicit input is ongoing and the gates are satisfied.
  const bool ongoing = any_explicit_ongoing && implicits_ok;

  Contribute(should_trigger, ongoing, false);

  if (!any_trigger_ongoing) {
    evaluation_ongoing_ = false;
  }
  return should_trigger && handling_input && action_->ConsumesInput();
}
