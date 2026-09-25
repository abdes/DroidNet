//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <stdexcept>
#include <string>

#include "DemoShell/Internal/PostProcessConsoleBindings.h"
#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/Services/PostProcessSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Console/Console.h>
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
  using Status = console::ExecutionStatus;
  class PostProcessConsoleTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
      SettingsService::ForDemoApp()->SetPersistenceEnabled(false);
      graphics = std::make_shared<vortex::testing::FakeGraphics>();
      graphics->CreateCommandQueues(graphics::SingleQueueStrategy());
      auto config = RendererConfig {};
      config.upload_queue_key
        = graphics->QueueKeyFor(graphics::QueueRole::kGraphics).get();
      renderer = std::make_unique<vortex::Renderer>(graphics, config);
      world = std::make_shared<scene::Scene>("Console owner", 16);
      auto environment = std::make_unique<scene::SceneEnvironment>();
      auto& post
        = environment->AddSystem<scene::environment::PostProcessVolume>();
      auto exposure = scene::ExposureSettings {};
      exposure.mode = engine::ExposureMode::kManual;
      exposure.metering_mask = content::ResourceKey { 41U };
      post.SetExposureSettings(exposure);
      world->SetEnvironment(std::move(environment));
      auto camera = world->CreateNode("ConsoleCamera");
      ASSERT_TRUE(
        camera.AttachCamera(std::make_unique<scene::PerspectiveCamera>()));
      cameras.SetSceneActivationPolicy(SceneActivationPolicy::kExperimentOwned);
      cameras.OnRuntimeMainViewReady(camera, ViewPort {});
      settings.SetSceneActivationPolicy(
        SceneActivationPolicy::kExperimentOwned);
      settings.BindCameraSettings(observer_ptr { &cameras });
      settings.BindScene(observer_ptr { world.get() });
      settings.BindMainView(ViewId { 1U });
      settings.BindVortexRenderer(observer_ptr { renderer.get() });
      target = "scene:" + std::to_string(settings.GetSceneRevision());
      const auto first = renderer->UpsertPublishedRuntimeView(frame,
        ViewId { 1U }, engine::ViewContext {}, {}, {},
        vortex::CompositionView::ViewStateHandle { 11U });
      auto borrower = engine::ViewContext {};
      borrower.metadata.exposure_view_id = first;
      renderer->UpsertPublishedRuntimeView(frame, ViewId { 2U }, borrower, {},
        {}, vortex::CompositionView::ViewStateHandle { 22U });
      Bind();
    }
    void TearDown() override
    {
      bindings.reset();
      settings.BindVortexRenderer({});
      renderer->OnShutdown();
    }
    auto Bind() -> void
    {
      bindings = std::make_unique<internal::PostProcessConsoleBindings>(
        console, settings, [this] { return observer_ptr { renderer.get() }; });
    }
    auto Execute(const std::string& command) -> console::ExecutionResult
    {
      return console.Execute(command,
        { .source = console::CommandSource::kAutomation,
          .shipping_build = true,
          .record_history = false });
    }
    console::Console console;
    std::shared_ptr<vortex::testing::FakeGraphics> graphics;
    std::unique_ptr<vortex::Renderer> renderer;
    std::shared_ptr<scene::Scene> world;
    engine::FrameContext frame;
    CameraSettingsService cameras;
    ui::PostProcessSettingsService settings;
    std::unique_ptr<internal::PostProcessConsoleBindings> bindings;
    std::string target;
  };
} // namespace

NOLINT_TEST_F(
  PostProcessConsoleTest, DiscoversTargetsAndUnregistersOnOwnerDestruction)
{
  EXPECT_EQ(console.Complete("pp.").size(), 8U);
  const auto listed = Execute("pp.targets");
  EXPECT_EQ(listed.status, Status::kOk);
  EXPECT_NE(listed.output.find(target), std::string::npos);
  EXPECT_NE(listed.output.find("11"), std::string::npos);
  const auto inspected = Execute("pp.inspect " + target);
  EXPECT_EQ(inspected.status, Status::kOk);
  EXPECT_NE(
    inspected.output.find("renderer acceptance unavailable/pending capture"),
    std::string::npos);
  EXPECT_NE(inspected.output.find("aperture_f="), std::string::npos);
  bindings.reset();
  EXPECT_TRUE(console.Complete("pp.").empty());
  EXPECT_EQ(Execute("pp.targets").status, Status::kNotFound);
  Bind();
  EXPECT_EQ(Execute("pp.targets").status, Status::kOk);
}

NOLINT_TEST_F(
  PostProcessConsoleTest, RejectsInvalidPatchesAtomicallyAndRejectsStaleScenes)
{
  const auto before = settings.GetExposureSettings();
  const auto epoch = settings.GetEpoch();
  for (const auto* invalid :
    { "manual_ev=4 min_ev=12 max_ev=2", "manual_ev=4 nonexistent=1",
      "manual_ev=4 key=0", "manual_ev=nan", "manual_ev=4 manual_ev=5",
      "manual_ev=", "enabled=maybe", "mode=unknown" }) {
    EXPECT_EQ(Execute("pp.exposure " + target + " " + invalid).status,
      Status::kInvalidArguments);
    EXPECT_EQ(settings.GetExposureSettings(), before);
    EXPECT_EQ(settings.GetEpoch(), epoch);
  }
  EXPECT_EQ(Execute("pp.exposure " + target
              + " mode=auto min_ev=-4 max_ev=14 compensation_ev=+1")
              .status,
    Status::kOk);
  EXPECT_EQ(settings.GetExposureMode(), engine::ExposureMode::kAuto);
  EXPECT_FLOAT_EQ(settings.GetAutoExposureMinEv(), -4);
  EXPECT_FLOAT_EQ(settings.GetAutoExposureMaxEv(), 14);
  settings.BindScene({});
  EXPECT_EQ(Execute("pp.exposure " + target + " manual_ev=4").status,
    Status::kInvalidArguments);
  settings.BindScene(observer_ptr { world.get() });
  EXPECT_EQ(Execute("pp.inspect " + target).status, Status::kInvalidArguments);
}

NOLINT_TEST_F(PostProcessConsoleTest, ValidatesWholeCameraAndOutputRequests)
{
  const auto camera = settings.GetCameraExposure();
  ASSERT_TRUE(camera);
  const auto epoch = settings.GetEpoch();
  EXPECT_EQ(Execute("pp.camera " + target + " 2 60 0").status,
    Status::kInvalidArguments);
  EXPECT_FLOAT_EQ(settings.GetManualCameraAperture(), camera->aperture_f);
  EXPECT_FLOAT_EQ(settings.GetManualCameraShutterRate(), camera->shutter_rate);
  EXPECT_EQ(settings.GetEpoch(), epoch);
  EXPECT_EQ(Execute("pp.camera " + target + " 8 60 200").status, Status::kOk);
  EXPECT_FLOAT_EQ(settings.GetManualCameraIso(), 200);
  const auto tone = settings.GetToneMapper();
  const auto gamma = settings.GetGamma();
  EXPECT_EQ(Execute("pp.output " + target + " filmic -1").status,
    Status::kInvalidArguments);
  EXPECT_EQ(settings.GetToneMapper(), tone);
  EXPECT_FLOAT_EQ(settings.GetGamma(), gamma);
  EXPECT_EQ(Execute("pp.output " + target + " none 1").status, Status::kOk);
  EXPECT_FALSE(settings.GetTonemappingEnabled());
  EXPECT_FLOAT_EQ(settings.GetGamma(), 1);
  EXPECT_EQ(Execute("pp.output " + target + " aces 2.4").status, Status::kOk);
  EXPECT_TRUE(settings.GetTonemappingEnabled());
  EXPECT_EQ(settings.GetToneMapper(), engine::ToneMapper::kAcesFitted);
}

NOLINT_TEST_F(PostProcessConsoleTest, MaskAndCurveStayWithTheAuthoredOwner)
{
  EXPECT_EQ(Execute("pp.exposure " + target + " mask=off").status, Status::kOk);
  EXPECT_EQ(settings.GetExposureSettings().metering_mask.get(), 0U);
  const auto mask = Execute("pp.exposure " + target + " mask=scene");
  EXPECT_EQ(mask.status, Status::kOk);
  EXPECT_NE(mask.output.find("asynchronous"), std::string::npos);
  EXPECT_EQ(settings.GetExposureSettings().metering_mask.get(), 41U);
  EXPECT_EQ(Execute("pp.exposure " + target + " mask=99").status,
    Status::kInvalidArguments);
  EXPECT_EQ(Execute("pp.curve " + target + " -2:-1 2:1").status, Status::kOk);
  const auto before = settings.GetExposureSettings();
  EXPECT_EQ(Execute("pp.curve " + target + " 0:0 0:1").status,
    Status::kInvalidArguments);
  EXPECT_EQ(settings.GetExposureSettings(), before);
  EXPECT_EQ(Execute("pp.curve " + target + " clear").status, Status::kOk);
  EXPECT_TRUE(settings.GetExposureSettings().compensation_curve.empty());
}

NOLINT_TEST_F(
  PostProcessConsoleTest, QueuesRealTokensAndRejectsBorrowersAndRetiredOwners)
{
  const auto owner = vortex::CompositionView::ViewStateHandle { 11U };
  EXPECT_EQ(
    Execute("pp.transition 22 seed 8").status, Status::kInvalidArguments);
  EXPECT_EQ(
    Execute("pp.transition 999 seed 8").status, Status::kInvalidArguments);
  EXPECT_FALSE(renderer->InspectExposureTransition(
    vortex::CompositionView::ViewStateHandle { 999U }));
  const auto queued = Execute("pp.transition 11 seed 8");
  EXPECT_EQ(queued.status, Status::kOk);
  EXPECT_TRUE(queued.output.starts_with("queued"));
  const auto first = renderer->InspectExposureTransition(owner);
  ASSERT_TRUE(first);
  EXPECT_EQ(first->phase, vortex::ExposureTransitionPhase::kQueued);
  EXPECT_EQ(first->request.seed_ev, 8.0F);
  EXPECT_EQ(Execute("pp.transition 11 seed 9").status, Status::kOk);
  EXPECT_GT(renderer->InspectExposureTransition(owner)->request.generation,
    first->request.generation);
  EXPECT_TRUE(Execute("pp.transition.status 11").output.starts_with("queued"));
  renderer->RemovePublishedRuntimeView(frame, ViewId { 1U });
  EXPECT_EQ(
    Execute("pp.transition 11 remeter").status, Status::kInvalidArguments);
  EXPECT_EQ(
    Execute("pp.transition.status 11").status, Status::kInvalidArguments);
}

NOLINT_TEST_F(
  PostProcessConsoleTest, RemotePolicyDeniesEditsBeforeOwnerMutation)
{
  const auto before = settings.GetExposureSettings();
  const auto result = console.Execute("pp.exposure " + target + " manual_ev=4",
    { .source = console::CommandSource::kRemote });
  EXPECT_EQ(result.status, Status::kDenied);
  EXPECT_EQ(settings.GetExposureSettings(), before);
}

NOLINT_TEST_F(
  PostProcessConsoleTest, RegistrationConflictRollsBackPartialBindings)
{
  bindings.reset();
  const auto existing = console.RegisterCommand({
    .name = "pp.camera",
    .help = "Existing owner",
    .flags = console::CommandFlags::kNone,
    .handler = [](const auto&, const auto&) -> console::ExecutionResult {
      return { .output = "existing" };
    },
  });
  ASSERT_TRUE(existing.IsValid());
  EXPECT_THROW(Bind(), std::runtime_error);
  EXPECT_EQ(console.Complete("pp.").size(), 1U);
  EXPECT_EQ(Execute("pp.camera").output, "existing");
  EXPECT_EQ(Execute("pp.targets").status, Status::kNotFound);
}

} // namespace oxygen::examples::testing
