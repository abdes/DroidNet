//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "./SceneNode_test.h"

#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/SceneNode.h>
#include <limits>

using oxygen::scene::DirectionalLight;
using oxygen::scene::PointLight;
using oxygen::scene::SpotLight;

using oxygen::scene::testing::SceneNodeTestBase;

namespace {

//------------------------------------------------------------------------------
// Light Component Tests
//------------------------------------------------------------------------------

class SceneNodeLightTest : public SceneNodeTestBase { };

//! Test that attaching a DirectionalLight works as expected.
NOLINT_TEST_F(SceneNodeLightTest, AttachLight_AttachesDirectionalLight)
{
  // Arrange
  auto node = scene_->CreateNode("LightNode");
  auto light = std::make_unique<DirectionalLight>();
  EXPECT_FALSE(node.HasLight());

  // Act
  const bool attached = node.AttachLight(std::move(light));

  // Assert
  EXPECT_TRUE(attached);
  EXPECT_TRUE(node.HasLight());
  const auto light_ref = node.GetLightAs<DirectionalLight>();
  ASSERT_TRUE(light_ref.has_value());
  EXPECT_EQ(light_ref->get().GetTypeId(), DirectionalLight::ClassTypeId());
}

//! Test that attaching a PointLight works as expected.
NOLINT_TEST_F(SceneNodeLightTest, AttachLight_AttachesPointLight)
{
  // Arrange
  auto node = scene_->CreateNode("LightNode");
  auto light = std::make_unique<PointLight>();
  EXPECT_FALSE(node.HasLight());

  // Act
  const bool attached = node.AttachLight(std::move(light));

  // Assert
  EXPECT_TRUE(attached);
  EXPECT_TRUE(node.HasLight());
  const auto light_ref = node.GetLightAs<PointLight>();
  ASSERT_TRUE(light_ref.has_value());
  EXPECT_EQ(light_ref->get().GetTypeId(), PointLight::ClassTypeId());
}

//! Test that attaching a SpotLight works as expected.
NOLINT_TEST_F(SceneNodeLightTest, AttachLight_AttachesSpotLight)
{
  // Arrange
  auto node = scene_->CreateNode("LightNode");
  auto light = std::make_unique<SpotLight>();
  EXPECT_FALSE(node.HasLight());

  // Act
  const bool attached = node.AttachLight(std::move(light));

  // Assert
  EXPECT_TRUE(attached);
  EXPECT_TRUE(node.HasLight());
  const auto light_ref = node.GetLightAs<SpotLight>();
  ASSERT_TRUE(light_ref.has_value());
  EXPECT_EQ(light_ref->get().GetTypeId(), SpotLight::ClassTypeId());
}

/*! Test that attaching a light fails if one already exists.
    Scenario: Attach a second light (even a different type). */
NOLINT_TEST_F(SceneNodeLightTest, AttachLight_FailsIfLightAlreadyExists)
{
  // Arrange
  auto node = scene_->CreateNode("LightNode");
  auto light1 = std::make_unique<PointLight>();
  auto light2 = std::make_unique<DirectionalLight>();

  EXPECT_TRUE(node.AttachLight(std::move(light1)));
  EXPECT_TRUE(node.HasLight());

  // Act
  const bool attached = node.AttachLight(std::move(light2));

  // Assert
  EXPECT_FALSE(attached);
}

//! Test that DetachLight removes the light component from the node.
NOLINT_TEST_F(SceneNodeLightTest, DetachLight_RemovesLightComponent)
{
  // Arrange
  auto node = scene_->CreateNode("LightNode");
  auto light = std::make_unique<PointLight>();
  EXPECT_TRUE(node.AttachLight(std::move(light)));
  EXPECT_TRUE(node.HasLight());

  // Act
  const bool detached = node.DetachLight();

  // Assert
  EXPECT_TRUE(detached);
  EXPECT_FALSE(node.HasLight());
  EXPECT_FALSE(node.GetLightAs<PointLight>().has_value());
}

//! Test that DetachLight returns false if no light is attached.
NOLINT_TEST_F(SceneNodeLightTest, DetachLight_NoLight_ReturnsFalse)
{
  // Arrange
  auto node = scene_->CreateNode("LightNode");
  EXPECT_FALSE(node.HasLight());

  // Act
  const bool detached = node.DetachLight();

  // Assert
  EXPECT_FALSE(detached);
}

/*! Test that ReplaceLight replaces an existing light.
    Scenario: Replace a light with another light type and verify new one. */
NOLINT_TEST_F(SceneNodeLightTest, ReplaceLight_ReplacesExistingLight)
{
  // Arrange
  auto node = scene_->CreateNode("LightNode");
  auto light1 = std::make_unique<PointLight>();
  auto light2 = std::make_unique<DirectionalLight>();

  EXPECT_TRUE(node.AttachLight(std::move(light1)));
  EXPECT_TRUE(node.HasLight());

  // Act
  const bool replaced = node.ReplaceLight(std::move(light2));

  // Assert
  EXPECT_TRUE(replaced);
  EXPECT_TRUE(node.HasLight());
  EXPECT_TRUE(node.GetLightAs<DirectionalLight>().has_value());
  EXPECT_FALSE(node.GetLightAs<PointLight>().has_value());
}

//! Test that ReplaceLight acts like Attach if no light is present.
NOLINT_TEST_F(SceneNodeLightTest, ReplaceLight_NoLight_ActsLikeAttach)
{
  // Arrange
  auto node = scene_->CreateNode("LightNode");
  auto light = std::make_unique<SpotLight>();
  EXPECT_FALSE(node.HasLight());

  // Act
  const bool replaced = node.ReplaceLight(std::move(light));

  // Assert
  EXPECT_TRUE(replaced);
  EXPECT_TRUE(node.HasLight());
  EXPECT_TRUE(node.GetLightAs<SpotLight>().has_value());
}

//! Test that GetLightAs returns nullopt if no light is attached.
NOLINT_TEST_F(SceneNodeLightTest, GetLightAs_ReturnsNulloptIfNoLight)
{
  // Arrange
  auto node = scene_->CreateNode("LightNode");
  EXPECT_FALSE(node.HasLight());

  // Act
  const auto light_ref = node.GetLightAs<PointLight>();

  // Assert
  EXPECT_FALSE(light_ref.has_value());
}

//! Test that GetLightAs returns nullopt on type mismatch.
NOLINT_TEST_F(SceneNodeLightTest, GetLightAs_ReturnsNullOptOnTypeMismatch)
{
  // Arrange
  auto node = scene_->CreateNode("LightNode");
  auto light = std::make_unique<PointLight>();
  ASSERT_TRUE(node.AttachLight(std::move(light)));

  // Act
  const auto light_ref = node.GetLightAs<DirectionalLight>();

  // Assert
  EXPECT_FALSE(light_ref.has_value());
}

//! Test that AttachLight returns false if passed nullptr.
NOLINT_TEST_F(SceneNodeLightTest, AttachLight_Nullptr_ReturnsFalse)
{
  // Arrange
  auto node = scene_->CreateNode("LightNode");
  std::unique_ptr<oxygen::Component> null_light;

  // Act & Assert
  EXPECT_FALSE(node.AttachLight(std::move(null_light)));
}

NOLINT_TEST_F(SceneNodeLightTest, InvalidWholeCandidatePreservesLightAndMutationSequence)
{
  auto node = scene_->CreateNode("Atomic spot");
  ASSERT_TRUE(node.AttachLight(std::make_unique<SpotLight>()));
  scene_->SyncObservers();
  const auto sequence = scene_->GetMutationDispatchCounters().drained_records;
  oxygen::scene::LightValidationError error;
  EXPECT_FALSE(node.EditLight<SpotLight>([](auto& light) {
    light.SetRange(27.0F);
    light.SetConeAnglesRadians(0.8F, 0.4F);
    light.Common().color_rgb = { 0.2F, 0.3F, 0.4F };
  }, &error));
  EXPECT_EQ(error.field, "cone_angles");
  scene_->SyncObservers();
  EXPECT_EQ(scene_->GetMutationDispatchCounters().drained_records, sequence);
  const auto& accepted = node.GetLightAs<SpotLight>()->get();
  EXPECT_EQ(accepted.GetRange(), 10.0F);
  EXPECT_EQ(accepted.Common().color_rgb, oxygen::Vec3(1.0F));
  ASSERT_TRUE(node.EditLight<SpotLight>([](auto& light) {
    light.SetRange(0.0F);
    light.SetConeAnglesRadians(0.7F, 0.9F);
  }));
  EXPECT_EQ(node.GetLightAs<SpotLight>()->get().GetRange(), 0.0F);
  EXPECT_EQ(node.GetLightAs<SpotLight>()->get().GetInnerConeAngleRadians(), 0.7F);
}

NOLINT_TEST_F(SceneNodeLightTest, InvalidReplacementDoesNotDetachAcceptedComponent)
{
  auto node = scene_->CreateNode("Accepted point");
  ASSERT_TRUE(node.AttachLight(std::make_unique<PointLight>()));
  auto invalid = std::make_unique<DirectionalLight>();
  invalid->SetIntensityLux(std::numeric_limits<float>::infinity());
  EXPECT_FALSE(node.ReplaceLight(std::move(invalid)));
  EXPECT_TRUE(node.GetLightAs<PointLight>());
  EXPECT_FALSE(node.GetLightAs<DirectionalLight>());
}

} // namespace
