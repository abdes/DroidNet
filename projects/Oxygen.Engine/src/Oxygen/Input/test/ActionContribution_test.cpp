//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/test/ActionTestAccess.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::Axis1D;
using oxygen::Axis2D;
using oxygen::input::Action;
using oxygen::input::ActionAccumulation;
using oxygen::input::ActionValue;
using oxygen::input::ActionValueType;
using oxygen::input::testing::ActionTestAccess;

namespace {

// Two distinct binding identities.
const int kBindingA = 1;
const int kBindingB = 2;

constexpr Action::State kHeld { .triggered = true, .ongoing = true };
constexpr Action::State kReleased {};
constexpr Action::State kCanceled { .canceled = true };

//! Several bindings: the action stays ongoing while any of them is.
NOLINT_TEST(ActionContribution, OngoingWhileAnyBindingIsOngoing)
{
  Action action { "move", ActionValueType::kBool };
  ActionTestAccess::BeginFrame(action);
  ActionTestAccess::Contribute(action, &kBindingA, kHeld, ActionValue { true });
  ActionTestAccess::Contribute(action, &kBindingB, kHeld, ActionValue { true });
  ActionTestAccess::EndFrame(action);

  ActionTestAccess::BeginFrame(action);
  ActionTestAccess::Contribute(
    action, &kBindingB, kReleased, ActionValue { false });
  EXPECT_TRUE(action.IsOngoing());
  EXPECT_TRUE(action.GetValue().GetAs<bool>());
  EXPECT_FALSE(action.WasReleasedThisFrame());
  EXPECT_FALSE(action.WasCompletedThisFrame());
}

//! Triggered from an earlier frame does not carry over to this frame.
NOLINT_TEST(ActionContribution, TriggeredIsPerFrame)
{
  Action action { "fire", ActionValueType::kBool };
  ActionTestAccess::BeginFrame(action);
  ActionTestAccess::Contribute(action, &kBindingA, kHeld, ActionValue { true });
  ActionTestAccess::EndFrame(action);

  ActionTestAccess::BeginFrame(action);
  ActionTestAccess::Contribute(action, &kBindingB,
    Action::State { .ongoing = true }, ActionValue { true });
  EXPECT_FALSE(action.IsTriggered());
  EXPECT_TRUE(action.IsOngoing());
}

//! A cancel from one binding does not cancel an action another binding holds.
NOLINT_TEST(ActionContribution, CancelIgnoredWhileAnotherBindingIsActive)
{
  Action action { "fire", ActionValueType::kBool };
  ActionTestAccess::BeginFrame(action);
  ActionTestAccess::Contribute(action, &kBindingA, kHeld, ActionValue { true });
  ActionTestAccess::Contribute(
    action, &kBindingB, kCanceled, ActionValue { false });
  EXPECT_FALSE(action.WasCanceledThisFrame());
  EXPECT_TRUE(action.IsOngoing());
}

//! Highest-absolute keeps, per component, the value furthest from zero.
NOLINT_TEST(ActionContribution, HighestAbsolutePerComponent)
{
  Action action { "move", ActionValueType::kAxis2D };
  ASSERT_EQ(action.GetAccumulation(), ActionAccumulation::kHighestAbsolute);
  ActionTestAccess::BeginFrame(action);
  ActionTestAccess::Contribute(
    action, &kBindingA, kHeld, ActionValue { Axis2D { .x = 0.5F, .y = 1.0F } });
  constexpr float kStrongerX = -0.8F;
  constexpr float kWeakerY = 0.2F;
  ActionTestAccess::Contribute(action, &kBindingB, kHeld,
    ActionValue { Axis2D { .x = kStrongerX, .y = kWeakerY } });
  const auto value = action.GetValue().GetAs<Axis2D>();
  EXPECT_FLOAT_EQ(value.x, kStrongerX);
  EXPECT_FLOAT_EQ(value.y, 1.0F);
}

//! Cumulative sums the components, so opposing bindings cancel out.
NOLINT_TEST(ActionContribution, CumulativeSumsComponents)
{
  Action action { "throttle", ActionValueType::kAxis1D };
  action.SetAccumulation(ActionAccumulation::kCumulative);
  ActionTestAccess::BeginFrame(action);
  ActionTestAccess::Contribute(
    action, &kBindingA, kHeld, ActionValue { Axis1D { .x = 1.0F } });
  ActionTestAccess::Contribute(
    action, &kBindingB, kHeld, ActionValue { Axis1D { .x = -1.0F } });
  EXPECT_FLOAT_EQ(action.GetValue().GetAs<Axis1D>().x, 0.0F);
  EXPECT_TRUE(action.IsOngoing());
}

//! Removing the last contribution ends the activation.
NOLINT_TEST(ActionContribution, RemovingLastContributionCompletes)
{
  Action action { "move", ActionValueType::kBool };
  ActionTestAccess::BeginFrame(action);
  ActionTestAccess::Contribute(action, &kBindingA, kHeld, ActionValue { true });
  ActionTestAccess::EndFrame(action);

  ActionTestAccess::BeginFrame(action);
  ActionTestAccess::RemoveContribution(action, &kBindingA);
  EXPECT_FALSE(action.IsOngoing());
  EXPECT_FALSE(action.GetValue().GetAs<bool>());
  EXPECT_TRUE(action.WasCompletedThisFrame());
}

//! A trigger without a held phase completes at the start of the next frame.
NOLINT_TEST(ActionContribution, InstantTriggerCompletesNextFrame)
{
  Action action { "jump", ActionValueType::kBool };
  ActionTestAccess::BeginFrame(action);
  ActionTestAccess::Contribute(action, &kBindingA,
    Action::State { .triggered = true }, ActionValue { true });
  EXPECT_FALSE(action.WasCompletedThisFrame());
  ActionTestAccess::EndFrame(action);

  ActionTestAccess::BeginFrame(action);
  EXPECT_TRUE(action.WasCompletedThisFrame());
  ActionTestAccess::EndFrame(action);

  ActionTestAccess::BeginFrame(action);
  EXPECT_FALSE(action.WasCompletedThisFrame())
    << "one Completed per activation";
}

//! Each rising Triggered edge is latched once for fixed-step consumers.
NOLINT_TEST(ActionContribution, FixedStepLatchCountsRisingEdges)
{
  Action action { "jump", ActionValueType::kBool };
  constexpr Action::State kTap { .triggered = true };
  ActionTestAccess::BeginFrame(action);
  ActionTestAccess::Contribute(action, &kBindingA, kTap, ActionValue { true });
  ActionTestAccess::Contribute(action, &kBindingA, kTap, ActionValue { true });
  EXPECT_EQ(action.GetPendingFixedStepTriggers(), 1U) << "no new edge";
  ActionTestAccess::Contribute(
    action, &kBindingA, kReleased, ActionValue { false });
  ActionTestAccess::Contribute(action, &kBindingA, kTap, ActionValue { true });
  EXPECT_EQ(action.GetPendingFixedStepTriggers(), 2U);
  ActionTestAccess::EndFrame(action);

  // The latch survives frame boundaries until a fixed step consumes it.
  ActionTestAccess::BeginFrame(action);
  EXPECT_TRUE(action.ConsumeFixedStepTrigger());
  EXPECT_TRUE(action.ConsumeFixedStepTrigger());
  EXPECT_FALSE(action.ConsumeFixedStepTrigger());
}

} // namespace
