//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <glm/glm.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/ScenePrep/Extractors.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemProto.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

#include <Oxygen/Renderer/Test/Sceneprep/ScenePrepHelpers.h>
#include <Oxygen/Renderer/Test/Sceneprep/ScenePrepTestFixture.h>

using oxygen::View;

using oxygen::engine::sceneprep::ExtractionPreFilter;
using oxygen::engine::sceneprep::RenderItemProto;
using oxygen::engine::sceneprep::ScenePrepContext;
using oxygen::engine::sceneprep::ScenePrepState;

using oxygen::data::MaterialAsset;
using oxygen::data::MeshBuilder;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

using namespace oxygen::engine::sceneprep::testing;

namespace {

class ExtractionFilterTest : public ScenePrepTestFixture {
protected:
  auto SetUp() -> void override
  {
    // Delegate initialization to base fixture, then emulate the nullptr view
    // case used by the original test by placing a null-like context.
    ScenePrepTestFixture::SetUp();
    EmplaceContextWithView();
  }

  auto InvokeFilter()
  {
    return ExtractionPreFilter(Context(), State(), Proto());
  }
};

//! Verifies that when a scene node is marked invisible via its flags the
//! extraction pre-filter marks the proto as dropped and does not throw.
/*!
 Arrange: the node's effective visibility bit is set to false. Act: invoke the
 ExtractionPreFilter. Assert: the filter completes without throwing and the
 resulting proto is marked dropped.

 This test does not assume any further side-effects (such as changes to
 transform or geometry pointers) and only validates the "dropped" outcome.
*/
NOLINT_TEST_F(ExtractionFilterTest, InvisibleNode_Fails)
{
  // Mark node as invisible via flags
  Flags().SetFlag(SceneNodeFlags::kVisible,
    oxygen::scene::SceneFlag {}.SetEffectiveValueBit(false));

  // Act & Assert: should not throw and proto must be dropped
  NOLINT_EXPECT_NO_THROW(InvokeFilter());
  EXPECT_TRUE(Proto().IsDropped());
}

//! Verifies that a visible node with geometry passes the pre-filter and that
//! the proto is seeded with expected defaults.
/*!
 Arrange: geometry is prepared in the fixture SetUp() and the context uses a
 null-like view as configured by the fixture. Act: run ExtractionPreFilter.
 Assert: the proto is not dropped, is marked visible, has default shadow flags
 set, holds a non-null geometry pointer, and receives the expected world
 transform from the node.

 This test intentionally checks observable proto state only and avoids asserting
 on unspecified implementation details of proto initialization.
*/
NOLINT_TEST_F(ExtractionFilterTest, WithGeometry_PassesAndSeeds)
{
  // Geometry initialized in SetUp(); invoke filter and assert
  NOLINT_EXPECT_NO_THROW(ExtractionPreFilter(Context(), State(), Proto()));

  // Basic outcome checks
  EXPECT_FALSE(Proto().IsDropped());
  EXPECT_TRUE(Proto().IsVisible());

  // Default shadow flags expected for visible renderables in this fixture
  EXPECT_TRUE(Proto().CastsShadows());
  EXPECT_TRUE(Proto().ReceivesShadows());

  // Seeded geometry and transform
  EXPECT_NE(Proto().Geometry(), nullptr);
  EXPECT_EQ(Proto().GetWorldTransform(), WorldMatrix());
}

} // namespace
