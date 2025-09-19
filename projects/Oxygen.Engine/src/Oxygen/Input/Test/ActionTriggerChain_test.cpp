//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Tests for ActionTriggerChain

#include <memory>

#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::input::Action;
using oxygen::input::ActionTriggerChain;
using oxygen::input::ActionValue;
using oxygen::time::CanonicalDuration;
using namespace std::chrono_literals;

namespace {

NOLINT_TEST(ActionTriggerChain, TriggersWhenLinkedActionTriggers)
{
  auto linked
    = std::make_shared<Action>("A", oxygen::input::ActionValueType::kBool);
  ActionTriggerChain chain;
  chain.SetLinkedAction(linked);

  // Arrange: linked action triggers
  linked->BeginFrameTracking();
  linked->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    ActionValue { false });

  // Act: chain observes linked action ongoing, but needs local press to fire
  ActionValue press_down { true };
  chain.UpdateState(press_down, CanonicalDuration {});

  // Assert: chain fires on local press while prerequisite active
  EXPECT_TRUE(chain.IsTriggered());

  // No repeat while held
  chain.UpdateState(ActionValue { true }, CanonicalDuration {});
  EXPECT_FALSE(chain.IsTriggered());

  // Rising edge again should fire
  chain.UpdateState(ActionValue { false }, CanonicalDuration {});
  chain.UpdateState(ActionValue { true }, CanonicalDuration {});
  EXPECT_TRUE(chain.IsTriggered());
}

NOLINT_TEST(ActionTriggerChain, DoesNotTriggerWhenUnlinkedOrIdle)
{
  // Unlinked
  ActionTriggerChain chain_unlinked;
  ActionValue dummy { false };
  chain_unlinked.UpdateState(dummy, CanonicalDuration {});
  EXPECT_FALSE(chain_unlinked.IsTriggered());

  // Linked but idle
  auto linked
    = std::make_shared<Action>("B", oxygen::input::ActionValueType::kBool);
  ActionTriggerChain chain_linked;
  chain_linked.SetLinkedAction(linked);
  linked->BeginFrameTracking();
  // No trigger update on linked -> remains idle
  chain_linked.UpdateState(dummy, CanonicalDuration {});
  EXPECT_FALSE(chain_linked.IsTriggered());
  EXPECT_TRUE(chain_linked.IsIdle());

  // When prerequisite becomes active, still need local press
  linked->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    ActionValue { false });
  chain_linked.UpdateState(ActionValue { false }, CanonicalDuration {});
  EXPECT_FALSE(chain_linked.IsTriggered());
  chain_linked.UpdateState(ActionValue { true }, CanonicalDuration {});
  EXPECT_TRUE(chain_linked.IsTriggered());
}

NOLINT_TEST(ActionTriggerChain, ExpiresArmAfterMaxDelay)
{
  auto linked
    = std::make_shared<Action>("C", oxygen::input::ActionValueType::kBool);
  ActionTriggerChain chain;
  chain.SetLinkedAction(linked);
  chain.SetMaxDelaySeconds(0.1F); // 100 ms window

  // Arm by triggering prerequisite
  linked->BeginFrameTracking();
  linked->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    ActionValue { false });

  // Advance time beyond window without local press
  chain.UpdateState(ActionValue { false }, CanonicalDuration { 110ms });

  // Now press: should NOT fire because window expired
  chain.UpdateState(ActionValue { true }, CanonicalDuration {});
  EXPECT_FALSE(chain.IsTriggered());
}

NOLINT_TEST(ActionTriggerChain, RequiresPrerequisiteHeldOnPress)
{
  auto linked
    = std::make_shared<Action>("D", oxygen::input::ActionValueType::kBool);
  ActionTriggerChain chain;
  chain.SetLinkedAction(linked);
  chain.RequirePrerequisiteHeld(true);

  // Arm by triggering prerequisite, but then let it go idle
  linked->BeginFrameTracking();
  linked->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    ActionValue { false });
  // Simulate prerequisite going idle
  linked->UpdateState(
    Action::State {
      .triggered = false,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    ActionValue { false });

  // Even with local press, requirement of held prerequisite blocks firing
  chain.UpdateState(ActionValue { true }, CanonicalDuration {});
  EXPECT_FALSE(chain.IsTriggered());
}

} // namespace
