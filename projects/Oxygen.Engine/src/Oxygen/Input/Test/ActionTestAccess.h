//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionValue.h>

namespace oxygen::input::testing {

//! Test peer for Action's state mutators, which are private to the input
//! system. Lets unit tests put an action in an arbitrary state, for example
//! the prerequisite of a Chain trigger or the steps of a Combo.
class ActionTestAccess {
public:
  static void BeginFrame(Action& action) { action.BeginFrameTracking(); }

  static void EndFrame(Action& action) { action.EndFrameTracking(); }

  static void UpdateState(
    Action& action, const Action::State& state, const ActionValue& value)
  {
    action.UpdateState(state, value);
  }

  static void Contribute(Action& action, const void* binding,
    const Action::State& state, const ActionValue& value)
  {
    action.Contribute(binding, state, value);
  }

  static void RemoveContribution(Action& action, const void* binding)
  {
    action.RemoveContribution(binding);
  }
};

} // namespace oxygen::input::testing
