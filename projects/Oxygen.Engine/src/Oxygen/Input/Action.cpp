//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionState.h>
#include <Oxygen/Input/ActionValue.h>

using oxygen::Axis1D;
using oxygen::Axis2D;
using oxygen::input::Action;
using oxygen::input::ActionAccumulation;
using oxygen::input::ActionState;
using oxygen::input::ActionValue;
using oxygen::input::ActionValueType;

namespace {

//! Combine one component of a binding's value into the running result.
void CombineComponent(
  const ActionAccumulation mode, float& result, const float value) noexcept
{
  if (mode == ActionAccumulation::kCumulative) {
    result += value;
  } else if (std::abs(value) > std::abs(result)) {
    result = value;
  }
}

auto HasFlag(const ActionState states, const ActionState flag) noexcept
{
  return (states & flag) != ActionState::kNone;
}

} // namespace

Action::Action(std::string name, const ActionValueType value_type,
  const data::AssetKey asset_key)
  : name_(std::move(name))
  , asset_key_(asset_key)
  , value_type_(value_type)
  , value_(ZeroValue())
{
  if (asset_key_.IsNil()) {
    throw std::invalid_argument("input action '" + name_ + "' has a nil key");
  }
}

auto Action::State::ToActionState() const -> ActionState
{
  // Only the current flags; edges such as Started are derived from the
  // recorded transitions.
  auto s = ActionState::kNone;
  if (ongoing) {
    s |= ActionState::kOngoing;
  }
  if (completed) {
    s |= ActionState::kCompleted;
  }
  if (canceled) {
    s |= ActionState::kCanceled;
  }
  if (triggered) {
    s |= ActionState::kTriggered;
  }
  return s;
}

auto Action::State::FromActionState(const ActionState states) -> State
{
  return State {
    .triggered = HasFlag(states, ActionState::kTriggered),
    .ongoing = HasFlag(states, ActionState::kOngoing),
    .completed = HasFlag(states, ActionState::kCompleted),
    .canceled = HasFlag(states, ActionState::kCanceled),
  };
}

auto Action::ZeroValue() const -> ActionValue
{
  switch (value_type_) {
  case ActionValueType::kAxis1D:
    return ActionValue { Axis1D { .x = 0.0F } };
  case ActionValueType::kAxis2D:
    return ActionValue { Axis2D { .x = 0.0F, .y = 0.0F } };
  case ActionValueType::kBool:
    break;
  }
  return ActionValue { false };
}

void Action::BeginFrameTracking()
{
  // An activation that triggered without holding (Pressed, Tap, Released)
  // ends with the frame in which it triggered.
  const bool non_held_activation_ended
    = state_.triggered && !state_.ongoing && !state_.canceled;

  state_.triggered = false;
  state_.completed = false;
  state_.canceled = false;
  frame_transitions_.clear();
  value_updated_this_frame_ = false;
  ++frame_serial_;

  if (non_held_activation_ended) {
    state_.completed = true;
    triggered_this_activation_ = false;
    RecordTransition(ActionState::kTriggered, ActionState::kCompleted, value_);
  }
}

void Action::EndFrameTracking()
{
  // Level state persists into the next frame; transitions stay readable until
  // the next BeginFrameTracking().
}

void Action::UpdateState(const State& state, const ActionValue& value)
{
  const auto previous_flags = state_.ToActionState();
  state_ = state;
  value_ = value;
  value_updated_this_frame_ = true;

  if (const auto new_flags = state_.ToActionState();
    new_flags != previous_flags) {
    RecordTransition(previous_flags, new_flags, value_);
  }
}

void Action::Contribute(
  const void* binding, const State& state, const ActionValue& value)
{
  const Contribution contribution {
    .binding = binding,
    .state = state,
    .value = value,
    .frame_serial = frame_serial_,
  };
  if (const auto it
    = std::ranges::find(contributions_, binding, &Contribution::binding);
    it != contributions_.end()) {
    *it = contribution;
  } else {
    contributions_.push_back(contribution);
  }
  Recombine();
}

void Action::RemoveContribution(const void* binding)
{
  if (std::erase_if(contributions_,
        [binding](
          const Contribution& c) -> bool { return c.binding == binding; })
    > 0) {
    Recombine();
  }
}

void Action::Recombine()
{
  State combined {};
  bool any_canceled = false;
  Axis2D sum { .x = 0.0F, .y = 0.0F };
  bool any_bool = false;
  for (const auto& c : contributions_) {
    const bool current_frame = c.frame_serial == frame_serial_;
    combined.ongoing |= c.state.ongoing;
    combined.triggered |= current_frame && c.state.triggered;
    any_canceled |= current_frame && c.state.canceled;
    switch (value_type_) {
    case ActionValueType::kBool:
      any_bool |= c.value.GetAs<bool>();
      break;
    case ActionValueType::kAxis1D:
      CombineComponent(accumulation_, sum.x, c.value.GetAs<Axis1D>().x);
      break;
    case ActionValueType::kAxis2D:
      CombineComponent(accumulation_, sum.x, c.value.GetAs<Axis2D>().x);
      CombineComponent(accumulation_, sum.y, c.value.GetAs<Axis2D>().y);
      break;
    }
  }

  const bool active = combined.ongoing || combined.triggered;
  combined.canceled = any_canceled && !active;

  // Completed closes an activation that triggered at least once and ended
  // without a cancel. Edges raised earlier in the frame stay visible.
  const bool was_active = state_.ongoing || state_.triggered;
  combined.completed = state_.completed
    || (was_active && !active && triggered_this_activation_
      && !combined.canceled);
  if (combined.triggered) {
    triggered_this_activation_ = true;
  } else if (!active) {
    triggered_this_activation_ = false;
  }

  ActionValue value;
  switch (value_type_) {
  case ActionValueType::kBool:
    value = ActionValue { any_bool };
    break;
  case ActionValueType::kAxis1D:
    value = ActionValue { Axis1D { .x = sum.x } };
    break;
  case ActionValueType::kAxis2D:
    value = ActionValue { sum };
    break;
  }
  UpdateState(combined, value);
}

void Action::RecordTransition(
  const ActionState from, const ActionState to, const ActionValue& value)
{
  if (!HasFlag(from, ActionState::kTriggered)
    && HasFlag(to, ActionState::kTriggered)) {
    ++pending_fixed_triggers_;
  }
  frame_transitions_.push_back({
    .from_state = from,
    .to_state = to,
    .value_at_transition = value,
  });
}

auto Action::ConsumeFixedStepTrigger() -> bool
{
  if (pending_fixed_triggers_ == 0) {
    return false;
  }
  --pending_fixed_triggers_;
  return true;
}

// --- Convenience per-frame edge queries -------------------------------------

auto Action::WasStartedThisFrame() const -> bool
{
  return std::ranges::any_of(frame_transitions_, [](const auto& t) -> bool {
    return !HasFlag(t.from_state, ActionState::kOngoing)
      && HasFlag(t.to_state, ActionState::kOngoing);
  });
}

auto Action::WasTriggeredThisFrame() const -> bool
{
  return std::ranges::any_of(frame_transitions_, [](const auto& t) -> bool {
    return HasFlag(t.to_state, ActionState::kTriggered);
  });
}

auto Action::WasValueUpdatedThisFrame() const -> bool
{
  return value_updated_this_frame_;
}

auto Action::WasCompletedThisFrame() const -> bool
{
  return std::ranges::any_of(frame_transitions_, [](const auto& t) -> bool {
    return HasFlag(t.to_state, ActionState::kCompleted);
  });
}

auto Action::WasCanceledThisFrame() const -> bool
{
  return std::ranges::any_of(frame_transitions_, [](const auto& t) -> bool {
    return HasFlag(t.to_state, ActionState::kCanceled);
  });
}

auto Action::WasReleasedThisFrame() const -> bool
{
  return std::ranges::any_of(frame_transitions_, [](const auto& t) -> bool {
    return HasFlag(t.from_state, ActionState::kOngoing)
      && !HasFlag(t.to_state, ActionState::kOngoing);
  });
}
