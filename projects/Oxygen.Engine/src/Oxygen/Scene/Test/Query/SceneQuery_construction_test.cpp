//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "SceneQueryTestBase.h"

#include <Oxygen/Scene/SceneQuery.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::scene::ConstVisitedNode;
using oxygen::scene::Scene;
using oxygen::scene::SceneQuery;
using oxygen::scene::testing::SceneQueryTestBase;

namespace {

//=== Construction Test Fixture ===-------------------------------------------//

class SceneQueryConstructionTest : public SceneQueryTestBase {
protected:
  void SetUp() override
  {
    // Don't call base SetUp() - we need manual control for construction tests
    GetFactory().Reset();
  }
};

//=== Construction Tests ===--------------------------------------------------//

NOLINT_TEST_F(SceneQueryConstructionTest, Construction_WithValidScene_Succeeds)
{
  // Arrange: Create a valid scene using TestSceneFactory
  auto scene = GetFactory().CreateSingleNodeScene("ValidScene");
  ASSERT_NE(scene, nullptr);

  // Act: Construct SceneQuery with valid scene
  const auto query = std::make_unique<SceneQuery>(scene);

  // Assert: Query should be successfully constructed
  EXPECT_NE(query, nullptr);
}

//! Aborts when constructing SceneQuery with a null scene pointer.
NOLINT_TEST_F(SceneQueryConstructionTest, Construction_WithNullScene_Aborts)
{
  // Arrange: Create null scene pointer
  std::shared_ptr<const Scene> null_scene = nullptr;

  // Act & Assert: Constructing SceneQuery with null scene should cause death
  EXPECT_DEATH(
    { auto query = std::make_unique<SceneQuery>(null_scene); },
    ".*"); // Match any death message
}

//! Succeeds when constructing SceneQuery with an empty scene.
NOLINT_TEST_F(SceneQueryConstructionTest, Construction_WithEmptyScene_Succeeds)
{
  // Arrange: Create an empty scene using TestSceneFactory
  auto scene = GetFactory().CreateEmptyScene("EmptyScene");
  ASSERT_TRUE(scene->IsEmpty());

  // Act: Construct SceneQuery with empty scene
  const auto query = std::make_unique<SceneQuery>(scene);

  // Assert: Query should be successfully constructed
  EXPECT_NE(query, nullptr);
}

//! Verifies multiple SceneQuery instances can be constructed from the same
//! Scene.
NOLINT_TEST_F(
  SceneQueryConstructionTest, Construction_MultipleQueries_SameScene_Succeeds)
{
  // Arrange: Create a valid scene
  auto shared_scene = GetFactory().CreateSingleNodeScene("SharedScene");
  ASSERT_NE(shared_scene, nullptr);

  // Act: Construct multiple SceneQuery instances
  SceneQuery query1(shared_scene);
  SceneQuery query2(shared_scene);

  // Assert: Both queries should be valid and independent
  EXPECT_NE(&query1, &query2); // Ensure different objects
}

} // namespace
