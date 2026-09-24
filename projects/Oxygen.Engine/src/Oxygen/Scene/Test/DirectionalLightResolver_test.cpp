//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Scene.h>

namespace {
using namespace oxygen;
using namespace oxygen::scene;

class DirectionalLightResolverTest : public ::testing::Test {
protected:
  void SetUp() override { scene_ = std::make_shared<Scene>("Directional lights", 32U); }
  void Flush() { scene_->Update(false); scene_->SyncObservers(); }
  auto Add(std::string name, AtmosphereLightSlot slot = AtmosphereLightSlot::kNone) -> SceneNode
  {
    auto node = scene_->CreateNode(name);
    auto light = std::make_unique<DirectionalLight>();
    light->SetAtmosphereLightSlot(slot);
    EXPECT_TRUE(node.AttachLight(std::move(light)));
    Flush();
    return node;
  }
  std::shared_ptr<Scene> scene_;
};

NOLINT_TEST_F(DirectionalLightResolverTest, EmptySceneHasNoAtmosphericSources)
{
  const auto& resolver = scene_->GetDirectionalLightResolver();
  EXPECT_TRUE(resolver.ResolveDirectionalLights().empty());
  EXPECT_FALSE(resolver.ResolvePrimarySun());
  EXPECT_FALSE(resolver.ResolveSecondarySun());
}

NOLINT_TEST_F(DirectionalLightResolverTest, UnassignedLightsNeverBecomeAnImplicitSun)
{
  Add("Sun"); Add("Moon"); Add("Fill");
  const auto& resolver = scene_->GetDirectionalLightResolver();
  EXPECT_EQ(resolver.ResolveDirectionalLights().size(), 3U);
  EXPECT_FALSE(resolver.ResolvePrimarySun());
  EXPECT_FALSE(resolver.ResolveSecondarySun());
}

NOLINT_TEST_F(DirectionalLightResolverTest, SecondaryRetainsItsSlotWithoutPrimary)
{
  const auto secondary = Add("Secondary", AtmosphereLightSlot::kSecondary);
  Add("Unassigned");
  const auto& resolver = scene_->GetDirectionalLightResolver();
  EXPECT_FALSE(resolver.ResolvePrimarySun());
  ASSERT_TRUE(resolver.ResolveSecondarySun());
  EXPECT_EQ(resolver.ResolveSecondarySun()->NodeHandle(), secondary.GetHandle());
}

NOLINT_TEST_F(DirectionalLightResolverTest, BothAssignmentsRemainIndependentOfCollectionOrder)
{
  const auto secondary = Add("Secondary", AtmosphereLightSlot::kSecondary);
  Add("Fill");
  const auto primary = Add("Primary", AtmosphereLightSlot::kPrimary);
  const auto& slots = scene_->GetDirectionalLightResolver().ResolveAtmosphereLights();
  ASSERT_TRUE(slots.slots[0]); ASSERT_TRUE(slots.slots[1]);
  EXPECT_EQ(slots.slots[0]->NodeHandle(), primary.GetHandle());
  EXPECT_EQ(slots.slots[1]->NodeHandle(), secondary.GetHandle());
  EXPECT_EQ(slots.conflict_count, 0U);
}

NOLINT_TEST_F(DirectionalLightResolverTest, HiddenInactiveOwnerStillReservesItsSlot)
{
  auto owner = Add("Stored primary", AtmosphereLightSlot::kPrimary);
  owner.GetFlags()->get().SetLocalValue(SceneNodeFlags::kVisible, false);
  ASSERT_TRUE(owner.EditLight<DirectionalLight>([](auto& light) { light.Common().affects_world = false; }));
  Flush();
  EXPECT_FALSE(scene_->GetDirectionalLightResolver().ResolvePrimarySun());
  auto other = Add("Candidate");
  LightValidationError error;
  EXPECT_FALSE(other.EditLight<DirectionalLight>([](auto& light) {
    light.SetIntensityLux(123.0F);
    light.SetAtmosphereLightSlot(AtmosphereLightSlot::kPrimary);
  }, &error));
  EXPECT_EQ(error.conflicting_node, owner.GetHandle());
  EXPECT_NE(error.message.find("Stored primary"), std::string::npos);
  EXPECT_EQ(other.GetLightAs<DirectionalLight>()->get().GetIntensityLux(), 100000.0F);
  EXPECT_EQ(other.GetLightAs<DirectionalLight>()->get().GetAtmosphereLightSlot(), AtmosphereLightSlot::kNone);
}

NOLINT_TEST_F(DirectionalLightResolverTest, VisibilityOnlyEditsInvalidateResolvedSources)
{
  auto primary = Add("Primary", AtmosphereLightSlot::kPrimary);
  auto& resolver = scene_->GetDirectionalLightResolver();
  ASSERT_TRUE(resolver.ResolvePrimarySun());
  primary.GetFlags()->get().SetLocalValue(SceneNodeFlags::kVisible, false);
  Flush(); EXPECT_FALSE(resolver.ResolvePrimarySun());
  primary.GetFlags()->get().SetLocalValue(SceneNodeFlags::kVisible, true);
  Flush(); EXPECT_TRUE(resolver.ResolvePrimarySun());
}

NOLINT_TEST_F(DirectionalLightResolverTest, LocallyShownChildSurvivesHiddenParent)
{
  auto parent = scene_->CreateNode("Hidden parent");
  parent.GetFlags()->get().SetLocalValue(SceneNodeFlags::kVisible, false);
  auto child_result = scene_->CreateChildNode(parent, "Shown light");
  ASSERT_TRUE(child_result);
  auto child = *child_result;
  child.GetFlags()->get().SetLocalValue(SceneNodeFlags::kVisible, true);
  auto light = std::make_unique<DirectionalLight>();
  light->SetAtmosphereLightSlot(AtmosphereLightSlot::kPrimary);
  ASSERT_TRUE(child.AttachLight(std::move(light)));
  Flush();
  const auto source = scene_->GetDirectionalLightResolver().ResolvePrimarySun();
  ASSERT_TRUE(source); EXPECT_EQ(source->NodeHandle(), child.GetHandle());
}

NOLINT_TEST_F(DirectionalLightResolverTest, AcceptedEditNotifiesAndDetachReleasesAssignment)
{
  auto primary = Add("Primary", AtmosphereLightSlot::kPrimary);
  ASSERT_TRUE(primary.EditLight<DirectionalLight>([](auto& light) {
    light.SetAtmosphereLightSlot(AtmosphereLightSlot::kSecondary);
    light.Common().exposure_compensation_ev = 1.0F;
  }));
  Flush();
  auto& resolver = scene_->GetDirectionalLightResolver();
  EXPECT_FALSE(resolver.ResolvePrimarySun());
  ASSERT_TRUE(resolver.ResolveSecondarySun());
  EXPECT_EQ(resolver.ResolveSecondarySun()->Light().Common().exposure_compensation_ev, 1.0F);
  ASSERT_TRUE(primary.DetachLight());
  Add("Replacement", AtmosphereLightSlot::kSecondary);
  EXPECT_TRUE(resolver.ResolveSecondarySun());
}
} // namespace
