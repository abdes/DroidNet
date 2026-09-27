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
#include "Async/MainModule.h"
#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoAppContext.h"
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

MainModule::MainModule(const DemoAppContext& app)
  : Base(app)
{
  DCHECK_NOTNULL_F(app_.platform);
  DCHECK_F(!app_.gfx_weak.expired());
}

auto MainModule::OnAttachedImpl(
  oxygen::observer_ptr<oxygen::IAsyncEngine> engine) noexcept
  -> std::unique_ptr<DemoShell>
{
  CHECK_NOTNULL_F(engine);

  auto shell = std::make_unique<DemoShell>();

  settings_service_ = std::make_shared<AsyncDemoSettingsService>();
  vm_ = std::make_shared<AsyncDemoVm>(observer_ptr { settings_service_.get() },
    observer_ptr { &camera_spot_light_ }, &completed_frame_tracker_,
    &scene_content_.Spheres());
  vm_->SetEnsureSpotlightCallback([this] -> void { EnsureCameraSpotLight(); });

  async_panel_ = std::make_shared<AsyncDemoPanel>(observer_ptr { vm_.get() });

  DemoShellConfig shell_config;
  shell_config.engine = engine;
  shell_config.enable_renderer_bound_panels = true;
  shell_config.force_environment_override = false;
  shell_config.panel_config = DemoShellPanelConfig {
    .content_loader = false,
    .camera_controls = true,
    .environment = true,
    .lighting = true,
    .diagnostics = true,
    .post_process = true,
    .ground_grid = true,
  };
  shell_config.enable_camera_rig = true;

  if (!shell->Initialize(shell_config)) {
    LOG_F(WARNING, "Async: DemoShell initialization failed");
    return nullptr;
  }

  if (!shell->RegisterPanel(async_panel_)) {
    LOG_F(WARNING, "Async: failed to register Async panel");
  } else if (!shell->GetActivePanelName().has_value()) {
    shell->SetActivePanel(async_panel_->GetName());
  }

  // Create Main View ID
  main_view_id_ = GetOrCreateViewId("MainView");

  return shell;
}

MainModule::~MainModule() = default;

auto MainModule::BuildDefaultWindowProperties() const
  -> oxygen::platform::window::Properties
{

  constexpr std::uint32_t kWindowWidth = 2600;
  constexpr std::uint32_t kWindowHeight = 1400;

  oxygen::platform::window::Properties props(
    "Oxygen Graphics Demo - AsyncEngine");
  props.extent = { .width = kWindowWidth, .height = kWindowHeight };
  props.flags = {
    .hidden = false,
    .always_on_top = false,
    .full_screen = app_.fullscreen,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false,
  };
  return props;
}

void MainModule::OnShutdown() noexcept
{
#ifdef OXYGEN_BUILD_UI_TESTS
  StopUiTests();
#endif
  auto& shell = GetShell();
  shell.SetScene(std::unique_ptr<scene::Scene> {});
  active_scene_ = {};

  async_panel_.reset();
  vm_.reset();
  settings_service_.reset();
  Base::OnShutdown();
}

auto MainModule::ClearBackbufferReferences() -> void
{
  // DemoModuleBase owns and retires scene targets during resize.
}

auto MainModule::OnFrameStart(observer_ptr<engine::FrameContext> context)
  -> void
{
  DCHECK_NOTNULL_F(context);
  auto& shell = GetShell();

  if (shell.HasStagedScene()) {
    CHECK_F(shell.PublishStagedScene(),
      "expected staged scene before frame-start publish");
    active_scene_ = shell.GetActiveScene();
    auto published_camera = shell.TakePublishedMainCamera();
    if (published_camera.IsAlive()) {
      main_camera_ = std::move(published_camera);
    }
  }

  shell.OnFrameStart(*context);
  StartFrameTracking();
  TrackFrameAction("Frame started");

  if (app_window_ != nullptr && !app_window_->GetWindow()) {
    TrackFrameAction("GUI update skipped - app window not available");
    // continue to Base to ensure cleanup
  }

  LOG_SCOPE_F(3, "MainModule::OnFrameStart");

  // Call base to handle window lifecycle and surface setup
  Base::OnFrameStart(context);

  if (!HasRenderableWindow()) {
    active_views_.clear();
    return;
  }

  LOG_SCOPE_F(3, "MainModule::OnExampleFrameStart");

  // Register scene with frame context (required for rendering)
  const auto scene_ptr = shell.TryGetScene();
  if (scene_ptr) {
    context->SetScene(oxygen::observer_ptr { scene_ptr.get() });
  }

  if (app_window_ != nullptr && !app_window_->GetWindow()) {
    active_views_.clear();
  }

  // Ensure drone is configured once the rig is available
  const auto rig = shell.GetCameraRig();
  if (rig != last_camera_rig_) {
    last_camera_rig_ = rig;
    drone_configured_ = false;
  }
  if (!drone_configured_ && rig) {
    ConfigureDrone();
    drone_configured_ = true;
  }
}

auto MainModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  if (app_window_ != nullptr && !app_window_->GetWindow()) {
    // Window invalid, skip update
    DLOG_F(1, "OnSceneMutation: no valid window - skipping");
    active_views_.clear();
    TrackFrameAction("Scene mutation skipped - app window not available");
    TrackPhaseEnd();
    co_return;
  }

  LOG_SCOPE_F(3, "MainModule::OnSceneMutation");
  TrackPhaseStart("Scene Mutation");
  TrackFrameAction("Scene mutation phase started");

  EnsureExampleScene();
  if (!active_scene_.IsValid()) {
    co_await Base::OnSceneMutation(context);
    TrackPhaseEnd();
    co_return;
  }

  const auto extent = ResolveViewExtent();
  const auto width = static_cast<int>(extent.x);
  const auto height = static_cast<int>(extent.y);

  EnsureMainCamera(width, height);
  EnsureCameraSpotLight();
  scene_content_.UpdateMaterials(anim_time_);

  TrackFrameAction("Scene mutations updated");
  co_await Base::OnSceneMutation(context);

  TrackPhaseEnd();
  co_return;
}

auto MainModule::EnsureCameraSpotLight() -> void
{
  auto& shell = GetShell();
  const auto scene_ptr = shell.TryGetScene();
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
  TrackPhaseStart("Gameplay");
  auto& shell = GetShell();

  if (vm_->IsAnimationEnabled()) {
    const auto seconds
      = std::chrono::duration<double>(context->GetGameDeltaTime().get())
          .count();
    constexpr double kMaximumAnimationStep = 0.05;
    anim_time_ += std::min(seconds, kMaximumAnimationStep);
    scene_content_.Animate(anim_time_);
  }
  vm_->SetAnimationTime(anim_time_);

  shell.Update(context->GetGameDeltaTime());

  TrackPhaseEnd();
  co_return;
}

auto MainModule::OnPublishViews(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  TrackPhaseStart("Publish Views");
  co_await Base::OnPublishViews(context);
  TrackFrameAction("Published Vortex scene view");
  TrackPhaseEnd();
}

auto MainModule::UpdateComposition(engine::FrameContext& context,
  std::vector<vortex::CompositionView>& views) -> void
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
  GetShell().OnMainViewReady(context, composition);
  views.push_back(std::move(composition));
}

auto MainModule::OnPreRender(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  TrackPhaseStart("PreRender");

  static_cast<void>(context);

  if (app_window_ != nullptr && !app_window_->GetWindow()) {
    DLOG_F(1, "OnPreRender: no valid window - skipping");
    TrackPhaseEnd();
    co_return;
  }

  LOG_SCOPE_F(3, "MainModule::OnPreRender");

  TrackFrameAction("Pre-render setup started");
  TrackFrameAction("Vortex runtime seam prepared");

  TrackPhaseEnd();
  co_return;
}

auto MainModule::OnGuiUpdate(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  TrackPhaseStart("GUI Update");

  // Window must be available to render GUI
  if (app_window_ == nullptr || !app_window_->GetWindow()
    || app_window_->IsShuttingDown()) {
    TrackFrameAction("GUI update skipped - app window not available/closing");
    TrackPhaseEnd();
    co_return;
  }
  if (!active_scene_.IsValid() || !main_camera_.IsAlive()) {
    TrackFrameAction("GUI update skipped - scene/camera not ready");
    TrackPhaseEnd();
    co_return;
  }

  LOG_SCOPE_F(3, "MainModule::OnGuiUpdate");

  auto& shell = GetShell();
  shell.Draw(context);

  TrackFrameAction("GUI overlay built");
  TrackPhaseEnd();
  co_return;
}

auto MainModule::OnFrameEnd(observer_ptr<engine::FrameContext> context) -> void
{
  LOG_SCOPE_F(3, "MainModule::OnFrameEnd");

  TrackFrameAction("Frame ended");
  EndFrameTracking();
#ifdef OXYGEN_BUILD_UI_TESTS
  if (!active_views_.empty()) {
    Base::OnFrameEnd(context);
  }
#else
  static_cast<void>(context);
#endif
}

auto MainModule::EnsureExampleScene() -> void
{
  auto& shell = GetShell();
  if (active_scene_.IsValid() || shell.HasStagedScene()) {
    return;
  }
  constexpr std::size_t kSceneCapacity = 128U;
  auto scene = std::make_unique<scene::Scene>("Async Showcase", kSceneCapacity);
  shell.StageScene(std::move(scene));
  scene_content_.Populate(*shell.GetStagedScene());
  main_camera_ = scene_content_.Camera();
  sun_light_ = scene_content_.Sun();
  shell.SetStagedMainCamera(main_camera_);
}

auto MainModule::EnsureMainCamera(const int width, const int height) -> void
{
  // NOLINTBEGIN(*-magic-numbers)
  LOG_SCOPE_FUNCTION(2);
  using scene::PerspectiveCamera;

  auto& shell = GetShell();
  const auto scene_ptr = shell.TryGetScene();
  auto* scene = scene_ptr.get();
  if (scene == nullptr) {
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

auto MainModule::ConfigureDrone() -> void
{
  // NOLINTBEGIN(*-magic-numbers)
  auto& shell = GetShell();
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
  if (!app_.headless && app_window_ != nullptr && app_window_->GetWindow()) {
    const auto extent = app_window_->GetWindow()->Size();
    return { extent.width, extent.height };
  }

  return { kDefaultOffscreenWidth, kDefaultOffscreenHeight };
}

auto MainModule::TrackPhaseStart(const std::string& phase_name) -> void
{
  phase_start_time_ = std::chrono::steady_clock::now();
  current_phase_name_ = phase_name;
}

auto MainModule::TrackPhaseEnd() -> void
{
  if (phase_start_time_ != std::chrono::steady_clock::time_point {}) {
    const auto end_time = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
      end_time - phase_start_time_);

    // Add timing to current frame tracker
    current_frame_tracker_.phase_timings.emplace_back(
      current_phase_name_, duration);

    // Reset for next phase
    phase_start_time_ = std::chrono::steady_clock::time_point {};
    current_phase_name_.clear();
  }
}

auto MainModule::TrackFrameAction(const std::string& action) -> void
{
  current_frame_tracker_.frame_actions.push_back(action);
}

auto MainModule::StartFrameTracking() -> void
{
  current_frame_tracker_ = FrameActionTracker {};
  current_frame_tracker_.frame_start_time = std::chrono::steady_clock::now();
}

auto MainModule::EndFrameTracking() -> void
{
  current_frame_tracker_.frame_end_time = std::chrono::steady_clock::now();

  // Calculate total frame time if we don't have phase timings
  if (current_frame_tracker_.phase_timings.empty()) {
    const auto total_duration
      = std::chrono::duration_cast<std::chrono::microseconds>(
        current_frame_tracker_.frame_end_time
        - current_frame_tracker_.frame_start_time);
    current_frame_tracker_.phase_timings.emplace_back(
      "Total Frame", total_duration);
  }

  completed_frame_tracker_ = current_frame_tracker_;
}

} // namespace oxygen::examples::async
