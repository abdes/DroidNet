//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Input/Action.h>

using oxygen::input::Action;
using oxygen::input::ActionState;

Action::Action(std::string name, const ActionValueType value_type)
  : name_(std::move(name))
  , value_type_(value_type)
{
}

auto Action::State::ToActionState() const -> ActionState
{
  // ToActionState returns only the current state
  // (ongoing/triggered/completed/canceled). Edge events like Started are not
  // included here because they require comparing the current state to the
  // previous state. Use the recorded transitions or Was*ThisFrame() helpers
  // to query edges.
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

auto Action::State::FromActionState(ActionState states) -> State
{
  return State {
    .triggered = static_cast<bool>(states & ActionState::kTriggered),
    .ongoing = static_cast<bool>(states & ActionState::kOngoing),
    .completed = static_cast<bool>(states & ActionState::kCompleted),
    .canceled = static_cast<bool>(states & ActionState::kCanceled),
  };
}

void Action::BeginFrameTracking()
{
  // Clear edge flags for the new frame; edges are non-sticky by invariant.
  state_.triggered = false;
  state_.completed = false;
  state_.canceled = false;
  // Reset per-frame transitions
  frame_transitions_.clear();
  // Reset per-frame value update flag
  // Note: We don't expose this flag directly; WasValueUpdatedThisFrame()
  // inspects transitions or this cached flag if needed.
  value_updated_this_frame_ = false;
}

void Action::UpdateState(const State& state, const ActionValue& value)
{
  // Compute flags for previous and new snapshots
  const auto previous_flags = state_.ToActionState();

  // Update state flags
  state_ = state;

  // Update value
  value_ = value;
  value_updated_this_frame_ = true;

  // Record transition if state changed
  const auto new_flags = state_.ToActionState();
  if (new_flags != previous_flags) {
    RecordTransition(previous_flags, new_flags, value_);
  }
}

void Action::EndFrameTracking()
{
  // No hard reset here. The action persists level state (ongoing/value)
  // across frames. Per-frame transitions have already been recorded and are
  // intended for current-frame queries only.
}

void Action::RecordTransition(
  ActionState from, ActionState to, const ActionValue& value)
{
  // TODO(abdes): Timebase consistency — use engine time consistently for
  // timestamps. For now, use SecondsToDuration(0) placeholder or a suitable
  // engine clock when available in this TU.
  frame_transitions_.push_back({
    .from_state = from,
    .to_state = to,
    .value_at_transition = value,
  });
}

// --- Convenience per-frame edge queries -------------------------------------

auto Action::WasStartedThisFrame() const -> bool
{
  // kStarted removed; retain behavior by deriving from Ongoing edge.
  for (const auto& t : frame_transitions_) {
    const bool from_ongoing
      = static_cast<bool>(t.from_state & ActionState::kOngoing);
    const bool to_ongoing
      = static_cast<bool>(t.to_state & ActionState::kOngoing);
    if (!from_ongoing && to_ongoing) {
      return true;
    }
  }
  return false;
}

auto Action::WasTriggeredThisFrame() const -> bool
{
  for (const auto& t : frame_transitions_) {
    const bool to_trig
      = static_cast<bool>(t.to_state & ActionState::kTriggered);
    if (to_trig) {
      return true;
    }
  }
  return false;
}

auto Action::WasValueUpdatedThisFrame() const -> bool
{
  return value_updated_this_frame_;
}

auto Action::WasCompletedThisFrame() const -> bool
{
  for (const auto& t : frame_transitions_) {
    const bool to_completed
      = static_cast<bool>(t.to_state & ActionState::kCompleted);
    if (to_completed) {
      return true;
    }
  }
  return false;
}

auto Action::WasCanceledThisFrame() const -> bool
{
  for (const auto& t : frame_transitions_) {
    const bool to_canceled
      = static_cast<bool>(t.to_state & ActionState::kCanceled);
    if (to_canceled) {
      return true;
    }
  }
  return false;
}

auto Action::WasReleasedThisFrame() const -> bool
{
  // Released is an Ongoing->Idle edge; infer by transitions where Ongoing bit
  // changes from 1 to 0.
  for (const auto& t : frame_transitions_) {
    const bool from_ongoing
      = static_cast<bool>(t.from_state & ActionState::kOngoing);
    const bool to_ongoing
      = static_cast<bool>(t.to_state & ActionState::kOngoing);
    if (from_ongoing && !to_ongoing) {
      return true;
    }
  }
  return false;
}
