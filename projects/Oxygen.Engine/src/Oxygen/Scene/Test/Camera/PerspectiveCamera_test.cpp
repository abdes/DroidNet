//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Testing/GTest.h>

// ProjectionConvention removed; tests assume engine canonical projection

using testing::FloatEq;
using testing::Test;

NOLINT_TEST(PerspectiveCameraAspectTest, ResolvesPerTargetWithoutMutation)
{
  oxygen::scene::PerspectiveCamera camera;
  constexpr auto kFixedRatio = 4.0F / 3.0F;
  camera.SetAspectRatio(kFixedRatio);
  const oxygen::ViewPort wide { .width = 1920.0F, .height = 1080.0F };
  const oxygen::ViewPort square { .width = 1080.0F, .height = 1080.0F };
  const auto wide_projection = camera.ProjectionMatrix(wide);
  const auto square_projection = camera.ProjectionMatrix(square);
  EXPECT_FLOAT_EQ(
    glm::column(wide_projection, 1).y, glm::column(square_projection, 1).y);
  EXPECT_FLOAT_EQ(
    glm::column(wide_projection, 1).y / glm::column(wide_projection, 0).x,
    wide.width / wide.height);
  EXPECT_FLOAT_EQ(
    glm::column(square_projection, 1).y / glm::column(square_projection, 0).x,
    1.0F);
  EXPECT_FLOAT_EQ(camera.GetAspectRatio(), kFixedRatio);
  camera.SetAspectMode(oxygen::CameraAspectMode::kFixed);
  const auto fixed_projection = camera.ProjectionMatrix(wide);
  EXPECT_FLOAT_EQ(
    glm::column(fixed_projection, 1).y / glm::column(fixed_projection, 0).x,
    kFixedRatio);
  EXPECT_EQ(camera.ProjectionMatrix(wide), camera.ProjectionMatrix(square));
  EXPECT_FLOAT_EQ(camera.GetAspectRatio(), kFixedRatio);
}

namespace {

//! Testable camera, using the D3D12 convention, and exposing UpdateDependencies
//! for testing
class TestPerspectiveCamera final : public oxygen::scene::PerspectiveCamera {
public:
  TestPerspectiveCamera() = default;
  using PerspectiveCamera::UpdateDependencies;
};

//! Fixture for basic PerspectiveCamera tests
class D3d12PerspectiveCameraTest : public Test {
protected:
  auto SetUp() -> void override
  {
    // Arrange: create camera and dummy transform
    camera_ = std::make_unique<TestPerspectiveCamera>();
    transform_ = std::make_unique<oxygen::scene::detail::TransformComponent>();
    // Simulate dependency injection
    camera_->UpdateDependencies(
      [this](oxygen::TypeId) -> oxygen::Component& { return *transform_; });
  }

  std::unique_ptr<TestPerspectiveCamera> camera_;
  std::unique_ptr<oxygen::scene::detail::TransformComponent> transform_;
};

//! Fixture for basic PerspectiveCamera tests
class VulkanPerspectiveCameraTest : public Test {
protected:
  auto SetUp() -> void override
  {
    // Arrange: create camera and dummy transform
    camera_ = std::make_unique<TestPerspectiveCamera>();
    transform_ = std::make_unique<oxygen::scene::detail::TransformComponent>();
    // Simulate dependency injection
    camera_->UpdateDependencies(
      [this](oxygen::TypeId) -> oxygen::Component& { return *transform_; });
  }

  std::unique_ptr<TestPerspectiveCamera> camera_;
  std::unique_ptr<oxygen::scene::detail::TransformComponent> transform_;
};

//! Test default construction and parameter accessors
TEST_F(D3d12PerspectiveCameraTest, DefaultParameters) // NOLINT(*-magic-numbers)
{
  // Assert
  EXPECT_FLOAT_EQ(camera_->GetFieldOfView(), 1.0F);
  EXPECT_FLOAT_EQ(camera_->GetAspectRatio(), oxygen::kDefaultCameraAspectRatio);
  EXPECT_EQ(camera_->GetAspectMode(), oxygen::CameraAspectMode::kAuto);
  EXPECT_FLOAT_EQ(camera_->GetNearPlane(), 0.1F);
  EXPECT_FLOAT_EQ(camera_->GetFarPlane(), 1000.0F);
  EXPECT_FALSE(camera_->GetViewport().has_value());
}

//! Test parameter setters and getters
TEST_F(D3d12PerspectiveCameraTest, SettersAndGetters) // NOLINT(*-magic-numbers)
{
  // Act
  camera_->SetFieldOfView(0.5F);
  camera_->SetAspectRatio(2.0F);
  camera_->SetNearPlane(0.5F);
  camera_->SetFarPlane(500.0F);
  oxygen::ViewPort vp { 10.F, 20.F, 640.F, 480.F, 0.F, 1.F };
  camera_->SetViewport(vp);
  // Assert
  EXPECT_FLOAT_EQ(camera_->GetFieldOfView(), 0.5F);
  EXPECT_FLOAT_EQ(camera_->GetAspectRatio(), 2.0F);
  EXPECT_FLOAT_EQ(camera_->GetNearPlane(), 0.5F);
  EXPECT_FLOAT_EQ(camera_->GetFarPlane(), 500.0F);
  const auto viewport = camera_->GetViewport();
  if (!viewport) {
    FAIL() << "Expected viewport";
  }
  const auto vpr = *viewport;
  EXPECT_FLOAT_EQ(vpr.top_left_x, 10.F);
  EXPECT_FLOAT_EQ(vpr.top_left_y, 20.F);
  EXPECT_FLOAT_EQ(vpr.width, 640.F);
  EXPECT_FLOAT_EQ(vpr.height, 480.F);
  EXPECT_FLOAT_EQ(vpr.min_depth, 0.F);
  EXPECT_FLOAT_EQ(vpr.max_depth, 1.F);
  camera_->ResetViewport();
  EXPECT_FALSE(camera_->GetViewport().has_value());
}

//! Test projection matrix calculation
TEST_F(D3d12PerspectiveCameraTest, ProjectionMatrix_Valid)
{
  // Act
  camera_->SetFieldOfView(glm::radians(90.0F));
  camera_->SetAspectRatio(1.0F);
  camera_->SetNearPlane(1.0F);
  camera_->SetFarPlane(100.0F);
  glm::mat4 proj = camera_->ProjectionMatrix();
  // Assert (check some known values for 90deg FOV)
  EXPECT_FLOAT_EQ(glm::column(proj, 1).y, 1.0F);
}

//! Test ActiveViewport returns correct value
TEST_F(D3d12PerspectiveCameraTest, ActiveViewport_ReturnsSetOrDefault)
{
  // Act & Assert
  {
    const auto avp = camera_->ActiveViewport();
    EXPECT_FLOAT_EQ(avp.top_left_x, 0.F);
    EXPECT_FLOAT_EQ(avp.top_left_y, 0.F);
    EXPECT_FLOAT_EQ(avp.width, 0.F);
    EXPECT_FLOAT_EQ(avp.height, 0.F);
    EXPECT_FLOAT_EQ(avp.min_depth, 0.F);
    EXPECT_FLOAT_EQ(avp.max_depth, 1.F);
  }
  camera_->SetViewport(oxygen::ViewPort { 1.F, 2.F, 3.F, 4.F, 0.F, 1.F });
  {
    const auto avp_set = camera_->ActiveViewport();
    EXPECT_FLOAT_EQ(avp_set.top_left_x, 1.F);
    EXPECT_FLOAT_EQ(avp_set.top_left_y, 2.F);
    EXPECT_FLOAT_EQ(avp_set.width, 3.F);
    EXPECT_FLOAT_EQ(avp_set.height, 4.F);
    EXPECT_FLOAT_EQ(avp_set.min_depth, 0.F);
    EXPECT_FLOAT_EQ(avp_set.max_depth, 1.F);
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
  camera_->SetFieldOfView(glm::radians(90.0F));
  camera_->SetAspectRatio(1.0F);
  camera_->SetNearPlane(1.0F);
  // Act
  glm::vec4 rect = camera_->ClippingRectangle();
  // Assert
  EXPECT_FLOAT_EQ(rect.x, -1.0F);
  EXPECT_FLOAT_EQ(rect.y, -1.0F);
  EXPECT_FLOAT_EQ(rect.z, 1.0F);
  EXPECT_FLOAT_EQ(rect.w, 1.0F);
}

//! Test projection matrix calculation for D3D12 and Vulkan conventions
TEST_F(VulkanPerspectiveCameraTest, ProjectionMatrix_Convention_Vulkan)
{
  // Arrange: Set parameters for a typical perspective projection
  camera_->SetFieldOfView(glm::radians(90.0F));
  camera_->SetAspectRatio(1.0F);
  camera_->SetNearPlane(1.0F);
  camera_->SetFarPlane(100.0F);

  // Engine canonical projection: no Y-flip expected
  glm::mat4 proj_vk = camera_->ProjectionMatrix();
  EXPECT_FLOAT_EQ(glm::column(proj_vk, 1).y, 1.0F);
}

//! Test projection matrix calculation for D3D12 and Vulkan conventions
TEST_F(D3d12PerspectiveCameraTest, ProjectionMatrix_Convention_D3D12)
{
  camera_->SetFieldOfView(glm::radians(90.0F));
  camera_->SetAspectRatio(1.0F);
  camera_->SetNearPlane(1.0F);
  camera_->SetFarPlane(100.0F);

  glm::mat4 proj_d3d12 = camera_->ProjectionMatrix();
  // For 90deg FOV, aspect 1, near 1, far 100, glm::column(proj, 1).y should
  // be 1.0
  EXPECT_FLOAT_EQ(glm::column(proj_d3d12, 1).y, 1.0F);
}

} // namespace
