//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/Internal/SceneNodeImportDefaults.h>
#include <Oxygen/Data/PakFormat_world.h>

namespace {

using oxygen::content::import::detail::BuildImportedNodeFlags;
namespace world = oxygen::data::pak::world;

NOLINT_TEST(SceneNodeImportDefaultsTest, EmptyHelperNodeKeepsOnlyVisibility)
{
  const auto visible_flags = BuildImportedNodeFlags(
    /*visible=*/true, /*has_renderable=*/false, /*has_light=*/false);
  EXPECT_EQ(visible_flags, world::kSceneNodeFlag_Visible);

  const auto hidden_flags = BuildImportedNodeFlags(
    /*visible=*/false, /*has_renderable=*/false, /*has_light=*/false);
  EXPECT_EQ(hidden_flags, 0U);
}

NOLINT_TEST(SceneNodeImportDefaultsTest,
  RenderableNodesCastAndReceiveByDefaultEvenWhenInitiallyHidden)
{
  const auto visible_flags = BuildImportedNodeFlags(
    /*visible=*/true, /*has_renderable=*/true, /*has_light=*/false);
  EXPECT_EQ(visible_flags,
    world::kSceneNodeFlag_Visible | world::kSceneNodeFlag_CastsShadows
      | world::kSceneNodeFlag_ReceivesShadows);

  const auto hidden_flags = BuildImportedNodeFlags(
    /*visible=*/false, /*has_renderable=*/true, /*has_light=*/false);
  EXPECT_EQ(hidden_flags,
    world::kSceneNodeFlag_CastsShadows | world::kSceneNodeFlag_ReceivesShadows);
}

NOLINT_TEST(SceneNodeImportDefaultsTest,
  LightNodesDefaultNodeLevelShadowCastingCapabilityOn)
{
  const auto visible_flags = BuildImportedNodeFlags(
    /*visible=*/true, /*has_renderable=*/false, /*has_light=*/true);
  EXPECT_EQ(visible_flags,
    world::kSceneNodeFlag_Visible | world::kSceneNodeFlag_CastsShadows);

  const auto hidden_flags = BuildImportedNodeFlags(
    /*visible=*/false, /*has_renderable=*/false, /*has_light=*/true);
  EXPECT_EQ(hidden_flags, world::kSceneNodeFlag_CastsShadows);
}

NOLINT_TEST(SceneNodeImportDefaultsTest,
  NodesWithRenderableAndLightKeepRenderableShadowDefaults)
{
  const auto flags = BuildImportedNodeFlags(
    /*visible=*/true, /*has_renderable=*/true, /*has_light=*/true);
  EXPECT_EQ(flags,
    world::kSceneNodeFlag_Visible | world::kSceneNodeFlag_CastsShadows
      | world::kSceneNodeFlag_ReceivesShadows);
}

} // namespace
