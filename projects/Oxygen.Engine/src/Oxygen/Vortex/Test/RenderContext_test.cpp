//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <limits>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Time/SimulationClock.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Internal/PerViewScope.h>
#include <Oxygen/Vortex/RenderContext.h>

namespace {

using oxygen::ViewId;
using oxygen::vortex::CompositionView;
using oxygen::vortex::RenderContext;
using oxygen::vortex::internal::PerViewScope;

TEST(RenderContextTest, ResetClearsPhase1PerViewAndFrameState)
{
  auto context = RenderContext {};
  context.pass_enable_flags.emplace(1U, true);
  context.current_view.view_id = oxygen::ViewId {
    7U,
  };
  context.current_view.exposure_view_id = oxygen::ViewId {
    9U,
  };
  {
    auto& view_entry = context.frame_views.emplace_back();
    view_entry.view_id = oxygen::ViewId {
      5U,
    };
    view_entry.is_scene_view = true;
  }
  context.active_view_index = 0U;
  context.frame_slot = oxygen::frame::Slot {
    1U,
  };
  context.frame_sequence = oxygen::frame::SequenceNumber {
    3U,
  };
  context.delta_time = 0.5F;
  context.view_outputs.emplace(
    oxygen::ViewId {
      3U,
    },
    nullptr);

  context.Reset();

  EXPECT_TRUE(context.pass_enable_flags.empty());
  EXPECT_EQ(context.current_view.view_id, oxygen::kInvalidViewId);
  EXPECT_EQ(context.current_view.exposure_view_id, oxygen::kInvalidViewId);
  EXPECT_TRUE(context.frame_views.empty());
  EXPECT_EQ(context.active_view_index, std::numeric_limits<std::size_t>::max());
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

TEST(RenderContextTest, PerViewScopeCopiesFrameEntryAndRestoresCursor)
{
  auto context = RenderContext {};
  context.current_view.view_id = ViewId {
    99U,
  };
  {
    auto& view_entry = context.frame_views.emplace_back();
    view_entry.view_id = ViewId {
      1U,
    };
    view_entry.is_scene_view = true;
  }
  {
    auto& view_entry = context.frame_views.emplace_back();
    view_entry.view_id = ViewId {
      2U,
    };
    view_entry.exposure_view_id = ViewId {
      1U,
    };
    view_entry.view_state_handle = CompositionView::ViewStateHandle {
      20U,
    };
    view_entry.exposure_view_state_handle = CompositionView::ViewStateHandle {
      10U,
    };
    view_entry.is_scene_view = true;
    view_entry.is_reflection_capture = true;
    view_entry.with_atmosphere = true;
    view_entry.with_height_fog = true;
    view_entry.with_local_fog = true;
  }

  {
    PerViewScope scope {
      context,
      1U,
    };

    EXPECT_EQ(context.active_view_index, 1U);
    EXPECT_EQ(context.current_view.view_id,
      (ViewId {
        2U,
      }));
    EXPECT_EQ(context.current_view.exposure_view_id,
      (ViewId {
        1U,
      }));
    EXPECT_EQ(context.current_view.view_state_handle,
      (CompositionView::ViewStateHandle {
        20U,
      }));
    EXPECT_EQ(context.current_view.exposure_view_state_handle,
      (CompositionView::ViewStateHandle {
        10U,
      }));
    EXPECT_TRUE(context.current_view.is_reflection_capture);
    EXPECT_TRUE(context.current_view.with_atmosphere);
    EXPECT_TRUE(context.current_view.with_height_fog);
    EXPECT_TRUE(context.current_view.with_local_fog);
  }

  EXPECT_EQ(context.current_view.view_id,
    (ViewId {
      99U,
    }));
  EXPECT_EQ(context.active_view_index, std::numeric_limits<std::size_t>::max());
}

TEST(RenderContextTest, PerViewScopeRejectsNestedScope)
{
  auto context = RenderContext {};
  {
    auto& view_entry = context.frame_views.emplace_back();
    view_entry.view_id = ViewId {
      1U,
    };
    view_entry.is_scene_view = true;
  }

  EXPECT_DEATH_IF_SUPPORTED(([&context] -> void {
    [[maybe_unused]] PerViewScope outer {
      context,
      0U,
    };
    [[maybe_unused]] PerViewScope nested {
      context,
      0U,
    };
  }()),
    "Nested PerViewScope");
}

} // namespace
