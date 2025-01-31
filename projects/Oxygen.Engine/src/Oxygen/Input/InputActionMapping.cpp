//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <type_traits>

#include "Oxygen/Input/Action.h"
#include "Oxygen/Input/ActionTriggers.h"
#include "Oxygen/Input/ActionValue.h"
#include "Oxygen/Input/InputActionMapping.h"
#include "Oxygen/Platform/Common/Input.h"
#include "Oxygen/Platform/Common/InputEvent.h"
#include <Oxygen/Base/Composition.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/TimeUtils.h>
#include <Oxygen/Base/TypeSystem.h>
#include <Oxygen/Base/Types/Geometry.h>

using oxygen::input::InputActionMapping;
using oxygen::platform::InputSlots;

InputActionMapping::InputActionMapping(std::shared_ptr<Action> action,
    const platform::InputSlot& input_slot)
    : action_(std::move(action))
    , slot_(input_slot)
{
}

void InputActionMapping::StartEvaluation()
{
    // Reset Action value
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
    DLOG_F(2, "action {} triggers evaluation started", action_->GetName());
    action_->OnStarted()(*action_);
    evaluation_ongoing_ = true;
    found_explicit_trigger_ = false;
    any_explicit_triggered_ = false;
    all_implicits_triggered_ = true;
    blocked_ = false;
}

void InputActionMapping::NotifyActionCanceled()
{
    DLOG_F(2, "action {} cancelled", action_->GetName());
    action_->OnCanceled()(*action_);
    CompleteEvaluation();
}
void InputActionMapping::NotifyActionTriggered()
{
    LOG_F(INFO, "===> action triggered : {}", action_->GetName());
    action_->OnTriggered()(*action_, action_value_);
    any_explicit_triggered_ = false;
    all_implicits_triggered_ = true;
}
void InputActionMapping::NotifyActionOngoing()
{
    DLOG_F(2, "action {} trigger evaluation ongoing", action_->GetName());
    action_->OnOngoing()(*action_);
    action_ongoing_ = true;
}

void InputActionMapping::CompleteEvaluation()
{
    DLOG_F(2, "action {} trigger evaluation completed", action_->GetName());
    action_->OnCompleted()(*action_);
    evaluation_ongoing_ = false;
    action_ongoing_ = false;
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
        action_value_.Update({ .x = dx, .y = dy });
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
    CompleteEvaluation();
}

auto InputActionMapping::Update(oxygen::Duration delta_time) -> bool
{
    const auto input_consumed = DoUpdate(delta_time);

    if (clear_value_after_update_) {
        action_value_.Update({ .x = 0.0F, .y = 0.0F });
        clear_value_after_update_ = false;
    }

    return input_consumed;
}

auto InputActionMapping::DoUpdate(Duration delta_time) -> bool
{
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
        } else if (trigger->IsImplicit()) {
            all_implicits_triggered_ &= trigger->IsTriggered();
        } else if (trigger->IsBlocker()) {
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

    bool input_consumed { false };

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
    } else {
        NotifyActionOngoing();
    }
    return input_consumed;
}
