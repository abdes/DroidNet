//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/SceneNodeImportDefaults.h

#include <Oxygen/Cooker/Import/Internal/SceneNodeImportDefaults.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::detail::BuildImportedNodeFlags;
namespace world = oxygen::data::pak::world;

//! A visible node inherits visibility and both shadow flags, and holds no local
//! value: the instance root governs them.
NOLINT_TEST(
  SceneNodeImportDefaultsTest, VisibleNodeInheritsEveryInheritableFlag)
{
  const auto flags = BuildImportedNodeFlags(/*visible=*/true);

  EXPECT_EQ(flags.inherited_flags, world::kSceneNodeFlags_Inheritable);
  EXPECT_EQ(flags.node_flags, 0U);
}

//! A node the source hides keeps a local Hidden and inherits its shadow flags.
NOLINT_TEST(SceneNodeImportDefaultsTest, HiddenNodeKeepsLocalHidden)
{
  const auto flags = BuildImportedNodeFlags(/*visible=*/false);

  EXPECT_EQ(flags.inherited_flags,
    world::kSceneNodeFlag_CastsShadows | world::kSceneNodeFlag_ReceivesShadows);
  EXPECT_EQ(flags.node_flags & world::kSceneNodeFlag_Visible, 0U);
}

//! Static and ray-casting selectable have no source mode and stay off; every
//! result is a record the loader accepts.
NOLINT_TEST(SceneNodeImportDefaultsTest, FlagsAreCanonical)
{
  for (const auto visible : { true, false }) {
    const auto flags = BuildImportedNodeFlags(visible);
    auto record = world::NodeRecord {};
    record.node_flags = flags.node_flags;
    record.inherited_flags = flags.inherited_flags;

    EXPECT_TRUE(world::HasCanonicalNodeFlags(record)) << visible;
    EXPECT_EQ(flags.node_flags
        & (world::kSceneNodeFlag_Static
          | world::kSceneNodeFlag_RayCastingSelectable),
      0U)
      << visible;
  }
}

} // namespace
