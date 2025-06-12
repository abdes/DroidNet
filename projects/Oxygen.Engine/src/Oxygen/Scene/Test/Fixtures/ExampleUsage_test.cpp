//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

/// @file ExampleUsage_test.cpp
/// @brief Example demonstrating how to use the new Scene testing
/// infrastructure.
///
/// This file shows how the new testing fixtures reduce code duplication and
/// provide consistent testing patterns across the Scene module.

#include <Oxygen/Scene/Test/Fixtures/SceneTestFixtures.h>

using oxygen::scene::testing::SceneBasicTest;
using oxygen::scene::testing::SceneErrorTest;

//=============================================================================
// Example: Basic Scene Functionality Tests
//=============================================================================

/// Example of using the basic test fixture with built-in helpers.
class ExampleSceneBasicTest : public SceneBasicTest {
  // No additional setup needed - SceneTest provides scene_, helper methods,
  // etc.
};

NOLINT_TEST_F(ExampleSceneBasicTest, CreateNodeAndValidate)
{
  // Arrange: Scene is already set up by fixture

  // Act: Use built-in helper to create node
  auto node = CreateNode("ExampleNode");

  // Assert: Use built-in validation helper
  ExpectNodeValidWithName(node, "ExampleNode");
  ExpectSceneNodeCount(1);
}

NOLINT_TEST_F(ExampleSceneBasicTest, CreateComplexHierarchy)
{
  // Arrange & Act: Use built-in hierarchy creation helper
  auto hierarchy = CreateThreeLevelHierarchy("Root", "Middle", "Leaf");

  // Assert: Use built-in validation helpers
  ExpectNodeValidWithName(hierarchy.grandparent, "Root");
  ExpectNodeValidWithName(hierarchy.parent, "Middle");
  ExpectNodeValidWithName(hierarchy.child, "Leaf");
  ExpectNodeParent(hierarchy.parent, hierarchy.grandparent);
  ExpectNodeParent(hierarchy.child, hierarchy.parent);
  ExpectSceneNodeCount(3);
}

//=============================================================================
// Example: Error Testing with Categorized Fixtures
//=============================================================================

/// Example of using the error test fixture for invalid operations.
class ExampleSceneErrorTest : public SceneErrorTest {
  // Inherits all the same helpers as BasicTest but categorized for error
  // testing
};

NOLINT_TEST_F(ExampleSceneErrorTest, InvalidNodeOperations)
{
  // Arrange: Create node and invalidate it
  auto invalid_node = CreateLazyInvalidationNode("TestNode");

  // Act & Assert: Use built-in validation for invalid nodes
  ExpectNodeLazyInvalidated(invalid_node);
  ExpectNodeNotInScene(invalid_node);
}
