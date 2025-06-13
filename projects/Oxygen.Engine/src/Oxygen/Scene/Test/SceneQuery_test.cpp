//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneQuery.h>

using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneQuery;

namespace {

//===----------------------------------------------------------------------===//
// SceneQuery Basic Functionality Tests
//===----------------------------------------------------------------------===//

class SceneQueryBasicTest : public ::testing::Test {
protected:
  void SetUp() override { scene_ = std::make_shared<Scene>("TestScene", 128); }
  void TearDown() override { scene_.reset(); }
  std::shared_ptr<Scene> scene_;
};

NOLINT_TEST_F(SceneQueryBasicTest, FindFirst_ReturnsNodeWithMatchingName)
{
  // Arrange
  auto node = scene_->CreateNode("Player");
  SceneQuery query(scene_);

  // Act
  auto result = query.FindFirst([](const auto& visited) {
    return visited.node_impl->GetName() == "Player";
  });

  // Assert
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetName(), "Player");
}

} // namespace
