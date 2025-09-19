//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <ranges>

#include <Oxygen/Input/InputSnapshot.h>
#include <Oxygen/Platform/InputEvent.h>

using oxygen::input::Action;
using oxygen::input::ActionState;
using oxygen::input::ActionValue;
using oxygen::input::InputSnapshot;

InputSnapshot::InputSnapshot(
  const std::vector<std::shared_ptr<Action>>& actions)
{
  // Build a thin lookup from name to action pointer. We deliberately do not
  // copy state or transitions to avoid redundancy. Actions won't change after
  // kInput, so querying them is stable during the remainder of the frame.
  actions_.reserve(actions.size());
  for (const auto& a : actions) {
    if (a) {
      actions_.emplace(a->GetName(), a.get());
    }
  }
}

bool InputSnapshot::IsActionTriggered(std::string_view action_name) const
{
  const auto* a = FindAction(action_name);
  return a ? a->IsTriggered() : false;
}

bool InputSnapshot::IsActionOngoing(std::string_view action_name) const
{
  const auto* a = FindAction(action_name);
  return a ? a->IsOngoing() : false;
}

bool InputSnapshot::IsActionCompleted(std::string_view action_name) const
{
  const auto* a = FindAction(action_name);
  return a ? a->IsCompleted() : false;
}

bool InputSnapshot::IsActionCanceled(std::string_view action_name) const
{
  const auto* a = FindAction(action_name);
  return a ? a->IsCanceled() : false;
}

bool InputSnapshot::IsActionIdle(std::string_view action_name) const
{
  const auto* a = FindAction(action_name);
  return a ? a->IsIdle() : true; // Default to idle if action not found
}

ActionValue InputSnapshot::GetActionValue(std::string_view action_name) const
{
  const auto* a = FindAction(action_name);
  return a ? a->GetValue() : ActionValue {};
}

bool InputSnapshot::DidActionStart(std::string_view action_name) const
{
  const auto* a = FindAction(action_name);
  if (!a) {
    return false;
  }
  bool saw_start_edge = false; // within-frame Ongoing edge
  for (const auto& t : a->GetFrameTransitions()) {
    const bool from_none = (t.from_state == ActionState::kNone);
    const bool from_ongoing
      = static_cast<bool>(t.from_state & ActionState::kOngoing);
    const bool to_ongoing
      = static_cast<bool>(t.to_state & ActionState::kOngoing);
    const bool to_triggered
      = static_cast<bool>(t.to_state & ActionState::kTriggered);
    if (from_none && to_triggered) {
      return true;
    }
    if (!from_ongoing && to_ongoing) {
      saw_start_edge = true;
    }
    if (saw_start_edge && to_triggered) {
      return true;
    }
  }
  return false;
}

bool InputSnapshot::DidActionComplete(std::string_view action_name) const
{
  return DidActionTransition(
           action_name, ActionState::kOngoing, ActionState::kCompleted)
    || DidActionTransition(
      action_name, ActionState::kTriggered, ActionState::kCompleted);
}

bool InputSnapshot::DidActionCancel(std::string_view action_name) const
{
  return DidActionTransition(
           action_name, ActionState::kOngoing, ActionState::kCanceled)
    || DidActionTransition(
      action_name, ActionState::kTriggered, ActionState::kCanceled);
}

ActionState InputSnapshot::GetActionStateFlags(
  std::string_view action_name) const
{
  const auto* a = FindAction(action_name);
  if (!a) {
    return ActionState::kNone;
  }
  return Action::State { .triggered = a->IsTriggered(),
    .ongoing = a->IsOngoing(),
    .completed = a->IsCompleted(),
    .canceled = a->IsCanceled() }
    .ToActionState();
}

bool InputSnapshot::DidActionTrigger(std::string_view action_name) const
{
  const auto* a = FindAction(action_name);
  if (!a) {
    return false;
  }
  return std::ranges::any_of(
    a->GetFrameTransitions(), [](const Action::FrameTransition& t) {
      return static_cast<bool>(t.to_state & ActionState::kTriggered);
    });
}

bool InputSnapshot::DidActionRelease(std::string_view action_name) const
{
  const auto* a = FindAction(action_name);
  if (!a) {
    return false;
  }
  for (const auto& t : a->GetFrameTransitions()) {
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

bool InputSnapshot::DidActionValueUpdate(std::string_view action_name) const
{
  const auto* a = FindAction(action_name);
  return a ? a->WasValueUpdatedThisFrame() : false;
}

bool InputSnapshot::DidActionTransition(
  std::string_view action_name, ActionState from, ActionState to) const
{
  const auto* a = FindAction(action_name);
  if (!a)
    return false;

  return std::ranges::any_of(
    a->GetFrameTransitions(), [from, to](const Action::FrameTransition& t) {
      return t.from_state == from && t.to_state == to;
    });
}

auto InputSnapshot::GetActionTransitions(std::string_view action_name) const
  -> std::span<const Action::FrameTransition>
{
  const auto* a = FindAction(action_name);
  return a ? a->GetFrameTransitions()
           : std::span<const Action::FrameTransition> {};
}

auto InputSnapshot::FindAction(std::string_view action_name) const
  -> const Action*
{
  const auto it = actions_.find(std::string { action_name });
  return it != actions_.end() ? it->second : nullptr;
}
