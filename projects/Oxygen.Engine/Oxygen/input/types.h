// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>

namespace oxygen::input {

  class Action;
  class ActionTrigger;
  class ActionValue;
  class InputActionMapping;
  class InputMappingContext;
  class InputSystem;

  enum class ActionValueType : uint8_t
  {
    kBool = 0,
    kAxis1D = 1,
    kAxis2D = 2,
  };

  enum class ActionStates : uint8_t
  {
    kNone = 0,
    kStarted = 1 << 0,
    kOngoing = 1 << 1,
    kCanceled = 1 << 2,
    kCompleted = 1 << 3,
    kTriggered = 1 << 4,
  };

  constexpr auto operator|=(ActionStates& states, const ActionStates other)
    -> auto& {
    states = static_cast<ActionStates>(
      static_cast<std::underlying_type_t<ActionStates>>(states)
      | static_cast<std::underlying_type_t<ActionStates>>(other));
    return states;
  }
  constexpr auto operator|(const ActionStates left, const ActionStates right) {
    const auto mods = static_cast<ActionStates>(
      static_cast<std::underlying_type_t<ActionStates>>(left)
      | static_cast<std::underlying_type_t<ActionStates>>(right));
    return mods;
  }
  constexpr auto operator&(const ActionStates left, const ActionStates right) {
    const auto mods = static_cast<ActionStates>(
      static_cast<std::underlying_type_t<ActionStates>>(left)
      & static_cast<std::underlying_type_t<ActionStates>>(right));
    return mods;
  }

}  // namespace oxygen::input
