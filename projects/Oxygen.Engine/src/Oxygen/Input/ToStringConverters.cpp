//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Input/ActionState.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>

auto oxygen::input::to_string(
  const oxygen::input::ActionValueType value) noexcept -> std::string_view
{
  switch (value) {
  case ActionValueType::kBool:
    return "Bool";
  case ActionValueType::kAxis1D:
    return "Axis1D";
  case ActionValueType::kAxis2D:
    return "Axis2D";
  }
  return "__NotSupported__";
}

auto oxygen::input::to_string(
  const oxygen::input::ActionTriggerType value) noexcept -> std::string_view
{
  switch (value) {
  case ActionTriggerType::kPressed:
    return "Pressed";
  case ActionTriggerType::kReleased:
    return "Released";
  case ActionTriggerType::kDown:
    return "Down";
  case ActionTriggerType::kHold:
    return "Hold";
  case ActionTriggerType::kHoldAndRelease:
    return "HoldAndRelease";
  case ActionTriggerType::kPulse:
    return "Pulse";
  case ActionTriggerType::kTap:
    return "Tap";
  case ActionTriggerType::kChord:
    return "Chord";
  case ActionTriggerType::kActionChain:
    return "ActionChain";
  case ActionTriggerType::kCombo:
    return "Combo";
  }
  return "__NotSupported__";
}

auto oxygen::input::to_string(
  const oxygen::input::ActionTrigger::Behavior value) noexcept
  -> std::string_view
{
  switch (value) {
  case ActionTrigger::Behavior::kExplicit:
    return "Explicit";
  case ActionTrigger::Behavior::kImplicit:
    return "Implicit";
  case ActionTrigger::Behavior::kBlocker:
    return "Blocker";
  }
  return "__NotSupported__";
}

auto oxygen::input::to_string(const oxygen::input::ActionState value)
  -> std::string
{
  using Flags = oxygen::input::ActionState;

  if (value == Flags::kNone) {
    return "None";
  }

  std::string result;
  bool first = true;
  auto checked = Flags::kNone;

  const auto check_and_append = [&](const Flags flag, const char* name) {
    if ((value & flag) == flag) {
      if (!first) {
        result += " | ";
      }
      result += name;
      first = false;
      checked |= flag;
    }
  };

  check_and_append(Flags::kOngoing, "Ongoing");
  check_and_append(Flags::kCanceled, "Canceled");
  check_and_append(Flags::kCompleted, "Completed");
  check_and_append(Flags::kTriggered, "Triggered");

  DCHECK_EQ_F(
    checked, value, "to_string: Unchecked ActionState value detected");
  return result;
}
