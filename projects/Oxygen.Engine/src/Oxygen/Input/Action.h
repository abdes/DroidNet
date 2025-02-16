//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Base/Detail/signal.hpp>
#include <Oxygen/Input/ActionState.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/api_export.h>

namespace oxygen::input {

class ActionTrigger;

class Action {
public:
    OXYGEN_INPUT_API Action(std::string name, ActionValueType value_type);

    [[nodiscard]] auto GetName() const { return name_; }

    [[nodiscard]] auto GetValueType() const { return value_type_; }

    [[nodiscard]] auto GetValue() const -> const auto& { return value_; }

    [[nodiscard]] auto ConsumesInput() const -> const auto&
    {
        return consume_input_;
    }
    void SetConsumesInput(const bool consume) { consume_input_ = consume; }

    // -- Action events ----------------------------------------------------------

    [[nodiscard]] auto OnCanceled() -> auto& { return on_canceled_; }
    [[nodiscard]] auto OnCompleted() -> auto& { return on_completed_; }
    [[nodiscard]] auto OnOngoing() -> auto& { return on_ongoing_; }
    [[nodiscard]] auto OnStarted() -> auto& { return on_started_; }
    [[nodiscard]] auto OnTriggered() -> auto& { return on_triggered_; }

    // -- Action states ----------------------------------------------------------

    [[nodiscard]] auto IsCanceled() const { return is_canceled_; }
    [[nodiscard]] auto IsCompleted() const { return is_completed_; }
    [[nodiscard]] auto IsOngoing() const { return is_ongoing_; }
    [[nodiscard]] auto IsIdle() const { return is_idle_; }
    [[nodiscard]] auto IsTriggered() const { return is_triggered_; }

    [[nodiscard]] auto GetCurrentStates() const -> ActionState;

    // In order to clear the action's triggered state
    friend class InputSystem;

protected:
    // protected so we can unit test the class
    void ClearTriggeredState() { is_triggered_ = false; }

private:
    std::string name_;
    ActionValueType value_type_;
    ActionValue value_;
    bool consume_input_ { false };
    std::vector<std::shared_ptr<ActionTrigger>> triggers_;

    sigslot::signal<const Action&> on_canceled_;
    sigslot::signal<const Action&> on_completed_;
    sigslot::signal<const Action&> on_ongoing_;
    sigslot::signal<const Action&> on_started_;
    sigslot::signal<const Action&, const ActionValue&> on_triggered_;

    bool is_canceled_ { false };
    bool is_completed_ { false };
    bool is_ongoing_ { false };
    bool is_idle_ { false };
    bool is_triggered_ { false };
};

} // namespace oxygen::input

// TODO(abdes) ass some debugging counters for the action.

// ElapsedProcessedTime:	How long it took from started to
// completed/canceled.

// LastTriggeredWorldTime:	last time it evaluated to a triggered state.
