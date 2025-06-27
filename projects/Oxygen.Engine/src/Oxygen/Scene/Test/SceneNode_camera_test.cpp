//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "./SceneNode_test.h"

#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/SceneNode.h>

using oxygen::scene::OrthographicCamera;
using oxygen::scene::PerspectiveCamera;

using oxygen::scene::testing::SceneNodeTestBase;

namespace {

//------------------------------------------------------------------------------
// Camera Component Tests
//------------------------------------------------------------------------------

class SceneNodeCameraTest : public SceneNodeTestBase { };

//! Test that attaching a PerspectiveCamera works as expected.
NOLINT_TEST_F(SceneNodeCameraTest, AttachCamera_AttachesPerspectiveCamera)
{
  // Arrange
  auto node = scene_->CreateNode("CameraNode");
  auto camera = std::make_unique<PerspectiveCamera>(
    oxygen::scene::camera::ProjectionConvention::kD3D12);
  EXPECT_FALSE(node.HasCamera());

  // Act: Attach camera
  const bool attached = node.AttachCamera(std::move(camera));

  // Assert
  EXPECT_TRUE(attached);
  EXPECT_TRUE(node.HasCamera());
  const auto camera_ref = node.GetCameraAs<PerspectiveCamera>();
  ASSERT_TRUE(camera_ref.has_value());
  EXPECT_EQ(camera_ref->get().GetTypeId(),
    oxygen::scene::PerspectiveCamera::ClassTypeId());
}

//! Test that attaching an OrthographicCamera works as expected.
NOLINT_TEST_F(SceneNodeCameraTest, AttachCamera_AttachesOrthographicCamera)
{
  // Arrange
  auto node = scene_->CreateNode("CameraNode");
  auto camera = std::make_unique<OrthographicCamera>(
    oxygen::scene::camera::ProjectionConvention::kD3D12);
  EXPECT_FALSE(node.HasCamera());

  // Act: Attach camera
  const bool attached = node.AttachCamera(std::move(camera));

  // Assert
  EXPECT_TRUE(attached);
  EXPECT_TRUE(node.HasCamera());
  const auto camera_ref = node.GetCameraAs<OrthographicCamera>();
  ASSERT_TRUE(camera_ref.has_value());
  EXPECT_EQ(camera_ref->get().GetTypeId(),
    oxygen::scene::OrthographicCamera::ClassTypeId());
}

/*! Test that attaching a camera fails if one already exists.
    Scenario: Attach a second camera to a node that already has one. */
NOLINT_TEST_F(SceneNodeCameraTest, AttachCamera_FailsIfCameraAlreadyExists)
{
  // Arrange
  auto node = scene_->CreateNode("CameraNode");
  auto camera1 = std::make_unique<PerspectiveCamera>(
    oxygen::scene::camera::ProjectionConvention::kD3D12);
  auto camera2 = std::make_unique<PerspectiveCamera>(
    oxygen::scene::camera::ProjectionConvention::kD3D12);

  EXPECT_TRUE(node.AttachCamera(std::move(camera1)));
  EXPECT_TRUE(node.HasCamera());

  // Act: Try to attach another camera
  const bool attached = node.AttachCamera(std::move(camera2));

  // Assert
  EXPECT_FALSE(attached);
}

//! Test that DetachCamera removes the camera component from the node.
NOLINT_TEST_F(SceneNodeCameraTest, DetachCamera_RemovesCameraComponent)
{
  // Arrange
  auto node = scene_->CreateNode("CameraNode");
  auto camera = std::make_unique<PerspectiveCamera>(
    oxygen::scene::camera::ProjectionConvention::kD3D12);
  EXPECT_TRUE(node.AttachCamera(std::move(camera)));
  EXPECT_TRUE(node.HasCamera());

  // Act: Detach camera
  const bool detached = node.DetachCamera();

  // Assert
  EXPECT_TRUE(detached);
  EXPECT_FALSE(node.HasCamera());
  EXPECT_FALSE(node.GetCameraAs<PerspectiveCamera>().has_value());
}

//! Test that DetachCamera returns false if no camera is attached.
NOLINT_TEST_F(SceneNodeCameraTest, DetachCamera_NoCamera_ReturnsFalse)
{
  // Arrange
  auto node = scene_->CreateNode("CameraNode");
  EXPECT_FALSE(node.HasCamera());

  // Act: Detach camera when none is attached
  const bool detached = node.DetachCamera();

  // Assert
  EXPECT_FALSE(detached);
}

/*! Test that ReplaceCamera replaces an existing camera.
    Scenario: Replace a camera and verify the new one is present. */
NOLINT_TEST_F(SceneNodeCameraTest, ReplaceCamera_ReplacesExistingCamera)
{
  // Arrange
  auto node = scene_->CreateNode("CameraNode");
  auto camera1 = std::make_unique<PerspectiveCamera>(
    oxygen::scene::camera::ProjectionConvention::kD3D12);
  auto camera2 = std::make_unique<PerspectiveCamera>(
    oxygen::scene::camera::ProjectionConvention::kD3D12);

  EXPECT_TRUE(node.AttachCamera(std::move(camera1)));
  EXPECT_TRUE(node.HasCamera());

  // Act: Replace with a new camera
  const bool replaced = node.ReplaceCamera(std::move(camera2));

  // Assert
  EXPECT_TRUE(replaced);
  EXPECT_TRUE(node.HasCamera());
  const auto camera_ref = node.GetCameraAs<PerspectiveCamera>();
  ASSERT_TRUE(camera_ref.has_value());
  EXPECT_EQ(camera_ref->get().GetTypeId(),
    oxygen::scene::PerspectiveCamera::ClassTypeId());
}

//! Test that ReplaceCamera acts like Attach if no camera is present.
NOLINT_TEST_F(SceneNodeCameraTest, ReplaceCamera_NoCamera_ActsLikeAttach)
{
  // Arrange
  auto node = scene_->CreateNode("CameraNode");
  auto camera = std::make_unique<PerspectiveCamera>(
    oxygen::scene::camera::ProjectionConvention::kD3D12);
  EXPECT_FALSE(node.HasCamera());

  // Act: Replace camera when none is attached
  const bool replaced = node.ReplaceCamera(std::move(camera));

  // Assert
  EXPECT_TRUE(replaced);
  EXPECT_TRUE(node.HasCamera());
}

//! Test that GetCameraAs returns nullopt if no camera is attached.
NOLINT_TEST_F(SceneNodeCameraTest, GetCamera_ReturnsNulloptIfNoCamera)
{
  // Arrange
  auto node = scene_->CreateNode("CameraNode");
  EXPECT_FALSE(node.HasCamera());

  // Act & Assert
  EXPECT_FALSE(node.GetCameraAs<PerspectiveCamera>().has_value());
}

//! Test that HasCamera returns true if a camera is attached.
NOLINT_TEST_F(SceneNodeCameraTest, HasCamera_ReturnsTrueIfCameraAttached)
{
  // Arrange
  auto node = scene_->CreateNode("CameraNode");
  auto camera = std::make_unique<PerspectiveCamera>(
    oxygen::scene::camera::ProjectionConvention::kD3D12);
  EXPECT_FALSE(node.HasCamera());

  // Act
  node.AttachCamera(std::move(camera));

  // Assert
  EXPECT_TRUE(node.HasCamera());
}

//! Test that AttachCamera returns false if passed nullptr.
NOLINT_TEST_F(SceneNodeCameraTest, AttachCamera_Nullptr_ReturnsFalse)
{
  // Arrange
  auto node = scene_->CreateNode("CameraNode");
  std::unique_ptr<oxygen::Component> null_camera;

  // Act & Assert
  EXPECT_FALSE(node.AttachCamera(std::move(null_camera)));
}

//! Test that GetCameraAs returns correct type for PerspectiveCamera.
NOLINT_TEST_F(SceneNodeCameraTest, GetCameraAs_ReturnsCorrectType_Perspective)
{
  // Arrange
  auto node = scene_->CreateNode("CameraNode");
  auto camera = std::make_unique<PerspectiveCamera>(
    oxygen::scene::camera::ProjectionConvention::kD3D12);
  ASSERT_TRUE(node.AttachCamera(std::move(camera)));

  // Act: Get camera as PerspectiveCamera
  const auto cam_ref = node.GetCameraAs<PerspectiveCamera>();

  // Assert
  ASSERT_TRUE(cam_ref.has_value());
  EXPECT_EQ(cam_ref->get().GetTypeId(), PerspectiveCamera::ClassTypeId());
}

//! Test that GetCameraAs returns correct type for OrthographicCamera.
NOLINT_TEST_F(SceneNodeCameraTest, GetCameraAs_ReturnsCorrectType_Orthographic)
{
  // Arrange
  auto node = scene_->CreateNode("CameraNode");
  auto camera = std::make_unique<OrthographicCamera>(
    oxygen::scene::camera::ProjectionConvention::kD3D12);
  ASSERT_TRUE(node.AttachCamera(std::move(camera)));

  // Act: Get camera as OrthographicCamera
  const auto cam_ref = node.GetCameraAs<OrthographicCamera>();

  // Assert
  ASSERT_TRUE(cam_ref.has_value());
  EXPECT_EQ(cam_ref->get().GetTypeId(), OrthographicCamera::ClassTypeId());
}

//! Test that GetCameraAs returns nullopt if no camera is attached.
NOLINT_TEST_F(SceneNodeCameraTest, GetCameraAs_ReturnsNulloptIfNoCamera)
{
  // Arrange
  auto node = scene_->CreateNode("CameraNode");
  // Act
  const auto cam_ref = node.GetCameraAs<PerspectiveCamera>();
  // Assert
  EXPECT_FALSE(cam_ref.has_value());
}

//! Test that GetCameraAs returns nullopt on type mismatch.
NOLINT_TEST_F(SceneNodeCameraTest, GetCameraAs_ReturnsNullOptOnTypeMismatch)
{
  // Arrange
  auto node = scene_->CreateNode("CameraNode");
  auto camera = std::make_unique<PerspectiveCamera>(
    oxygen::scene::camera::ProjectionConvention::kD3D12);
  ASSERT_TRUE(node.AttachCamera(std::move(camera)));

  // Act
  const auto cam_ref = node.GetCameraAs<OrthographicCamera>();
  // Assert
  EXPECT_FALSE(cam_ref.has_value());
}

} // namespace
