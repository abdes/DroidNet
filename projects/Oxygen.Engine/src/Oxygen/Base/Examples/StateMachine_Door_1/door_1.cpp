//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <iostream>

#include <Oxygen/Base/StateMachine.h>

using oxygen::fsm::ByDefault;
using oxygen::fsm::DoNothing;
using oxygen::fsm::On;
using oxygen::fsm::StateMachine;
using oxygen::fsm::TransitionTo;
using oxygen::fsm::Will;

namespace {
struct OpenEvent { };
struct CloseEvent { };

struct ClosedState;
struct OpenState;

struct ClosedState :
  // Using the state helpers to completely implement the state
  // in a declarative way
  Will<
    // Default action is to do nothing
    ByDefault<DoNothing>,
    // Specific action on a specific event
    On<OpenEvent, TransitionTo<OpenState>>> { };

struct OpenState
  : Will<ByDefault<DoNothing>, On<CloseEvent, TransitionTo<ClosedState>>> { };

using Door = StateMachine<ClosedState, OpenState>;

auto PrintDoorState(const Door& door) -> void
{
  std::cout << "   > door is " << (door.IsIn<OpenState>() ? "open" : "closed")
            << "\n";
}
} // namespace

auto main() -> int
{
  try {
    Door door { ClosedState {}, OpenState {} };
    std::cout << "-- Starting\n";
    PrintDoorState(door);

    std::cout << "-- sending close event\n";
    door.Handle(CloseEvent {});
    PrintDoorState(door);

    std::cout << "-- sending open event\n";
    door.Handle(OpenEvent {});
    PrintDoorState(door);

    std::cout << "-- sending close event\n";
    door.Handle(CloseEvent {});
    PrintDoorState(door);
  } catch (const std::exception& err) {
    std::cerr << "An exception was thrown: " << err.what() << '\n';
  } catch (...) {
    std::cerr << "An unknown exception was thrown" << '\n';
  }
}
