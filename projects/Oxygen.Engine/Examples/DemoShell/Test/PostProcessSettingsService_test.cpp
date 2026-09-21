//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>
#include <memory>

#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/Services/PostProcessSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>

namespace oxygen::examples::testing {
namespace {
  class PostProcessSettingsServiceTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
      graphics_ = std::make_shared<vortex::testing::FakeGraphics>();
      graphics_->CreateCommandQueues(graphics::SingleQueueStrategy());
      auto config = RendererConfig {};
      config.upload_queue_key
        = graphics_->QueueKeyFor(graphics::QueueRole::kGraphics).get();
      renderer_ = std::make_unique<vortex::Renderer>(graphics_, config);
      service_.BindVortexRenderer(observer_ptr { renderer_.get() });
    }
    void TearDown() override
    {
      service_.BindVortexRenderer({});
      renderer_->OnShutdown();
    }
    auto Register(ViewId intent, std::uint64_t handle,
      ViewId source = kInvalidViewId) -> ViewId
    {
      auto view = engine::ViewContext {};
      view.metadata.exposure_view_id = source;
      return renderer_->UpsertPublishedRuntimeView(frame_, intent, view, {}, {},
        vortex::CompositionView::ViewStateHandle { handle });
    }
    std::shared_ptr<vortex::testing::FakeGraphics> graphics_;
    std::unique_ptr<vortex::Renderer> renderer_;
    engine::FrameContext frame_;
    ui::PostProcessSettingsService service_;
  };
} // namespace

NOLINT_TEST_F(PostProcessSettingsServiceTest,
  ResetSeedsEachRegisteredOwnerAndSkipsBorrowers)
{
  const auto first = Register(ViewId { 1U }, 11U);
  Register(ViewId { 2U }, 22U);
  Register(ViewId { 3U }, 33U, first);
  service_.ResetAutoExposure(8.0F);
  for (const auto handle : { 11U, 22U }) {
    const auto status = renderer_->InspectExposureTransition(
      vortex::CompositionView::ViewStateHandle { handle });
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->phase, vortex::ExposureTransitionPhase::kQueued);
    EXPECT_EQ(
      status->request.policy, vortex::ExposureTransitionPolicy::kSeedFromEv100);
    EXPECT_EQ(status->request.seed_ev, 8.0F);
  }
  EXPECT_FALSE(renderer_
      ->InspectExposureTransition(
        vortex::CompositionView::ViewStateHandle { 33U })
      .has_value());
  EXPECT_EQ(renderer_->GetExposureOwners(),
    (std::vector { vortex::CompositionView::ViewStateHandle { 11U },
      vortex::CompositionView::ViewStateHandle { 22U } }));
}

NOLINT_TEST_F(PostProcessSettingsServiceTest,
  RepeatedResetUsesNewRendererGenerationsAndInvalidSeedDoesNotReplaceIntent)
{
  Register(ViewId { 1U }, 11U);
  const auto handle = vortex::CompositionView::ViewStateHandle { 11U };
  service_.ResetAutoExposure(4.0F);
  const auto first = renderer_->InspectExposureTransition(handle)->request;
  service_.ResetAutoExposure(8.0F);
  const auto second = renderer_->InspectExposureTransition(handle)->request;
  EXPECT_GT(second.generation, first.generation);
  {
    const auto retry = renderer_->RetryExposureTransition(first);
    ASSERT_TRUE(retry.has_value());
    EXPECT_EQ(*retry, vortex::ExposureTransitionPhase::kSuperseded);
  }
  service_.ResetAutoExposure(std::numeric_limits<float>::infinity());
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->request, second);
  service_.BindVortexRenderer({});
  service_.ResetAutoExposure(12.0F);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->request, second);
}

NOLINT_TEST_F(PostProcessSettingsServiceTest,
  ResetDoesNotPersistIntentForFutureOrRetiredViews)
{
  service_.ResetAutoExposure(8.0F);
  Register(ViewId { 1U }, 11U);
  const auto handle = vortex::CompositionView::ViewStateHandle { 11U };
  EXPECT_FALSE(renderer_->InspectExposureTransition(handle).has_value());
  renderer_->RemovePublishedRuntimeView(frame_, ViewId { 1U });
  service_.ResetAutoExposure(8.0F);
  EXPECT_TRUE(renderer_->GetExposureOwners().empty());
  EXPECT_FALSE(renderer_->InspectExposureTransition(handle).has_value());
}
NOLINT_TEST(PostProcessAuthoringTest, InvalidEditsDoNotMutateSceneOrPreferences)
{
  const auto saved = SettingsService::ForDemoApp();
  saved->SetPersistenceEnabled(false);
  static_cast<void>(saved->Remove("post_process"));
  saved->SetFloat("post_process.exposure.key", 12.5F);
  auto world = std::make_unique<scene::Scene>("Exposure authoring", 16);
  ui::PostProcessSettingsService service;
  service.BindScene(observer_ptr { world.get() });
  const auto original = service.GetExposureSettings();
  service.SetAutoExposureRange({ .minimum = 12.0F, .maximum = 4.0F });
  EXPECT_EQ(service.GetExposureSettings(), original);
  EXPECT_FALSE(service.GetValidationError().empty());
  service.SetExposureKey(std::numeric_limits<float>::quiet_NaN());
  EXPECT_EQ(service.GetExposureSettings(), original);
  EXPECT_EQ(saved->GetFloat("post_process.exposure.key"), 12.5F);
  service.SetAutoExposureBlackInfluence(2.0F);
  service.SetAutoExposureTransitionDistance(0.0F);
  EXPECT_EQ(service.GetExposureSettings(), original);
  const auto invalid_curve = std::vector<scene::ExposureCompensationKey> {
    { .metered_ev = 1.0F, .compensation_ev = 0.0F },
    { .metered_ev = 1.0F, .compensation_ev = 1.0F },
  };
  EXPECT_FALSE(service.SetExposureCompensationCurve(invalid_curve));
  EXPECT_EQ(world->GetEnvironment()
              ->TryGetSystem<scene::environment::PostProcessVolume>()
              ->GetExposureSettings(),
    original);
  EXPECT_FALSE(saved->GetString("post_process.auto_exposure.compensation_curve")
      .has_value());
  service.SetExposureMode(engine::ExposureMode::kAuto);
  service.SetAutoExposureTargetLuminance(0.0F);
  service.SetAutoExposureAdaptationSpeedUp(0.0F);
  service.SetAutoExposureAdaptationSpeedDown(0.0F);
  EXPECT_FLOAT_EQ(service.GetAutoExposureTargetLuminance(), 0.0F);
  EXPECT_FLOAT_EQ(service.GetAutoExposureAdaptationSpeedUp(), 0.0F);
  EXPECT_FLOAT_EQ(service.GetAutoExposureAdaptationSpeedDown(), 0.0F);
}

NOLINT_TEST(PostProcessAuthoringTest,
  ExperimentActivationAndEditsPreservePersonalPreferences)
{
  const auto saved = SettingsService::ForDemoApp();
  saved->SetPersistenceEnabled(false);
  static_cast<void>(saved->Remove("post_process"));
  saved->SetFloat("post_process.exposure.mode", 0.0F);
  saved->SetFloat("post_process.exposure.manual_ev", 4.0F);
  auto world = std::make_unique<scene::Scene>("Owned recipe", 16);
  auto environment = std::make_unique<scene::SceneEnvironment>();
  auto& volume
    = environment->AddSystem<scene::environment::PostProcessVolume>();
  auto recipe = scene::ExposureSettings {};
  recipe.black_influence = 0.4F;
  recipe.transition_distance = 2.5F;
  recipe.compensation_curve = {
    { .metered_ev = -4.0F, .compensation_ev = 1.0F },
    { .metered_ev = 12.0F, .compensation_ev = -0.5F },
  };
  volume.SetExposureSettings(recipe);
  world->SetEnvironment(std::move(environment));
  ui::PostProcessSettingsService service;
  service.SetSceneActivationPolicy(SceneActivationPolicy::kExperimentOwned);
  service.BindScene(observer_ptr { world.get() });
  EXPECT_EQ(service.GetExposureSettings(), recipe);
  service.SetManualExposureEv(7.0F);
  service.SetExposureCompensation(0.5F);
  EXPECT_EQ(saved->GetFloat("post_process.exposure.manual_ev"), 4.0F);
  EXPECT_FALSE(
    saved->GetFloat("post_process.exposure.compensation").has_value());
  service.ResetToDefaults();
  EXPECT_EQ(volume.GetExposureSettings(), recipe);
  recipe.manual_ev = 10.0F;
  volume.SetExposureSettings(recipe);
  service.OnFrameStart();
  EXPECT_EQ(service.GetExposureSettings(), recipe);
  service.BindScene(nullptr);
  EXPECT_FALSE(service.GetExposureStatus().has_value());
  EXPECT_EQ(saved->GetFloat("post_process.exposure.mode"), 0.0F);
}

NOLINT_TEST(PostProcessAuthoringTest,
  SavedControlsRestoreWithoutCarryingResourceKeysAcrossScenes)
{
  const auto saved = SettingsService::ForDemoApp();
  saved->SetPersistenceEnabled(false);
  static_cast<void>(saved->Remove("post_process"));
  const auto make_scene = [](const std::uint64_t mask) {
    auto world = std::make_unique<scene::Scene>("Mask owner", 16);
    auto environment = std::make_unique<scene::SceneEnvironment>();
    auto& volume
      = environment->AddSystem<scene::environment::PostProcessVolume>();
    auto exposure = scene::ExposureSettings {};
    exposure.metering_mask = content::ResourceKey { mask };
    volume.SetExposureSettings(exposure);
    world->SetEnvironment(std::move(environment));
    return world;
  };
  auto first_scene = make_scene(41U);
  auto second_scene = make_scene(73U);
  ui::PostProcessSettingsService service;
  service.BindScene(observer_ptr { first_scene.get() });
  service.SetAutoExposureBlackInfluence(0.4F);
  service.SetAutoExposureTransitionDistance(2.5F);
  const auto curve = std::vector<scene::ExposureCompensationKey> {
    { .metered_ev = -4.0F, .compensation_ev = 1.0F },
    { .metered_ev = 12.0F, .compensation_ev = -0.5F },
  };
  ASSERT_TRUE(service.SetExposureCompensationCurve(curve));
  auto foreign = service.GetExposureSettings();
  foreign.metering_mask = content::ResourceKey { 99U };
  EXPECT_FALSE(service.TrySetExposureSettings(foreign));
  EXPECT_EQ(
    service.GetExposureSettings().metering_mask, content::ResourceKey { 41U });
  service.SetUseSceneMeteringMask(false);

  ui::PostProcessSettingsService restored;
  restored.BindScene(observer_ptr { second_scene.get() });
  const auto settings = restored.GetExposureSettings();
  EXPECT_FLOAT_EQ(settings.black_influence, 0.4F);
  EXPECT_FLOAT_EQ(settings.transition_distance, 2.5F);
  EXPECT_EQ(settings.compensation_curve, curve);
  EXPECT_EQ(settings.metering_mask.get(), 0U);
  restored.SetUseSceneMeteringMask(true);
  EXPECT_EQ(
    restored.GetExposureSettings().metering_mask, content::ResourceKey { 73U });
}

NOLINT_TEST(PostProcessAuthoringTest,
  CameraRecipeSurvivesActivationWithoutSavedPoseOrLensReplay)
{
  const auto saved = SettingsService::ForDemoApp();
  saved->SetPersistenceEnabled(false);
  saved->SetFloat("camera_rig.RecipeCamera.position.x", 90.0F);
  saved->SetBool("camera_rig.RecipeCamera.camera.has_perspective", true);
  saved->SetBool("camera_rig.RecipeCamera.camera.exposure.enabled", true);
  saved->SetFloat("camera_rig.RecipeCamera.camera.exposure.aperture_f", 2.0F);
  auto world = std::make_shared<scene::Scene>("Camera recipe", 16);
  auto node = world->CreateNode("RecipeCamera");
  auto camera = std::make_unique<scene::PerspectiveCamera>();
  camera->SetExposure(
    { .aperture_f = 8.0F, .shutter_rate = 60.0F, .iso = 200.0F });
  ASSERT_TRUE(node.AttachCamera(std::move(camera)));
  ASSERT_TRUE(node.GetTransform().SetLocalPosition({ 1.0F, 2.0F, 3.0F }));
  CameraSettingsService service;
  service.SetSceneActivationPolicy(SceneActivationPolicy::kExperimentOwned);
  service.OnRuntimeMainViewReady(node, ViewPort {});
  service.PersistActiveCameraSettings();
  EXPECT_EQ(node.GetTransform().GetLocalPosition(), Vec3(1.0F, 2.0F, 3.0F));
  const auto component = node.GetCameraAs<scene::PerspectiveCamera>();
  ASSERT_TRUE(component.has_value());
  EXPECT_FLOAT_EQ(component->get().Exposure().aperture_f, 8.0F);
  EXPECT_EQ(saved->GetFloat("camera_rig.RecipeCamera.position.x"), 90.0F);
  EXPECT_EQ(
    saved->GetFloat("camera_rig.RecipeCamera.camera.exposure.aperture_f"),
    2.0F);
  service.SetSceneActivationPolicy(SceneActivationPolicy::kRestorePreferences);
  service.OnSceneActivated(*world);
  service.OnRuntimeMainViewReady(node, ViewPort {});
  EXPECT_FLOAT_EQ(node.GetTransform().GetLocalPosition()->x, 90.0F);
  EXPECT_FLOAT_EQ(component->get().Exposure().aperture_f, 2.0F);
}

} // namespace oxygen::examples::testing
