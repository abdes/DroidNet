//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::scene::camera::ProjectionConvention;

using testing::FloatEq;
using testing::Test;

namespace {

//! Testable camera, using the D3D12 convention, and exposing UpdateDependencies
//! for testing
class D3d12PerspectiveCamera final : public oxygen::scene::PerspectiveCamera {
public:
  D3d12PerspectiveCamera()
    : PerspectiveCamera(ProjectionConvention::kD3D12)
  {
  }

  using PerspectiveCamera::UpdateDependencies;
};

//! Testable camera, using the Vulkan convention, and exposing
//! UpdateDependencies for testing
class VulkanPerspectiveCamera final : public oxygen::scene::PerspectiveCamera {
public:
  VulkanPerspectiveCamera()
    : PerspectiveCamera(ProjectionConvention::kVulkan)
  {
  }

  using PerspectiveCamera::UpdateDependencies;
};

//! Fixture for basic PerspectiveCamera tests
class D3d12PerspectiveCameraTest : public Test {
protected:
  auto SetUp() -> void override
  {
    // Arrange: create camera and dummy transform
    camera_ = std::make_unique<D3d12PerspectiveCamera>();
    transform_ = std::make_unique<oxygen::scene::detail::TransformComponent>();
    // Simulate dependency injection
    camera_->UpdateDependencies(
      [this](oxygen::TypeId) -> oxygen::Component& { return *transform_; });
  }

  std::unique_ptr<D3d12PerspectiveCamera> camera_;
  std::unique_ptr<oxygen::scene::detail::TransformComponent> transform_;
};

//! Fixture for basic PerspectiveCamera tests
class VulkanPerspectiveCameraTest : public Test {
protected:
  auto SetUp() -> void override
  {
    // Arrange: create camera and dummy transform
    camera_ = std::make_unique<VulkanPerspectiveCamera>();
    transform_ = std::make_unique<oxygen::scene::detail::TransformComponent>();
    // Simulate dependency injection
    camera_->UpdateDependencies(
      [this](oxygen::TypeId) -> oxygen::Component& { return *transform_; });
  }

  std::unique_ptr<VulkanPerspectiveCamera> camera_;
  std::unique_ptr<oxygen::scene::detail::TransformComponent> transform_;
};

//! Test default construction and parameter accessors
TEST_F(D3d12PerspectiveCameraTest, DefaultParameters) // NOLINT(*-magic-numbers)
{
  // Assert
  EXPECT_FLOAT_EQ(camera_->GetFieldOfView(), 1.0f);
  EXPECT_FLOAT_EQ(camera_->GetAspectRatio(), 1.0f);
  EXPECT_FLOAT_EQ(camera_->GetNearPlane(), 0.1f);
  EXPECT_FLOAT_EQ(camera_->GetFarPlane(), 1000.0f);
  EXPECT_FALSE(camera_->GetViewport().has_value());
}

//! Test parameter setters and getters
TEST_F(D3d12PerspectiveCameraTest, SettersAndGetters) // NOLINT(*-magic-numbers)
{
  // Act
  camera_->SetFieldOfView(0.5f);
  camera_->SetAspectRatio(2.0f);
  camera_->SetNearPlane(0.5f);
  camera_->SetFarPlane(500.0f);
  oxygen::ViewPort vp { 10.f, 20.f, 640.f, 480.f, 0.f, 1.f };
  camera_->SetViewport(vp);
  // Assert
  EXPECT_FLOAT_EQ(camera_->GetFieldOfView(), 0.5f);
  EXPECT_FLOAT_EQ(camera_->GetAspectRatio(), 2.0f);
  EXPECT_FLOAT_EQ(camera_->GetNearPlane(), 0.5f);
  EXPECT_FLOAT_EQ(camera_->GetFarPlane(), 500.0f);
  ASSERT_TRUE(camera_->GetViewport().has_value());
  const auto vpr = camera_->GetViewport().value();
  EXPECT_FLOAT_EQ(vpr.top_left_x, 10.f);
  EXPECT_FLOAT_EQ(vpr.top_left_y, 20.f);
  EXPECT_FLOAT_EQ(vpr.width, 640.f);
  EXPECT_FLOAT_EQ(vpr.height, 480.f);
  EXPECT_FLOAT_EQ(vpr.min_depth, 0.f);
  EXPECT_FLOAT_EQ(vpr.max_depth, 1.f);
  camera_->ResetViewport();
  EXPECT_FALSE(camera_->GetViewport().has_value());
}

//! Test projection matrix calculation
TEST_F(D3d12PerspectiveCameraTest, ProjectionMatrix_Valid)
{
  // Act
  camera_->SetFieldOfView(glm::radians(90.0f));
  camera_->SetAspectRatio(1.0f);
  camera_->SetNearPlane(1.0f);
  camera_->SetFarPlane(100.0f);
  glm::mat4 proj = camera_->ProjectionMatrix();
  // Assert (check some known values for 90deg FOV)
  EXPECT_FLOAT_EQ(proj[1][1], 1.0f);
}

//! Test ActiveViewport returns correct value
TEST_F(D3d12PerspectiveCameraTest, ActiveViewport_ReturnsSetOrDefault)
{
  // Act & Assert
  {
    const auto avp = camera_->ActiveViewport();
    EXPECT_FLOAT_EQ(avp.top_left_x, 0.f);
    EXPECT_FLOAT_EQ(avp.top_left_y, 0.f);
    EXPECT_FLOAT_EQ(avp.width, 0.f);
    EXPECT_FLOAT_EQ(avp.height, 0.f);
    EXPECT_FLOAT_EQ(avp.min_depth, 0.f);
    EXPECT_FLOAT_EQ(avp.max_depth, 1.f);
  }
  camera_->SetViewport(oxygen::ViewPort { 1.f, 2.f, 3.f, 4.f, 0.f, 1.f });
  {
    const auto avp_set = camera_->ActiveViewport();
    EXPECT_FLOAT_EQ(avp_set.top_left_x, 1.f);
    EXPECT_FLOAT_EQ(avp_set.top_left_y, 2.f);
    EXPECT_FLOAT_EQ(avp_set.width, 3.f);
    EXPECT_FLOAT_EQ(avp_set.height, 4.f);
    EXPECT_FLOAT_EQ(avp_set.min_depth, 0.f);
    EXPECT_FLOAT_EQ(avp_set.max_depth, 1.f);
  }
}

//! Test ClippingRectangle returns correct near-plane extents
/*!
  Scenario: Camera at the origin, looking down -Z, with a 90-degree vertical
  field of view, aspect ratio 1.0, and near plane at 1.0. In this configuration:
  - The vertical field of view is 90°, so tan(45°) = 1.0.
  - The near plane is at z = -1.0 in view space.
  - The visible rectangle at the near plane is:
      left = -near * tan(fov/2) * aspect = -1.0
      right = +1.0
      bottom = -1.0
      top = +1.0
  - The expected rectangle is (-1, -1, 1, 1).
  This matches the canonical OpenGL/DirectX camera setup for a centered,
  symmetric frustum.
*/
TEST_F(D3d12PerspectiveCameraTest, ClippingRectangle_NearPlaneExtents)
{
  // Arrange
  camera_->SetFieldOfView(glm::radians(90.0f));
  camera_->SetAspectRatio(1.0f);
  camera_->SetNearPlane(1.0f);
  // Act
  glm::vec4 rect = camera_->ClippingRectangle();
  // Assert
  EXPECT_FLOAT_EQ(rect.x, -1.0f);
  EXPECT_FLOAT_EQ(rect.y, -1.0f);
  EXPECT_FLOAT_EQ(rect.z, 1.0f);
  EXPECT_FLOAT_EQ(rect.w, 1.0f);
}

//! Test projection matrix calculation for D3D12 and Vulkan conventions
TEST_F(VulkanPerspectiveCameraTest, ProjectionMatrix_Convention_Vulkan)
{
  // Arrange: Set parameters for a typical perspective projection
  camera_->SetFieldOfView(glm::radians(90.0f));
  camera_->SetAspectRatio(1.0f);
  camera_->SetNearPlane(1.0f);
  camera_->SetFarPlane(100.0f);

  // Vulkan convention (Y axis flipped)
  EXPECT_EQ(camera_->GetProjectionConvention(), ProjectionConvention::kVulkan);
  glm::mat4 proj_vk = camera_->ProjectionMatrix();
  // For Vulkan, proj[1][1] should be -1.0 (Y flipped)
  EXPECT_FLOAT_EQ(proj_vk[1][1], -1.0f);
}

//! Test projection matrix calculation for D3D12 and Vulkan conventions
TEST_F(D3d12PerspectiveCameraTest, ProjectionMatrix_Convention_D3D12)
{
  camera_->SetFieldOfView(glm::radians(90.0f));
  camera_->SetAspectRatio(1.0f);
  camera_->SetNearPlane(1.0f);
  camera_->SetFarPlane(100.0f);

  // D3D12 convention (default)
  EXPECT_EQ(camera_->GetProjectionConvention(), ProjectionConvention::kD3D12);
  glm::mat4 proj_d3d12 = camera_->ProjectionMatrix();
  // For 90deg FOV, aspect 1, near 1, far 100, proj[1][1] should be 1.0
  EXPECT_FLOAT_EQ(proj_d3d12[1][1], 1.0f);
}

} // namespace
