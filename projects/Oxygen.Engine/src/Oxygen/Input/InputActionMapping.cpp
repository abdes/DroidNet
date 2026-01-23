//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <type_traits>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>

using oxygen::input::InputActionMapping;
using oxygen::platform::InputSlots;

InputActionMapping::InputActionMapping(
  std::shared_ptr<Action> action, const platform::InputSlot& input_slot)
  : action_(std::move(action))
  , slot_(input_slot)
{
}

void InputActionMapping::StartEvaluation()
{
  // Reset local evaluation aggregates; Action itself has per-frame lifecycle
  // maintained by InputSystem::BeginFrameTracking/EndFrameTracking.
  switch (action_->GetValueType()) {
  case ActionValueType::kBool:
    action_value_.Set(false);
    break;
  case ActionValueType::kAxis1D:
    action_value_.Set(Axis1D { 0.0F });
    break;
  case ActionValueType::kAxis2D:
    action_value_.Set(Axis2D { .x = 0.0F, .y = 0.0F });
    break;
  }
  evaluation_ongoing_ = true;
  any_explicit_triggered_ = false;
  any_explicit_ongoing_ = false;
  all_implicits_triggered_ = true;
  blocked_ = false;
}

void InputActionMapping::NotifyActionCanceled()
{
  DLOG_F(2, "action {} canceled", action_->GetName());
  // When canceled, clear the action's value to its zero/default state
  ActionValue zero_value;
  switch (action_->GetValueType()) {
  case ActionValueType::kBool:
    zero_value.Update(false);
    break;
  case ActionValueType::kAxis1D:
    zero_value.Update(Axis1D { 0.0F });
    break;
  case ActionValueType::kAxis2D:
    zero_value.Update(Axis2D { .x = 0.0F, .y = 0.0F });
    break;
  }
  action_->UpdateState(
    Action::State {
      .triggered = false,
      .ongoing = false,
      .completed = false,
      .canceled = true,
    },
    zero_value);
  evaluation_ongoing_ = false;
}

void InputActionMapping::AddTrigger(std::shared_ptr<ActionTrigger> trigger)
{
  triggers_.push_back(std::move(trigger));
}

void InputActionMapping::HandleInput(const platform::InputEvent& event)
{
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

  using platform::ButtonState;
  using platform::input::ButtonStateComponent;
  using platform::input::MouseMotionComponent;
  using platform::input::MouseWheelComponent;

  if (const TypeId event_type = event.GetTypeId();
    event_type == platform::KeyEvent::ClassTypeId()
    || event_type == platform::MouseButtonEvent::ClassTypeId()) {
    DCHECK_F(event.HasComponent<ButtonStateComponent>());
    const auto& comp = event.GetComponent<ButtonStateComponent>();
    action_value_.Update(comp.GetState() == ButtonState::kPressed);
  } else if (event_type == platform::MouseMotionEvent::ClassTypeId()) {
    DCHECK_F(event.HasComponent<MouseMotionComponent>());
    const auto& comp = event.GetComponent<MouseMotionComponent>();
    const auto& [dx, dy] = comp.GetMotion();
    // Respect mapping slot: MouseX/MouseY/MouseXY
    if (slot_ == InputSlots::MouseX) {
      action_value_.Update(Axis1D { dx });
    } else if (slot_ == InputSlots::MouseY) {
      action_value_.Update(Axis1D { dy });
    } else {
      action_value_.Update({ .x = dx, .y = dy });
    }
    clear_value_after_update_ = true;
  } else if (event_type == platform::MouseWheelEvent::ClassTypeId()) {
    DCHECK_F(event.HasComponent<MouseWheelComponent>());
    const auto& comp = event.GetComponent<MouseWheelComponent>();
    const auto& [dx, dy] = comp.GetScrollAmount();
    if (slot_ == InputSlots::MouseWheelXY) {
      action_value_.Update({ .x = dx, .y = dy });
    } else if (slot_ == InputSlots::MouseWheelX
      || slot_ == InputSlots::MouseWheelLeft
      || slot_ == InputSlots::MouseWheelRight) {
      action_value_.Update(Axis1D { dx });
    } else if (slot_ == InputSlots::MouseWheelY
      || slot_ == InputSlots::MouseWheelUp
      || slot_ == InputSlots::MouseWheelDown) {
      action_value_.Update(Axis1D { dy });
    }
    clear_value_after_update_ = true;
  }

  // TODO(abdes) Call any value transformers on the initial value
}

void InputActionMapping::CancelInput()
{
  event_processing_ = false;
  action_value_ = last_action_value_;
  // Reset all triggers to clear any accumulated state (e.g., held duration)
  for (const auto& trigger : triggers_) {
    trigger->Reset();
  }
  // Mark canceled for this evaluation
  NotifyActionCanceled();
}

void InputActionMapping::AbortStaged() noexcept
{
  // Drop any staged input without producing Action edges. This prevents
  // stale event_processing_ or evaluation_ongoing_ from leaking into next
  // frames when a higher-priority context consumes input.
  event_processing_ = false;
  evaluation_ongoing_ = false;
  clear_value_after_update_ = false;
  // Reset all triggers to clear any accumulated state
  for (const auto& trigger : triggers_) {
    trigger->Reset();
  }
}

auto InputActionMapping::Update(
  const oxygen::time::CanonicalDuration delta_time) -> bool
{
  const auto input_consumed = DoUpdate(delta_time);

  if (clear_value_after_update_) {
    switch (action_->GetValueType()) {
    case ActionValueType::kBool:
      action_value_.Update(false);
      break;
    case ActionValueType::kAxis1D:
      action_value_.Update(Axis1D { 0.0F });
      break;
    case ActionValueType::kAxis2D:
      action_value_.Update(Axis2D { .x = 0.0F, .y = 0.0F });
      break;
    }
    clear_value_after_update_ = false;
  }

  return input_consumed;
}

auto InputActionMapping::DoUpdate(
  const oxygen::time::CanonicalDuration delta_time) -> bool
{
  // If the mapping has no triggers, it cannot and should not do anything with
  // the input events and state updates.
  if (triggers_.empty() || !evaluation_ongoing_) {
    return false;
  }

  // Recompute aggregation fresh for this update
  any_explicit_triggered_ = false;
  any_explicit_ongoing_ = false;
  all_implicits_triggered_ = true;
  blocked_ = false;

  // Detect if this mapping contains any explicit triggers at all. If yes,
  // we require at least one explicit to fire to allow triggering.
  bool has_explicit_triggers = false;
  for (const auto& t : triggers_) {
    if (t->IsExplicit()) {
      has_explicit_triggers = true;
      break;
    }
  }

  bool any_trigger_ongoing = false;
  // Implicit gating policy:
  // - For non-chain implicits (e.g., Hold, Tap windows), require Triggered
  //   in the same update; Ongoing alone is not sufficient.
  // - For chain implicits, allow the prerequisite gate to be satisfied by
  //   Triggered or Ongoing (armed) since it represents a prerequisite state
  //   rather than a punctual edge.
  bool all_non_chain_implicits_triggered = true;
  bool all_chain_implicits_ok = true;
  bool canceled = false;
  for (const auto& trigger : triggers_) {
    // Always evaluate implicit/blocker triggers even without fresh input so
    // they can react to time or external action state (e.g., chains/holds).
    const bool should_evaluate = event_processing_ || trigger->IsOngoing()
      || trigger->IsImplicit() || trigger->IsBlocker();
    if (!should_evaluate) {
      continue;
    }

    trigger->UpdateState(action_value_, delta_time);

    if (trigger->IsExplicit()) {
      any_explicit_triggered_ |= trigger->IsTriggered();
      any_explicit_ongoing_ |= trigger->IsOngoing();
      if (trigger->IsCanceled()) {
        canceled = true;
      }
    } else if (trigger->IsImplicit()) {
      const bool tr = trigger->IsTriggered();
      const bool on = trigger->IsOngoing();
      if (trigger->GetType() == ActionTriggerType::kActionChain) {
        // Chains act as prerequisite gates; accept armed (ongoing) or edge.
        all_chain_implicits_ok &= (tr || on);
      } else {
        // Time/value-based implicits must fire this update.
        all_non_chain_implicits_triggered &= tr;
      }
    } else if (trigger->IsBlocker()) {
      blocked_ |= trigger->IsTriggered();
    }
    any_trigger_ongoing |= trigger->IsOngoing();
  }

  const bool handling_input = event_processing_;
  event_processing_ = false;

  if (canceled) {
    // Short-circuit: preserve canceled edge without overwriting later
    NotifyActionCanceled();
    return false;
  }

  if (blocked_) {
    // Blocked: set idle snapshot (no edges), end evaluation
    action_->UpdateState(
      Action::State {
        .triggered = false,
        .ongoing = false,
        .completed = false,
        .canceled = false,
      },
      action_value_);
    evaluation_ongoing_ = false;
    return false;
  }

  bool input_consumed { false };
  // Aggregate final snapshot for this evaluation
  const bool allow_explicit
    = has_explicit_triggers ? any_explicit_triggered_ : true;

  // Implicit gating: all non-chain implicits must Trigger this update; chain
  // gates are satisfied by Triggered or Ongoing (armed prerequisite).
  const bool implicits_ok
    = all_non_chain_implicits_triggered && all_chain_implicits_ok;
  bool should_trigger = allow_explicit && implicits_ok;
  // Only consider the action ongoing if explicit input is ongoing AND all
  // implicit gates (e.g., chains/holds) are satisfied. This ensures level
  // state aligns with gating semantics.
  const bool action_ongoing = any_explicit_ongoing_ && implicits_ok;

  // Aggregate final snapshot for this update without preserving prior frame
  // edges at the mapping level. The InputSystem frame lifecycle is
  // responsible for edge visibility semantics across micro-updates.
  action_->UpdateState(
    Action::State {
      .triggered = should_trigger,
      .ongoing = action_ongoing,
      .completed = false,
      .canceled = false,
    },
    action_value_);

  input_consumed = should_trigger && handling_input && action_->ConsumesInput();
  // Keep evaluation running while any trigger (including implicits) is ongoing
  if (!any_trigger_ongoing) {
    evaluation_ongoing_ = false;
  }
  return input_consumed;
}
