//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <memory>

#include <glm/trigonometric.hpp>

#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
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
  ASSERT_TRUE(camera_ref.has_value());

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

  const auto resolved = resolver(ViewId { 7U });

  EXPECT_FLOAT_EQ(resolved.Viewport().width, override_viewport.width);
  EXPECT_FLOAT_EQ(resolved.Viewport().height, override_viewport.height);
  EXPECT_GT(resolved.FocalLengthPixels(), 0.0F);
}

} // namespace
