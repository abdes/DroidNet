//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <glm/ext/quaternion_float.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/SceneCameraViewResolver.h>

namespace {

using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::scene::OrthographicCamera;
using oxygen::scene::PerspectiveCamera;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::vortex::ResolveCameraContentRect;
using oxygen::vortex::SceneCameraViewResolver;

TEST(SceneCameraViewResolverTest, TargetAspectDoesNotRewriteAuthoredCamera)
{
  auto scene = std::make_shared<Scene>("resolver-aspect", 4U);
  auto camera_node = scene->CreateNode("camera");
  ASSERT_TRUE(camera_node.AttachCamera(std::make_unique<PerspectiveCamera>()));
  auto camera = camera_node.GetCameraAs<PerspectiveCamera>();
  if (!camera.has_value()) {
    FAIL() << "Expected camera to contain a value";
  }
  constexpr auto kAuthoredAspect = 4.0F / 3.0F;
  camera->get().SetAspectRatio(kAuthoredAspect);
  camera->get().SetAspectMode(oxygen::CameraAspectMode::kAuto);
  const auto lookup
    = [camera_node](const ViewId&) -> SceneNode { return camera_node; };
  const auto wide_target = ViewPort { .width = 1200.0F, .height = 600.0F };
  const auto square_target = ViewPort { .width = 600.0F, .height = 600.0F };
  const auto wide = SceneCameraViewResolver(lookup, wide_target)(ViewId { 1U });
  const auto square
    = SceneCameraViewResolver(lookup, square_target)(ViewId { 2U });

  EXPECT_FLOAT_EQ(glm::column(wide.ProjectionMatrix(), 1).y
      / glm::column(wide.ProjectionMatrix(), 0).x,
    2.0F);
  EXPECT_FLOAT_EQ(glm::column(square.ProjectionMatrix(), 1).y
      / glm::column(square.ProjectionMatrix(), 0).x,
    1.0F);
  EXPECT_FLOAT_EQ(glm::column(wide.ProjectionMatrix(), 1).y,
    glm::column(square.ProjectionMatrix(), 1).y);
  EXPECT_FLOAT_EQ(camera->get().GetAspectRatio(), kAuthoredAspect);

  camera->get().SetAspectMode(oxygen::CameraAspectMode::kFixed);
  const auto fixed
    = SceneCameraViewResolver(lookup, wide_target)(ViewId { 1U });
  EXPECT_FLOAT_EQ(glm::column(fixed.ProjectionMatrix(), 1).y
      / glm::column(fixed.ProjectionMatrix(), 0).x,
    kAuthoredAspect);
  EXPECT_FLOAT_EQ(camera->get().GetAspectRatio(), kAuthoredAspect);
}

TEST(SceneCameraViewResolverTest, OrthographicAutoFollowsTargetAspect)
{
  auto scene = std::make_shared<Scene>("resolver-ortho-aspect", 4U);
  auto camera_node = scene->CreateNode("camera");
  ASSERT_TRUE(camera_node.AttachCamera(std::make_unique<OrthographicCamera>()));
  auto camera = camera_node.GetCameraAs<OrthographicCamera>();
  if (!camera.has_value()) {
    FAIL() << "Expected camera to contain a value";
  }
  camera->get().SetExtents(-2.0F, 2.0F, -1.5F, 1.5F, 0.1F, 100.0F);
  camera->get().SetAspectMode(oxygen::CameraAspectMode::kAuto);
  const auto lookup
    = [camera_node](const ViewId&) -> SceneNode { return camera_node; };
  const auto wide_target = ViewPort { .width = 1200.0F, .height = 600.0F };
  const auto square_target = ViewPort { .width = 600.0F, .height = 600.0F };
  const auto wide = SceneCameraViewResolver(lookup, wide_target)(ViewId { 1U });
  const auto square
    = SceneCameraViewResolver(lookup, square_target)(ViewId { 2U });

  EXPECT_FLOAT_EQ(glm::column(wide.ProjectionMatrix(), 1).y
      / glm::column(wide.ProjectionMatrix(), 0).x,
    2.0F);
  EXPECT_FLOAT_EQ(glm::column(square.ProjectionMatrix(), 1).y
      / glm::column(square.ProjectionMatrix(), 0).x,
    1.0F);
  EXPECT_FLOAT_EQ(glm::column(wide.ProjectionMatrix(), 1).y,
    glm::column(square.ProjectionMatrix(), 1).y);
  EXPECT_FLOAT_EQ(camera->get().GetExtents().at(1), 2.0F);

  camera->get().SetAspectMode(oxygen::CameraAspectMode::kFixed);
  const auto fixed
    = SceneCameraViewResolver(lookup, wide_target)(ViewId { 1U });
  EXPECT_FLOAT_EQ(glm::column(fixed.ProjectionMatrix(), 1).y
      / glm::column(fixed.ProjectionMatrix(), 0).x,
    4.0F / 3.0F);
}

auto ExpectRect(const ViewPort& actual, const float x, const float y,
  const float width, const float height) -> void
{
  EXPECT_FLOAT_EQ(actual.top_left_x, x);
  EXPECT_FLOAT_EQ(actual.top_left_y, y);
  EXPECT_FLOAT_EQ(actual.width, width);
  EXPECT_FLOAT_EQ(actual.height, height);
}

//! Fixed fits the authored ratio in a centred integer rectangle; Auto fills.
TEST(SceneCameraViewResolverTest, ContentRectFitsFixedRatioWithBars)
{
  auto scene = std::make_shared<Scene>("resolver-content-rect", 4U);
  auto camera_node = scene->CreateNode("camera");
  ASSERT_TRUE(camera_node.AttachCamera(std::make_unique<PerspectiveCamera>()));
  auto camera = camera_node.GetCameraAs<PerspectiveCamera>();
  if (!camera.has_value()) {
    FAIL() << "Expected camera to contain a value";
  }
  const auto hd = ViewPort { .width = 1920.0F, .height = 1080.0F };
  const auto four_three = ViewPort { .width = 1440.0F, .height = 1080.0F };

  camera->get().SetAspectRatio(4.0F / 3.0F);
  camera->get().SetAspectMode(oxygen::CameraAspectMode::kAuto);
  ExpectRect(ResolveCameraContentRect(camera_node, hd), 0, 0, 1920, 1080);

  camera->get().SetAspectMode(oxygen::CameraAspectMode::kFixed);
  ExpectRect(ResolveCameraContentRect(camera_node, hd), 240, 0, 1440, 1080);
  ExpectRect(
    ResolveCameraContentRect(camera_node, four_three), 0, 0, 1440, 1080);

  camera->get().SetAspectRatio(16.0F / 9.0F);
  ExpectRect(
    ResolveCameraContentRect(camera_node, four_three), 0, 135, 1440, 810);
  ExpectRect(ResolveCameraContentRect(camera_node, hd), 0, 0, 1920, 1080);

  const auto offset = ViewPort {
    .top_left_x = 10.0F,
    .top_left_y = 20.0F,
    .width = 1440.0F,
    .height = 1080.0F,
  };
  ExpectRect(ResolveCameraContentRect(camera_node, offset), 10, 155, 1440, 810);
  EXPECT_FLOAT_EQ(camera->get().GetAspectRatio(), 16.0F / 9.0F)
    << "Framing must not rewrite the authored ratio";
}

//! Orthographic Fixed framing uses the ratio of its authored extents.
TEST(SceneCameraViewResolverTest, OrthographicContentRectUsesExtentsRatio)
{
  auto scene = std::make_shared<Scene>("resolver-ortho-content-rect", 4U);
  auto camera_node = scene->CreateNode("camera");
  ASSERT_TRUE(camera_node.AttachCamera(std::make_unique<OrthographicCamera>()));
  auto camera = camera_node.GetCameraAs<OrthographicCamera>();
  if (!camera.has_value()) {
    FAIL() << "Expected camera to contain a value";
  }
  camera->get().SetExtents(-2.0F, 2.0F, -1.5F, 1.5F, 0.1F, 100.0F);
  const auto hd = ViewPort { .width = 1920.0F, .height = 1080.0F };

  ExpectRect(ResolveCameraContentRect(camera_node, hd), 0, 0, 1920, 1080);
  camera->get().SetAspectMode(oxygen::CameraAspectMode::kFixed);
  ExpectRect(ResolveCameraContentRect(camera_node, hd), 240, 0, 1440, 1080);
}

//! Nodes without a camera, and invalid targets, are returned unchanged.
TEST(SceneCameraViewResolverTest, ContentRectWithoutCameraFillsTarget)
{
  auto scene = std::make_shared<Scene>("resolver-no-camera", 4U);
  const auto node = scene->CreateNode("empty");
  const auto target = ViewPort { .width = 640.0F, .height = 480.0F };

  ExpectRect(ResolveCameraContentRect(node, target), 0, 0, 640, 480);
  ExpectRect(ResolveCameraContentRect(node, ViewPort {}), 0, 0, 0, 0);
}

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
