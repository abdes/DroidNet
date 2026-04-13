//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Time/SimulationClock.h>
#include <Oxygen/Vortex/RenderContext.h>

namespace {

using oxygen::vortex::RenderContext;

TEST(RenderContextTest, ResetClearsPhase1PerViewAndFrameState)
{
  auto context = RenderContext {};
  context.pass_enable_flags.emplace(1U, true);
  context.current_view.view_id = oxygen::ViewId { 7U };
  context.current_view.exposure_view_id = oxygen::ViewId { 9U };
  context.frame_slot = oxygen::frame::Slot { 1U };
  context.frame_sequence = oxygen::frame::SequenceNumber { 3U };
  context.delta_time = 0.5F;
  context.view_outputs.emplace(oxygen::ViewId { 3U }, nullptr);

  context.Reset();

  EXPECT_TRUE(context.pass_enable_flags.empty());
  EXPECT_EQ(context.current_view.view_id, oxygen::ViewId {});
  EXPECT_EQ(context.current_view.exposure_view_id, oxygen::ViewId {});
  EXPECT_EQ(context.frame_slot, oxygen::frame::kInvalidSlot);
  EXPECT_EQ(context.frame_sequence, oxygen::frame::SequenceNumber {});
  EXPECT_TRUE(context.view_outputs.empty());
  EXPECT_EQ(
    context.delta_time, oxygen::time::SimulationClock::kMinDeltaTimeSeconds);
  EXPECT_EQ(context.pass_target.get(), nullptr);
  EXPECT_EQ(context.view_constants.get(), nullptr);
  EXPECT_EQ(context.material_constants.get(), nullptr);
  EXPECT_EQ(context.scene.get(), nullptr);
}

TEST(RenderContextTest, SceneAccessorsReflectAssignedScenePointer)
{
  auto context = RenderContext {};
  auto scene = oxygen::observer_ptr<oxygen::scene::Scene> {};
  context.scene = scene;

  EXPECT_EQ(context.GetSceneMutable().get(), scene.get());
  EXPECT_EQ(context.GetScene().get(), scene.get());
}

} // namespace
