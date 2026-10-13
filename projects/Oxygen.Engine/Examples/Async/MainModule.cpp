//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Async/AsyncDemoPanel.h"
#include "Async/AsyncDemoSettingsService.h"
#include "Async/AsyncDemoVm.h"
#include "Async/AsyncShowcase.h"
#include "Async/MainModule.h"
#include "DemoShell/DemoShell.h"
#include "DemoShell/UI/CameraRigController.h"
#include "DemoShell/UI/DroneCameraController.h"
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <glm/gtc/constants.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/HostModule.h>
#include <Oxygen/Host/HostWindow.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>

using oxygen::ViewPort;
using oxygen::scene::PerspectiveCamera;

namespace {

constexpr uint32_t kDefaultOffscreenWidth = 1280U;
constexpr uint32_t kDefaultOffscreenHeight = 720U;
constexpr glm::vec3 kSceneFocusPoint { 0.0F, 0.0F, 0.5F };

} // namespace

namespace oxygen::examples::async {

MainModule::MainModule(const host::HostContext& host)
  : Base(host)
{
}

auto MainModule::OnAttachedImpl(
  oxygen::observer_ptr<oxygen::IAsyncEngine> engine) noexcept
  -> std::unique_ptr<DemoShell>
{
  CHECK_NOTNULL_F(engine);

  auto shell = std::make_unique<DemoShell>();

  settings_service_ = std::make_shared<AsyncDemoSettingsService>();
  vm_ = std::make_shared<AsyncDemoVm>(observer_ptr { settings_service_.get() },
    observer_ptr { &camera_spot_light_ }, &scene_content_.Spheres());
  vm_->SetEnsureSpotlightCallback([this] -> void { EnsureCameraSpotLight(); });

  auto shell_config = MakeShellConfig();
  shell_config.force_environment_override = false;
  shell_config.panel_config = DemoShellPanelConfig {
    .content_loader = false,
    .camera_controls = true,
    .environment = true,
    .diagnostics = true,
    .post_process = true,
    .ground_grid = true,
  };
  shell_config.enable_camera_rig = true;

  if (!shell->Initialize(shell_config)) {
    LOG_F(WARNING, "Async: DemoShell initialization failed");
    return nullptr;
  }
  ConfigureDrone(*shell);

  async_panel_ = std::make_shared<AsyncDemoPanel>(
    observer_ptr { vm_.get() }, shell->GetPanelCommands());

  if (!shell->RegisterPanel(async_panel_)) {
    LOG_F(WARNING, "Async: failed to register Async panel");
  } else if (!shell->GetActivePanelName().has_value()) {
    shell->SetActivePanel(async_panel_->GetName());
  }

  main_view_id_ = Publisher().GetOrCreateViewId("MainView");

  return shell;
}

MainModule::~MainModule() = default;

auto MainModule::BuildWindowProperties() const
  -> oxygen::platform::window::Properties
{
  constexpr std::uint32_t kWindowWidth = 2600;
  constexpr std::uint32_t kWindowHeight = 1400;

  auto props = Base::BuildWindowProperties();
  props.title = "Oxygen Graphics Demo - AsyncEngine";
  props.extent = { .width = kWindowWidth, .height = kWindowHeight };
  return props;
}

void MainModule::OnShutdown() noexcept
{
#ifdef OXYGEN_BUILD_UI_TESTS
  StopUiTests();
#endif
  showcase_.reset();
  Stage().Clear();
  active_scene_ = {};
  Base::OnShutdown();

  async_panel_.reset();
  vm_.reset();
  settings_service_.reset();
}

auto MainModule::OnScenePublished(engine::FrameContext& context) -> void
{
  Base::OnScenePublished(context);
  active_scene_ = Stage().GetActiveScene();
  auto published_camera = Stage().TakePublishedMainCamera();
  if (published_camera.IsAlive()) {
    main_camera_ = std::move(published_camera);
  }
}

auto MainModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  if (!HasRenderableWindow()) {
    co_return;
  }

  EnsureExampleScene();
  if (!active_scene_.IsValid()) {
    co_await Base::OnSceneMutation(context);
    co_return;
  }

  const auto extent = ResolveViewExtent();
  const auto width = static_cast<int>(extent.x);
  const auto height = static_cast<int>(extent.y);

  EnsureMainCamera(width, height);
  EnsureCameraSpotLight();
  if (!showcase_ && camera_spot_light_.HasLight()) {
    showcase_ = std::make_unique<AsyncShowcase>(GetShell(),
      AsyncShowcase::SceneBindings {
        .scene = Stage().TryGetScene(),
        .camera = main_camera_,
        .sun = sun_light_,
        .spotlight = camera_spot_light_,
      });
    vm_->BindShowcase(observer_ptr { showcase_.get() });
  }
  scene_content_.UpdateMaterials(anim_time_);
  co_await Base::OnSceneMutation(context);
}

auto MainModule::EnsureCameraSpotLight() -> void
{
  const auto scene_ptr = Stage().TryGetScene();
  auto* scene = scene_ptr.get();
  if ((scene == nullptr) || !main_camera_.IsAlive()) {
    return;
  }

  if (!camera_spot_light_.IsAlive()) {
    auto child_opt = scene->CreateChildNode(main_camera_, "CameraSpotLight");
    if (!child_opt.has_value()) {
      return;
    }
    camera_spot_light_ = std::move(child_opt.value());
    // Offset the lamp from the eye so its cast shadows are visible.
    camera_spot_light_.GetTransform().SetLocalPosition(
      glm::vec3(2.0F, 0.5F, 0.0F));

    // Engine conventions:
    // - World/light forward = space::move::Forward (-Y).
    // - Camera look forward = space::look::Forward (-Z).
    // The camera spot light is a child of the camera, so we rotate the light
    // by +90deg about +X to map move::Forward to look::Forward while still
    // inheriting the camera's rotation.
    constexpr auto kPitch = glm::half_pi<float>();
    camera_spot_light_.GetTransform().SetLocalRotation(
      glm::angleAxis(kPitch, space::move::Right));
  }

  if (camera_spot_light_.IsAlive() && !camera_spot_light_.HasLight()) {
    auto light = std::make_unique<scene::SpotLight>();
    CHECK_NOTNULL_F(settings_service_);
    const auto intensity = settings_service_->GetSpotlightIntensity();
    const auto range = settings_service_->GetSpotlightRange();
    const auto color = settings_service_->GetSpotlightColor();
    const auto inner_cone = settings_service_->GetSpotlightInnerCone();
    const auto outer_cone = settings_service_->GetSpotlightOuterCone();
    const auto enabled = settings_service_->GetSpotlightEnabled();
    const auto casts_shadows = settings_service_->GetSpotlightCastsShadows();

    light->Common().affects_world = enabled;
    light->Common().color_rgb = color;
    light->SetLuminousFluxLm(intensity);
    light->Common().mobility = scene::LightMobility::kRealtime;
    light->Common().casts_shadows = casts_shadows;
    light->SetRange(range);
    const float clamped_inner = std::min(inner_cone, outer_cone);
    const float clamped_outer = std::max(inner_cone, outer_cone);
    light->SetConeAnglesRadians(clamped_inner, clamped_outer);
    light->SetSourceRadius(0.0F);

    const bool attached = camera_spot_light_.ReplaceLight(std::move(light));
    CHECK_F(attached, "Failed to attach SpotLight to CameraSpotLight");
  }
}

auto MainModule::OnGameplay(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  const auto seconds
    = std::chrono::duration<double>(context->GetGameDeltaTime().get()).count();
  if (showcase_) {
    showcase_->Update(seconds);
  }
  const bool paused = showcase_ && showcase_->IsPaused();
  if (!paused
    && (vm_->IsAnimationEnabled() || (showcase_ && showcase_->IsPlaying()))) {
    constexpr double kMaximumAnimationStep = 0.05;
    anim_time_ += std::min(seconds, kMaximumAnimationStep);
    scene_content_.Animate(anim_time_);
  }
  vm_->SetAnimationTime(anim_time_);
  co_return;
}

auto MainModule::BuildViews(
  engine::FrameContext& /*context*/, host::HostViews& views) -> void
{
  if (!active_scene_.IsValid() || !main_camera_.IsAlive()
    || !main_camera_.HasCamera()) {
    return;
  }
  const auto extent = ResolveViewExtent();
  if (extent.x == 0U || extent.y == 0U) {
    return;
  }
  View view {};
  view.viewport = ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(extent.x),
    .height = static_cast<float>(extent.y),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  auto composition
    = vortex::CompositionView::ForScene(main_view_id_, view, main_camera_);
  composition.view_state_handle
    = vortex::CompositionView::ViewStateHandle { main_view_id_.get() };
  composition.shading_mode = vortex::ShadingMode::kDeferred;
  composition.with_atmosphere = true;
  composition.with_height_fog = GetShell().IsHeightFogPassRequested();
  composition.with_local_fog = GetShell().IsLocalFogPassRequested();
  views.views.push_back(std::move(composition));
  views.main = main_view_id_;
}

auto MainModule::OnGuiUpdate(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  // The tour presents itself in place of the panels.
  if (!showcase_ || !showcase_->IsActive()) {
    co_await Base::OnGuiUpdate(context);
    co_return;
  }
  if (co_await DrawHostUi(context)) {
    async_panel_->DrawPresentation(
      context->GetFrameSequenceNumber(), UiTopInset());
  }
}

auto MainModule::EnsureExampleScene() -> void
{
  if (active_scene_.IsValid() || Stage().HasStagedScene()) {
    return;
  }
  constexpr std::size_t kSceneCapacity = 128U;
  Stage().Stage(
    std::make_shared<scene::Scene>("Async Showcase", kSceneCapacity));
  scene_content_.Populate(*Stage().GetStagedScene());
  main_camera_ = scene_content_.Camera();
  sun_light_ = scene_content_.Sun();
  Stage().SetStagedMainCamera(main_camera_);
}

auto MainModule::EnsureMainCamera(const int width, const int height) -> void
{
  // NOLINTBEGIN(*-magic-numbers)
  LOG_SCOPE_FUNCTION(2);
  using scene::PerspectiveCamera;

  if (!Stage().TryGetScene()) {
    return;
  }

  // Configure camera params
  const auto cam_ref = main_camera_.GetCameraAs<PerspectiveCamera>();
  if (cam_ref) {
    const float aspect = height > 0
      ? (static_cast<float>(width) / static_cast<float>(height))
      : 1.0F;
    auto& cam = cam_ref->get();
    cam.SetAspectRatio(aspect);
    cam.SetViewport(ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(width),
      .height = static_cast<float>(height),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    });
  }
  // NOLINTEND(*-magic-numbers)
}

auto MainModule::ConfigureDrone(DemoShell& shell) -> void
{
  // NOLINTBEGIN(*-magic-numbers)
  if (!shell.GetCameraRig()) {
    return;
  }

  auto drone_controller = shell.GetCameraRig()->GetDroneController();
  if (!drone_controller) {
    return;
  }

  // Drone path uses world space (Z-up). Altitude must be Z, not Y.
  drone_controller->SetPathGenerator([] -> std::vector<glm::vec3> {
    constexpr int points = 96;
    constexpr float a = 36.0F;
    constexpr float altitude = 14.0F;
    std::vector<glm::vec3> path;
    path.reserve(points);
    for (int i = 0; i < points; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(points);
      const float ang = t * glm::two_pi<float>();
      const float x = a * std::cos(ang);
      const float y = a * std::sin(ang) * std::cos(ang);
      path.emplace_back(x, y, altitude);
    }
    return path;
  });

  drone_controller->SetFocusHeight(kSceneFocusPoint.z);
  // NOLINTEND(*-magic-numbers)
}

auto MainModule::ResolveViewExtent() const noexcept -> glm::uvec2
{
  if (Window() && Window()->GetWindow()) {
    const auto extent = Window()->GetWindow()->Size();
    return { extent.width, extent.height };
  }

  return { kDefaultOffscreenWidth, kDefaultOffscreenHeight };
}

} // namespace oxygen::examples::async
