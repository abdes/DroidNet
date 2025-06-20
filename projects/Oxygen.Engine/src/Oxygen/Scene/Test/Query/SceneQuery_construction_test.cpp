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

NOLINT_TEST_F(SceneQueryConstructionTest, Construction_WithNullScene_Aborts)
{
  // Arrange: Create null scene pointer
  std::shared_ptr<const Scene> null_scene = nullptr;

  // Act & Assert: Constructing SceneQuery with null scene should cause death
  EXPECT_DEATH(
    { auto query = std::make_unique<SceneQuery>(null_scene); },
    ".*"); // Match any death message
}

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

} // namespace
