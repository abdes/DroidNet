//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Scene.h>

namespace oxygen::scene::testing {

//------------------------------------------------------------------------------
// Base Fixture for All SceneNode Tests
//------------------------------------------------------------------------------

class SceneNodeTestBase : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    static constexpr size_t kTestSceneCapacity = 1024;
    scene_
      = std::make_shared<oxygen::scene::Scene>("TestScene", kTestSceneCapacity);
  }
  auto TearDown() -> void override { scene_.reset(); }

  std::shared_ptr<oxygen::scene::Scene> scene_;
};

} // namespace oxygen::scene::testing
