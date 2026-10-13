//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::scene::testing {

namespace {

  constexpr auto kSceneCapacity = 16U;

  class SceneActiveCameraTest : public ::testing::Test {
  protected:
    auto MakeCameraNode(const char* name) -> SceneNode
    {
      auto node = scene_->CreateNode(name);
      EXPECT_TRUE(node.AttachCamera(std::make_unique<PerspectiveCamera>()));
      return node;
    }

    [[nodiscard]] auto GetScene() const -> Scene& { return *scene_; }

  private:
    std::shared_ptr<Scene> scene_
      = std::make_shared<Scene>("active-camera", kSceneCapacity);
  };

} // namespace

//! A new scene has no active camera.
NOLINT_TEST_F(SceneActiveCameraTest, StartsWithoutActiveCamera)
{
  EXPECT_FALSE(GetScene().ActiveCamera().has_value());
}

//! A live camera node of the scene becomes the active camera.
NOLINT_TEST_F(SceneActiveCameraTest, SetsLiveCameraNode)
{
  auto first = MakeCameraNode("first");
  auto second = MakeCameraNode("second");

  ASSERT_TRUE(GetScene().SetActiveCamera(first));
  ASSERT_TRUE(GetScene().SetActiveCamera(second));

  const auto active = GetScene().ActiveCamera();
  if (!active.has_value()) {
    FAIL() << "Expected an active camera";
  }
  EXPECT_EQ(active->GetHandle(), second.GetHandle());
}

//! A node without a camera, or of another scene, is rejected and the active
//! camera stays as it was.
NOLINT_TEST_F(SceneActiveCameraTest, RejectsNodesThatCannotView)
{
  auto camera = MakeCameraNode("camera");
  auto plain = GetScene().CreateNode("plain");
  auto other_scene = std::make_shared<Scene>("other", kSceneCapacity);
  auto foreign = other_scene->CreateNode("foreign");
  ASSERT_TRUE(foreign.AttachCamera(std::make_unique<PerspectiveCamera>()));
  ASSERT_TRUE(GetScene().SetActiveCamera(camera));

  EXPECT_FALSE(GetScene().SetActiveCamera(plain));
  EXPECT_FALSE(GetScene().SetActiveCamera(foreign));

  const auto active = GetScene().ActiveCamera();
  if (!active.has_value()) {
    FAIL() << "Expected an active camera";
  }
  EXPECT_EQ(active->GetHandle(), camera.GetHandle());
}

//! The active camera ends when its node is destroyed or loses its camera.
NOLINT_TEST_F(SceneActiveCameraTest, EndsWithNodeOrCamera)
{
  auto destroyed = MakeCameraNode("destroyed");
  ASSERT_TRUE(GetScene().SetActiveCamera(destroyed));
  ASSERT_TRUE(GetScene().DestroyNode(destroyed));
  EXPECT_FALSE(GetScene().ActiveCamera().has_value());

  auto detached = MakeCameraNode("detached");
  ASSERT_TRUE(GetScene().SetActiveCamera(detached));
  ASSERT_TRUE(detached.DetachCamera());
  EXPECT_FALSE(GetScene().ActiveCamera().has_value());
}

//! Clearing leaves the scene without an active camera.
NOLINT_TEST_F(SceneActiveCameraTest, ClearRemovesActiveCamera)
{
  auto camera = MakeCameraNode("camera");
  ASSERT_TRUE(GetScene().SetActiveCamera(camera));

  GetScene().ClearActiveCamera();

  EXPECT_FALSE(GetScene().ActiveCamera().has_value());
}

} // namespace oxygen::scene::testing
