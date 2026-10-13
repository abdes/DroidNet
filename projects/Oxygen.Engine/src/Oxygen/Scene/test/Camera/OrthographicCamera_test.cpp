//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float4.hpp>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Testing/GTest.h>

// ProjectionConvention removed; tests assume engine canonical projection
using testing::FloatEq;
using testing::Test;

namespace {

class TestOrthographicCamera final : public oxygen::scene::OrthographicCamera {
public:
  TestOrthographicCamera() = default;
  using OrthographicCamera::UpdateDependencies;
};

class D3d12OrthographicCameraTest : public Test {
protected:
  auto SetUp() -> void override
  {
    camera_ = std::make_unique<TestOrthographicCamera>();
    transform_ = std::make_unique<oxygen::scene::detail::TransformComponent>();
    camera_->UpdateDependencies(
      [this](oxygen::TypeId) -> oxygen::Component& { return *transform_; });
  }
  std::unique_ptr<TestOrthographicCamera> camera_;
  std::unique_ptr<oxygen::scene::detail::TransformComponent> transform_;
};

class VulkanOrthographicCameraTest : public Test {
protected:
  auto SetUp() -> void override
  {
    camera_ = std::make_unique<TestOrthographicCamera>();
    transform_ = std::make_unique<oxygen::scene::detail::TransformComponent>();
    camera_->UpdateDependencies(
      [this](oxygen::TypeId) -> oxygen::Component& { return *transform_; });
  }
  std::unique_ptr<TestOrthographicCamera> camera_;
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
    (std::array<float, 6> { -1, 1, -1, 1, 0.1F, 1000.0F }));
  EXPECT_FALSE(camera_->GetViewport().has_value());
  EXPECT_EQ(camera_->GetAspectMode(), oxygen::CameraAspectMode::kAuto);
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
  camera_->SetExtents(-2, 2, -3, 3, 0.5F, 500.0F);
  oxygen::ViewPort vp { 10.F, 20.F, 640.F, 480.F, 0.F, 1.F };
  camera_->SetViewport(vp);

  // Assert
  EXPECT_EQ(camera_->GetExtents(),
    (std::array<float, 6> { -2, 2, -3, 3, 0.5F, 500.0F }));
  ASSERT_TRUE(camera_->GetViewport().has_value());
  const auto vpr = camera_->GetViewport().value();
  EXPECT_FLOAT_EQ(vpr.top_left_x, 10.F);
  EXPECT_FLOAT_EQ(vpr.top_left_y, 20.F);
  EXPECT_FLOAT_EQ(vpr.width, 640.F);
  EXPECT_FLOAT_EQ(vpr.height, 480.F);
  EXPECT_FLOAT_EQ(vpr.min_depth, 0.F);
  EXPECT_FLOAT_EQ(vpr.max_depth, 1.F);

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
  camera_->SetExtents(-2, 2, -2, 2, 1.0F, 100.0F);

  // Act
  glm::mat4 proj = camera_->ProjectionMatrix();

  // Assert
  EXPECT_FLOAT_EQ(proj[1][1], 0.5F)
    << "Orthographic Y scale should be 2/(top-bottom)";
}

//! Fixed keeps the authored extents whatever the target aspect.
NOLINT_TEST_F(D3d12OrthographicCameraTest, FixedFramingIgnoresTargetAspect)
{
  // Arrange
  camera_->SetExtents(-2, 2, -1, 1, 1.0F, 100.0F);
  camera_->SetAspectMode(oxygen::CameraAspectMode::kFixed);
  const oxygen::ViewPort wide { 0.F, 0.F, 1920.F, 1080.F, 0.F, 1.F };

  // Act
  const glm::mat4 proj = camera_->ProjectionMatrix(wide);

  // Assert
  EXPECT_FLOAT_EQ(proj[0][0], 0.5F);
  EXPECT_FLOAT_EQ(proj[1][1], 1.0F);
}

//! Auto keeps the vertical extents and centre, deriving width per target.
NOLINT_TEST_F(D3d12OrthographicCameraTest, AutoFramingDerivesWidthFromTarget)
{
  // Arrange
  camera_->SetExtents(0, 4, -1, 1, 1.0F, 100.0F);
  camera_->SetAspectMode(oxygen::CameraAspectMode::kAuto);
  auto expected = TestOrthographicCamera {};
  expected.SetExtents(1, 3, -1, 1, 1.0F, 100.0F);
  const oxygen::ViewPort square { 0.F, 0.F, 100.F, 100.F, 0.F, 1.F };
  const oxygen::ViewPort wide { 0.F, 0.F, 200.F, 100.F, 0.F, 1.F };

  // Act
  const glm::mat4 square_proj = camera_->ProjectionMatrix(square);
  const glm::mat4 wide_proj = camera_->ProjectionMatrix(wide);

  // Assert
  EXPECT_EQ(square_proj, expected.ProjectionMatrix());
  expected.SetExtents(0, 4, -1, 1, 1.0F, 100.0F);
  EXPECT_EQ(wide_proj, expected.ProjectionMatrix());
  EXPECT_EQ(
    camera_->GetExtents(), (std::array<float, 6> { 0, 4, -1, 1, 1.0F, 100.0F }))
    << "Resolving a target must not rewrite the authored extents";
}

//! Auto without a usable target keeps the authored extents.
NOLINT_TEST_F(D3d12OrthographicCameraTest, AutoFramingWithoutTargetKeepsExtents)
{
  // Arrange
  camera_->SetExtents(-2, 2, -1, 1, 1.0F, 100.0F);
  camera_->SetAspectMode(oxygen::CameraAspectMode::kAuto);

  // Act
  const glm::mat4 proj = camera_->ProjectionMatrix(oxygen::ViewPort {});

  // Assert
  EXPECT_FLOAT_EQ(proj[0][0], 0.5F);
}

//! ActiveViewport returns correct value
/*! Scenario: Check default and set viewport values. */
NOLINT_TEST_F(D3d12OrthographicCameraTest, ActiveViewport_ReturnsSetOrDefault)
{
  // Arrange/Act/Assert
  {
    const auto avp = camera_->ActiveViewport();
    EXPECT_FLOAT_EQ(avp.top_left_x, 0.F);
    EXPECT_FLOAT_EQ(avp.top_left_y, 0.F);
    EXPECT_FLOAT_EQ(avp.width, 0.F);
    EXPECT_FLOAT_EQ(avp.height, 0.F);
    EXPECT_FLOAT_EQ(avp.min_depth, 0.F);
    EXPECT_FLOAT_EQ(avp.max_depth, 1.F);
  }

  // Act
  camera_->SetViewport(oxygen::ViewPort { 1.F, 2.F, 3.F, 4.F, 0.F, 1.F });
  // Assert
  const auto avp_set = camera_->ActiveViewport();
  EXPECT_FLOAT_EQ(avp_set.top_left_x, 1.F);
  EXPECT_FLOAT_EQ(avp_set.top_left_y, 2.F);
  EXPECT_FLOAT_EQ(avp_set.width, 3.F);
  EXPECT_FLOAT_EQ(avp_set.height, 4.F);
  EXPECT_FLOAT_EQ(avp_set.min_depth, 0.F);
  EXPECT_FLOAT_EQ(avp_set.max_depth, 1.F);
}

//! ClippingRectangle returns correct near-plane extents
/*!
 Scenario: Set extents and verify the clipping rectangle at the near plane.
*/
NOLINT_TEST_F(D3d12OrthographicCameraTest, ClippingRectangle_NearPlaneExtents)
{
  // Arrange
  camera_->SetExtents(-1, 1, -1, 1, 1.0F, 100.0F);

  // Act
  glm::vec4 rect = camera_->ClippingRectangle();

  // Assert
  EXPECT_FLOAT_EQ(rect.x, -1.0F);
  EXPECT_FLOAT_EQ(rect.y, -1.0F);
  EXPECT_FLOAT_EQ(rect.z, 1.0F);
  EXPECT_FLOAT_EQ(rect.w, 1.0F);
}

//! Projection matrix calculation for Vulkan convention
/*!
 Scenario: Set extents and verify the Y-flip in the projection matrix for
 Vulkan.
*/
NOLINT_TEST_F(VulkanOrthographicCameraTest, ProjectionMatrix_Convention_Vulkan)
{
  // Arrange
  camera_->SetExtents(-2, 2, -2, 2, 1.0F, 100.0F);

  // Act
  glm::mat4 proj_vk = camera_->ProjectionMatrix();

  // Assert: engine canonical projection => no Y-flip
  EXPECT_FLOAT_EQ(proj_vk[1][1], 0.5F)
    << "Engine canonical Y scale should be positive";
}

//! Projection matrix calculation for D3D12 convention
/*! Scenario: Set extents and verify the projection matrix for D3D12. */
NOLINT_TEST_F(D3d12OrthographicCameraTest, ProjectionMatrix_Convention_D3D12)
{
  // Arrange
  camera_->SetExtents(-2, 2, -2, 2, 1.0F, 100.0F);

  // Act
  glm::mat4 proj_d3d12 = camera_->ProjectionMatrix();

  // Assert
  EXPECT_FLOAT_EQ(proj_d3d12[1][1], 0.5F)
    << "Engine canonical Y scale should be positive";
}

} // namespace
