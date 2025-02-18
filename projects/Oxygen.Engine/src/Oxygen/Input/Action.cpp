//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Input/Action.h>

using oxygen::input::Action;

Action::Action(std::string name, const ActionValueType value_type)
    : name_(std::move(name))
    , value_type_(value_type)
{
    // All these connections are to our own events, they will be destroyed once
    // this object is destroyed. We do not need to manage their lifecycle.
    // NOLINTBEGIN(*-unused-return-value)

    on_started_.connect([this](const Action&) {
        is_idle_ = true;
        is_ongoing_ = false;
        is_canceled_ = false;
        is_completed_ = false;
        is_triggered_ = false;
    });
    on_ongoing_.connect([this](const Action&) {
        is_ongoing_ = true;
        is_idle_ = false;
    });
    on_canceled_.connect([this](const Action&) {
        is_canceled_ = true;
        is_idle_ = true;
        is_ongoing_ = false;
        is_triggered_ = false;
    });
    on_completed_.connect([this](const Action&) {
        is_completed_ = true;
        is_idle_ = true;
        is_canceled_ = false;
        is_ongoing_ = false;
    });
    on_triggered_.connect(
        [this](const Action&, const ActionValue&) { is_triggered_ = true; });

    // NOLINTEND(*-unused-return-value)
}

auto Action::GetCurrentStates() const -> ActionState
{
    auto states { ActionState::kNone };

    if (is_idle_) {
        states |= ActionState::kStarted;
    }
    if (is_ongoing_) {
        states |= ActionState::kOngoing;
    }
    if (is_completed_) {
        states |= ActionState::kCompleted;
    }
    if (is_canceled_) {
        states |= ActionState::kCanceled;
    }
    if (is_triggered_) {
        states |= ActionState::kTriggered;
    }

    return states;
}
