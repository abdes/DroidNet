//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionState.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/InputSnapshot.h>

using oxygen::input::Action;
using oxygen::input::ActionLookup;
using oxygen::input::ActionState;
using oxygen::input::ActionValue;
using oxygen::input::InputSnapshot;

InputSnapshot::InputSnapshot(
  const std::vector<std::shared_ptr<Action>>& actions)
{
  actions_.reserve(actions.size());
  for (const auto& a : actions) {
    if (a) {
      const auto t = a->GetFrameTransitions();
      actions_.push_back(Entry {
          .name = a->GetName(),
          .asset_key = a->GetAssetKey(),
          .state = Action::State {
            .triggered = a->IsTriggered(),
            .ongoing = a->IsOngoing(),
            .completed = a->IsCompleted(),
            .canceled = a->IsCanceled(),
          },
          .value = a->GetValue(),
          .value_updated = a->WasValueUpdatedThisFrame(),
          .transitions = { t.begin(), t.end() },
        });
    }
  }
}

auto InputSnapshot::IsActionTriggered(const ActionLookup& action) const -> bool
{
  const auto* a = FindAction(action);
  return (a != nullptr) ? a->state.triggered : false;
}

auto InputSnapshot::IsActionOngoing(const ActionLookup& action) const -> bool
{
  const auto* a = FindAction(action);
  return (a != nullptr) ? a->state.ongoing : false;
}

auto InputSnapshot::IsActionCompleted(const ActionLookup& action) const -> bool
{
  const auto* a = FindAction(action);
  return (a != nullptr) ? a->state.completed : false;
}

auto InputSnapshot::IsActionCanceled(const ActionLookup& action) const -> bool
{
  const auto* a = FindAction(action);
  return (a != nullptr) ? a->state.canceled : false;
}

auto InputSnapshot::IsActionIdle(const ActionLookup& action) const -> bool
{
  const auto* a = FindAction(action);
  return (a != nullptr) ? !(a->state.ongoing || a->state.triggered)
                        : true; // Default to idle if action not found
}

auto InputSnapshot::GetActionValue(const ActionLookup& action) const
  -> ActionValue
{
  const auto* a = FindAction(action);
  return (a != nullptr) ? a->value : ActionValue {};
}

auto InputSnapshot::DidActionStart(const ActionLookup& action) const -> bool
{
  const auto* a = FindAction(action);
  if (a == nullptr) {
    return false;
  }
  bool saw_start_edge = false; // within-frame Ongoing edge
  for (const auto& t :
    std::span<const Action::FrameTransition>(a->transitions)) {
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

auto InputSnapshot::DidActionComplete(const ActionLookup& action) const -> bool
{
  return DidActionTransition(
           action, ActionState::kOngoing, ActionState::kCompleted)
    || DidActionTransition(
      action, ActionState::kTriggered, ActionState::kCompleted);
}

auto InputSnapshot::DidActionCancel(const ActionLookup& action) const -> bool
{
  return DidActionTransition(
           action, ActionState::kOngoing, ActionState::kCanceled)
    || DidActionTransition(
      action, ActionState::kTriggered, ActionState::kCanceled);
}

auto InputSnapshot::GetActionStateFlags(const ActionLookup& action) const
  -> ActionState
{
  const auto* a = FindAction(action);
  if (a == nullptr) {
    return ActionState::kNone;
  }
  return a->state.ToActionState();
}

auto InputSnapshot::DidActionTrigger(const ActionLookup& action) const -> bool
{
  const auto* a = FindAction(action);
  if (a == nullptr) {
    return false;
  }
  return std::ranges::any_of(
    std::span<const Action::FrameTransition>(a->transitions),
    [](const Action::FrameTransition& t) -> bool {
      return static_cast<bool>(t.to_state & ActionState::kTriggered);
    });
}

auto InputSnapshot::DidActionRelease(const ActionLookup& action) const -> bool
{
  const auto* a = FindAction(action);
  if (a == nullptr) {
    return false;
  }
  return std::ranges::any_of(
    std::span<const Action::FrameTransition>(a->transitions),
    [](const auto& t) -> bool {
      const bool from_ongoing
        = static_cast<bool>(t.from_state & ActionState::kOngoing);
      const bool to_ongoing
        = static_cast<bool>(t.to_state & ActionState::kOngoing);
      return from_ongoing && !to_ongoing;
    });
}

auto InputSnapshot::DidActionValueUpdate(const ActionLookup& action) const
  -> bool
{
  const auto* a = FindAction(action);
  return (a != nullptr) ? a->value_updated : false;
}

auto InputSnapshot::DidActionTransition(
  const ActionLookup& action, ActionState from, ActionState to) const -> bool
{
  const auto* a = FindAction(action);
  if (a == nullptr) {
    return false;
  }

  return std::ranges::any_of(
    std::span<const Action::FrameTransition>(a->transitions),
    [from, to](const Action::FrameTransition& t) -> bool {
      return t.from_state == from && t.to_state == to;
    });
}

auto InputSnapshot::GetActionTransitions(const ActionLookup& action) const
  -> std::span<const Action::FrameTransition>
{
  const auto* a = FindAction(action);
  return (a != nullptr)
    ? std::span<const Action::FrameTransition>(a->transitions)
    : std::span<const Action::FrameTransition> {};
}

auto InputSnapshot::GetActionNames() const -> std::vector<std::string_view>
{
  std::vector<std::string_view> names;
  names.reserve(actions_.size());
  for (const auto& entry : actions_) {
    names.emplace_back(entry.name);
  }
  return names;
}

auto InputSnapshot::GetActionKeys() const -> std::vector<data::AssetKey>
{
  std::vector<data::AssetKey> keys;
  for (const auto& entry : actions_) {
    if (!entry.asset_key.IsNil()) {
      keys.push_back(entry.asset_key);
    }
  }
  return keys;
}

auto InputSnapshot::FindAction(const ActionLookup& action) const -> const Entry*
{
  const auto it
    = std::ranges::find_if(actions_, [&action](const Entry& entry) -> bool {
        return action.ByKey()
          ? (!action.Key().IsNil() && entry.asset_key == action.Key())
          : entry.name == action.Name();
      });
  return it != actions_.end() ? &*it : nullptr;
}
