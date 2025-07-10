//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>

#include <Oxygen/Base/StateMachine.h>

using asap::fsm::ByDefault;
using asap::fsm::Continue;
using asap::fsm::DoNothing;
using asap::fsm::On;
using asap::fsm::StateMachine;
using asap::fsm::Status;
using asap::fsm::TransitionTo;
using asap::fsm::Will;

namespace {
struct OpenEvent { };
struct CloseEvent { };

struct ClosedState;
struct OpenState;

struct ClosedState
  : Will<ByDefault<DoNothing>, On<OpenEvent, TransitionTo<OpenState>>> {
  using Will::Handle;

  template <typename Event>
  static auto OnEnter(const Event& /*event*/) -> Status
  {
    std::cout << "   > door is closed\n";
    return Continue {};
  }

  [[maybe_unused]] [[nodiscard]] static auto Handle(const CloseEvent& /*event*/)
    -> DoNothing
  {
    std::cerr << "Error: the door is already closed!\n";
    return DoNothing {};
  }
};

struct OpenState
  : Will<ByDefault<DoNothing>, On<CloseEvent, TransitionTo<ClosedState>>> {
  using Will::Handle;

  template <typename Event>
  static auto OnEnter(const Event& /*event*/) -> Status
  {
    std::cout << "   > door is open\n";
    return Continue {};
  }

  [[nodiscard]] [[maybe_unused]] static auto Handle(const OpenEvent& /*event*/)
    -> DoNothing
  {
    std::cerr << "Error: the door is already open!\n";
    return DoNothing {};
  }
};
} // namespace

using Door = StateMachine<ClosedState, OpenState>;

auto main() -> int
{
  try {
    Door door { ClosedState {}, OpenState {} };
    std::cout << "-- Starting\n";

    std::cout << "-- sending close event\n";
    door.Handle(CloseEvent {});

    std::cout << "-- sending open event\n";
    door.Handle(OpenEvent {});

    std::cout << "-- sending open event\n";
    door.Handle(OpenEvent {});

    std::cout << "-- sending close event\n";
    door.Handle(CloseEvent {});
  } catch (const std::exception& err) {
    std::cerr << "An exception was thrown: " << err.what() << '\n';
  } catch (...) {
    std::cerr << "An unknown exception was thrown" << '\n';
  }
}
