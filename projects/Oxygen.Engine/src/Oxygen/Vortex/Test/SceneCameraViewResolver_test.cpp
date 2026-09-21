//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <glm/ext/quaternion_float.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/SceneCameraViewResolver.h>

namespace {

using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::scene::PerspectiveCamera;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::vortex::SceneCameraViewResolver;

TEST(SceneCameraViewResolverTest, UsesViewportOverrideWhenProvided)
{
  auto scene = std::make_shared<Scene>("resolver-scene", 4U);
  auto camera_node = scene->CreateNode("camera");
  ASSERT_TRUE(camera_node.AttachCamera(std::make_unique<PerspectiveCamera>()));

  auto camera_ref = camera_node.GetCameraAs<PerspectiveCamera>();
  if (!camera_ref.has_value()) {
    FAIL() << "Expected camera_ref to have a value";
  }

  auto& camera = camera_ref->get();
  camera.SetFieldOfView(glm::radians(60.0F));
  camera.SetAspectRatio(16.0F / 9.0F);
  camera.SetNearPlane(0.1F);
  camera.SetFarPlane(100.0F);
  camera.SetViewport(ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = 0.0F,
    .height = 0.0F,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  });
  scene->Update();

  const auto override_viewport = ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = 1920.0F,
    .height = 1080.0F,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };

  const auto resolver = SceneCameraViewResolver(
    [camera_node](const ViewId&) -> SceneNode { return camera_node; },
    override_viewport);

  const auto resolved = resolver(ViewId {
    7U,
  });

  EXPECT_FLOAT_EQ(resolved.Viewport().width, override_viewport.width);
  EXPECT_FLOAT_EQ(resolved.Viewport().height, override_viewport.height);
  EXPECT_GT(resolved.FocalLengthPixels(), 0.0F);
  EXPECT_EQ(resolved.Scissor().left, 0);
  EXPECT_EQ(resolved.Scissor().right, 1920);
  EXPECT_EQ(resolved.Scissor().bottom, 1080);
}

TEST(SceneCameraViewResolverTest, PreservesScissorWithNonzeroViewportOrigin)
{
  auto scene = std::make_shared<Scene>("resolver-inset", 4U);
  auto camera = scene->CreateNode("camera");
  ASSERT_TRUE(camera.AttachCamera(std::make_unique<PerspectiveCamera>()));
  const auto viewport = ViewPort {
    .top_left_x = 120.0F,
    .top_left_y = 80.0F,
    .width = 640.0F,
    .height = 360.0F,
  };
  const auto inset = oxygen::Scissors {
    .left = 136,
    .top = 104,
    .right = 728,
    .bottom = 400,
  };
  const auto lookup = [camera](const ViewId&) -> SceneNode { return camera; };
  const auto resolved
    = SceneCameraViewResolver(lookup, viewport, inset)(ViewId {
      3U,
    });
  EXPECT_EQ(resolved.Scissor().left, 136);
  EXPECT_EQ(resolved.Scissor().top, 104);
  EXPECT_EQ(resolved.Scissor().right, 728);
  EXPECT_EQ(resolved.Scissor().bottom, 400);
  const auto defaulted = SceneCameraViewResolver(lookup, viewport)(ViewId {
    3U,
  });
  EXPECT_EQ(defaulted.Scissor().left, 120);
  EXPECT_EQ(defaulted.Scissor().top, 80);
  EXPECT_EQ(defaulted.Scissor().right, 760);
  EXPECT_EQ(defaulted.Scissor().bottom, 440);
}

TEST(SceneCameraViewResolverTest, RootCameraCanResolveWithoutSceneUpdate)
{
  auto scene = std::make_shared<Scene>("resolver-root-camera", 4U);
  auto camera_node = scene->CreateNode("camera");
  ASSERT_TRUE(camera_node.AttachCamera(std::make_unique<PerspectiveCamera>()));

  camera_node.GetTransform().SetLocalPosition(oxygen::Vec3 {
    1.0F,
    -6.0F,
    3.0F,
  });
  camera_node.GetTransform().SetLocalRotation(
    glm::quat(glm::radians(oxygen::Vec3 {
      -20.0F,
      0.0F,
      0.0F,
    })));

  auto camera_ref = camera_node.GetCameraAs<PerspectiveCamera>();
  if (!camera_ref.has_value()) {
    FAIL() << "Expected camera_ref to have a value";
  }

  auto& camera = camera_ref->get();
  camera.SetFieldOfView(glm::radians(45.0F));
  camera.SetAspectRatio(16.0F / 9.0F);
  camera.SetNearPlane(0.1F);
  camera.SetFarPlane(600.0F);
  camera.SetViewport(ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = 1280.0F,
    .height = 720.0F,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  });

  const auto resolver = SceneCameraViewResolver(
    [camera_node](const ViewId&) -> SceneNode { return camera_node; });

  const auto resolved = resolver(ViewId {
    11U,
  });

  EXPECT_EQ(resolved.CameraPosition(), oxygen::Vec3(1.0F, -6.0F, 3.0F));
  EXPECT_FLOAT_EQ(resolved.Viewport().width, 1280.0F);
  EXPECT_FLOAT_EQ(resolved.Viewport().height, 720.0F);
  EXPECT_GT(resolved.FocalLengthPixels(), 0.0F);
}

} // namespace
