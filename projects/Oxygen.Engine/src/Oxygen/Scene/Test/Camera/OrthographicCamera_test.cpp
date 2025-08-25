//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::scene::camera::ProjectionConvention;
using testing::FloatEq;
using testing::Test;

namespace {

//! Testable camera, using the D3D12 convention, and exposing UpdateDependencies
class D3d12OrthographicCamera final : public oxygen::scene::OrthographicCamera {
public:
  D3d12OrthographicCamera()
    : OrthographicCamera(ProjectionConvention::kD3D12)
  {
  }
  using OrthographicCamera::UpdateDependencies;
};

//! Testable camera, using the Vulkan convention, and exposing
//! UpdateDependencies
class VulkanOrthographicCamera final
  : public oxygen::scene::OrthographicCamera {
public:
  VulkanOrthographicCamera()
    : OrthographicCamera(ProjectionConvention::kVulkan)
  {
  }
  using OrthographicCamera::UpdateDependencies;
};

class D3d12OrthographicCameraTest : public Test {
protected:
  auto SetUp() -> void override
  {
    camera_ = std::make_unique<D3d12OrthographicCamera>();
    transform_ = std::make_unique<oxygen::scene::detail::TransformComponent>();
    camera_->UpdateDependencies(
      [this](oxygen::TypeId) -> oxygen::Component& { return *transform_; });
  }
  std::unique_ptr<D3d12OrthographicCamera> camera_;
  std::unique_ptr<oxygen::scene::detail::TransformComponent> transform_;
};

class VulkanOrthographicCameraTest : public Test {
protected:
  auto SetUp() -> void override
  {
    camera_ = std::make_unique<VulkanOrthographicCamera>();
    transform_ = std::make_unique<oxygen::scene::detail::TransformComponent>();
    camera_->UpdateDependencies(
      [this](oxygen::TypeId) -> oxygen::Component& { return *transform_; });
  }
  std::unique_ptr<VulkanOrthographicCamera> camera_;
  std::unique_ptr<oxygen::scene::detail::TransformComponent> transform_;
};

// -----------------------------------------------------------------------------
// OrthographicCamera: Basic Functionality
// -----------------------------------------------------------------------------

//! Default construction and parameter accessors
/*!
 Scenario: Construct a D3D12 orthographic camera and verify default extents and
 viewport.
*/
NOLINT_TEST_F(D3d12OrthographicCameraTest, DefaultParameters)
{
  // Arrange/Act: Camera is default constructed in SetUp

  // Assert
  EXPECT_EQ(camera_->GetExtents(),
    (std::array<float, 6> { -1, 1, -1, 1, 0.1f, 1000.0f }));
  EXPECT_FALSE(camera_->GetViewport().has_value());
}

//! Setters and getters for extents and viewport
/*!
 Scenario: Set and get orthographic extents and viewport, then reset viewport.

 This test simulates a real-world camera setup in a 2D editor or top-down game.
 The camera is configured to view a specific region of world space by setting
 its extents, and a custom viewport is assigned to render to a portion of the
 window (such as a minimap or UI panel). The test verifies that the camera's
 state updates as expected when these parameters are changed and reset, just as
 would occur in an actual application or engine.

 @see OrthographicCamera
*/
NOLINT_TEST_F(D3d12OrthographicCameraTest, SettersAndGetters)
{
  // Arrange

  // Act
  camera_->SetExtents(-2, 2, -3, 3, 0.5f, 500.0f);
  oxygen::ViewPort vp { 10.f, 20.f, 640.f, 480.f, 0.f, 1.f };
  camera_->SetViewport(vp);

  // Assert
  EXPECT_EQ(camera_->GetExtents(),
    (std::array<float, 6> { -2, 2, -3, 3, 0.5f, 500.0f }));
  ASSERT_TRUE(camera_->GetViewport().has_value());
  const auto vpr = camera_->GetViewport().value();
  EXPECT_FLOAT_EQ(vpr.top_left_x, 10.f);
  EXPECT_FLOAT_EQ(vpr.top_left_y, 20.f);
  EXPECT_FLOAT_EQ(vpr.width, 640.f);
  EXPECT_FLOAT_EQ(vpr.height, 480.f);
  EXPECT_FLOAT_EQ(vpr.min_depth, 0.f);
  EXPECT_FLOAT_EQ(vpr.max_depth, 1.f);

  // Act
  camera_->ResetViewport();

  // Assert
  EXPECT_FALSE(camera_->GetViewport().has_value());
}

//! Projection matrix calculation
/*! Scenario: Set extents and verify the projection matrix scale for D3D12. */
NOLINT_TEST_F(D3d12OrthographicCameraTest, ProjectionMatrix_Valid)
{
  // Arrange
  camera_->SetExtents(-2, 2, -2, 2, 1.0f, 100.0f);

  // Act
  glm::mat4 proj = camera_->ProjectionMatrix();

  // Assert
  EXPECT_FLOAT_EQ(proj[1][1], 0.5f)
    << "Orthographic Y scale should be 2/(top-bottom)";
}

//! ActiveViewport returns correct value
/*! Scenario: Check default and set viewport values. */
NOLINT_TEST_F(D3d12OrthographicCameraTest, ActiveViewport_ReturnsSetOrDefault)
{
  // Arrange/Act/Assert
  {
    const auto avp = camera_->ActiveViewport();
    EXPECT_FLOAT_EQ(avp.top_left_x, 0.f);
    EXPECT_FLOAT_EQ(avp.top_left_y, 0.f);
    EXPECT_FLOAT_EQ(avp.width, 0.f);
    EXPECT_FLOAT_EQ(avp.height, 0.f);
    EXPECT_FLOAT_EQ(avp.min_depth, 0.f);
    EXPECT_FLOAT_EQ(avp.max_depth, 1.f);
  }

  // Act
  camera_->SetViewport(oxygen::ViewPort { 1.f, 2.f, 3.f, 4.f, 0.f, 1.f });
  // Assert
  const auto avp_set = camera_->ActiveViewport();
  EXPECT_FLOAT_EQ(avp_set.top_left_x, 1.f);
  EXPECT_FLOAT_EQ(avp_set.top_left_y, 2.f);
  EXPECT_FLOAT_EQ(avp_set.width, 3.f);
  EXPECT_FLOAT_EQ(avp_set.height, 4.f);
  EXPECT_FLOAT_EQ(avp_set.min_depth, 0.f);
  EXPECT_FLOAT_EQ(avp_set.max_depth, 1.f);
}

//! ClippingRectangle returns correct near-plane extents
/*!
 Scenario: Set extents and verify the clipping rectangle at the near plane.
*/
NOLINT_TEST_F(D3d12OrthographicCameraTest, ClippingRectangle_NearPlaneExtents)
{
  // Arrange
  camera_->SetExtents(-1, 1, -1, 1, 1.0f, 100.0f);

  // Act
  glm::vec4 rect = camera_->ClippingRectangle();

  // Assert
  EXPECT_FLOAT_EQ(rect.x, -1.0f);
  EXPECT_FLOAT_EQ(rect.y, -1.0f);
  EXPECT_FLOAT_EQ(rect.z, 1.0f);
  EXPECT_FLOAT_EQ(rect.w, 1.0f);
}

//! Projection matrix calculation for Vulkan convention
/*!
 Scenario: Set extents and verify the Y-flip in the projection matrix for
 Vulkan.
*/
NOLINT_TEST_F(VulkanOrthographicCameraTest, ProjectionMatrix_Convention_Vulkan)
{
  // Arrange
  camera_->SetExtents(-2, 2, -2, 2, 1.0f, 100.0f);

  // Act
  EXPECT_EQ(camera_->GetProjectionConvention(), ProjectionConvention::kVulkan);
  glm::mat4 proj_vk = camera_->ProjectionMatrix();

  // Assert
  EXPECT_FLOAT_EQ(proj_vk[1][1], -0.5f)
    << "Vulkan Y scale should be negative (Y-flip)";
}

//! Projection matrix calculation for D3D12 convention
/*! Scenario: Set extents and verify the projection matrix for D3D12. */
NOLINT_TEST_F(D3d12OrthographicCameraTest, ProjectionMatrix_Convention_D3D12)
{
  // Arrange
  camera_->SetExtents(-2, 2, -2, 2, 1.0f, 100.0f);

  // Act
  EXPECT_EQ(camera_->GetProjectionConvention(), ProjectionConvention::kD3D12);
  glm::mat4 proj_d3d12 = camera_->ProjectionMatrix();

  // Assert
  EXPECT_FLOAT_EQ(proj_d3d12[1][1], 0.5f) << "D3D12 Y scale should be positive";
}

} // namespace
