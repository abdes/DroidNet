//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/StateMachine.h>

using oxygen::fsm::ByDefault;
using oxygen::fsm::Continue;
using oxygen::fsm::DoNothing;
using oxygen::fsm::Maybe;
using oxygen::fsm::On;
using oxygen::fsm::StateMachine;
using oxygen::fsm::Status;
using oxygen::fsm::TransitionTo;
using oxygen::fsm::Will;

namespace {

//! [Full State Machine Example]
/*
 * This example simulates a door with an electronic lock. When the door
 * is locked, the user chooses a lock code that needs to be re-entered
 * to unlock it.
 */

struct OpenEvent { };
struct CloseEvent { };

struct LockEvent {
  uint32_t newKey; // the lock code chosen by the user
};

struct UnlockEvent {
  uint32_t key; // the lock key entered when unlocking
};

struct ClosedState;
struct OpenState;
struct LockedState;

struct ClosedState
  : Will<ByDefault<DoNothing>, On<LockEvent, TransitionTo<LockedState>>,
      On<OpenEvent, TransitionTo<OpenState>>> { };

struct OpenState
  : Will<ByDefault<DoNothing>, On<CloseEvent, TransitionTo<ClosedState>>> { };

struct LockedState : ByDefault<DoNothing> {
  using ByDefault::Handle;

  explicit LockedState(uint32_t key)
    : key_(key)
  {
  }

  [[maybe_unused]] auto OnEnter(const LockEvent& event) -> Status
  {
    key_ = event.newKey;
    return Continue {};
  }

  //! [State Handle method]
  [[nodiscard]] [[maybe_unused]] auto Handle(const UnlockEvent& event) const
    -> Maybe<TransitionTo<ClosedState>>
  {
    if (event.key == key_) {
      return TransitionTo<ClosedState> {};
    }
    return DoNothing {};
  }
  //! [State Handle method]

private:
  uint32_t key_;
};

using Door = StateMachine<ClosedState, OpenState, LockedState>;

// NOLINTNEXTLINE
NOLINT_TEST(StateMachine, ExampleTest)
{
  Door door { ClosedState {}, OpenState {}, LockedState { 0 } };

  constexpr int lock_code = 1234;
  constexpr int bad_code = 2;

  door.Handle(LockEvent { lock_code });
  door.Handle(UnlockEvent { bad_code });
  door.Handle(UnlockEvent { lock_code });
}
//! [Full State Machine Example]

} // namespace
