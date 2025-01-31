//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen::input {

enum class ActionState : uint8_t {
    kNone = 0,
    kStarted = 1 << 0,
    kOngoing = 1 << 1,
    kCanceled = 1 << 2,
    kCompleted = 1 << 3,
    kTriggered = 1 << 4,
};

constexpr auto operator|=(ActionState& states, const ActionState other)
    -> auto&
{
    states = static_cast<ActionState>(
        static_cast<std::underlying_type_t<ActionState>>(states)
        | static_cast<std::underlying_type_t<ActionState>>(other));
    return states;
}
constexpr auto operator|(const ActionState left, const ActionState right)
{
    const auto mods = static_cast<ActionState>(
        static_cast<std::underlying_type_t<ActionState>>(left)
        | static_cast<std::underlying_type_t<ActionState>>(right));
    return mods;
}
constexpr auto operator&(const ActionState left, const ActionState right)
{
    const auto mods = static_cast<ActionState>(
        static_cast<std::underlying_type_t<ActionState>>(left)
        & static_cast<std::underlying_type_t<ActionState>>(right));
    return mods;
}

} // namespace oxygen::input
