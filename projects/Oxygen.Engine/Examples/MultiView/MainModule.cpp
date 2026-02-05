//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/CompositingTask.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

#include "DemoShell/Runtime/CompositionView.h"
#include "DemoShell/Runtime/ForwardPipeline.h"
#include "DemoShell/UI/CameraRigController.h"
#include "MultiView/MainModule.h"

namespace oxygen::examples::multiview {

MainModule::MainModule(
  const DemoAppContext& app, CompositingMode compositing_mode) noexcept
  : Base(app)
  , app_(app)
{
  compositing_mode_ = compositing_mode;
  main_view_id_ = this->GetOrCreateViewId("MainView");
  pip_view_id_ = this->GetOrCreateViewId("PipView");
}

auto MainModule::OnAttached(
  oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept -> bool
{
  CHECK_F(static_cast<bool>(engine), "MultiView requires a valid engine");
  CHECK_F(Base::OnAttached(engine), "MultiView base attach failed");

  // Initialize the pipeline if it hasn't been already.
  pipeline_ = std::make_unique<ForwardPipeline>(engine);
  CHECK_NOTNULL_F(pipeline_, "Failed to create ForwardPipeline");
  // Boost exposure to compensate for lower light intensity
  pipeline_->SetExposureValue(2.5F);

  // Initialize DemoShell with all panels disabled (non-interactive demo)
  shell_ = std::make_unique<DemoShell>();
  DemoShellConfig shell_config;
  shell_config.engine = engine;
  shell_config.panel_config = DemoShellPanelConfig {
    .content_loader = false,
    .camera_controls = false,
    .environment = false,
    .lighting = false,
    .rendering = false,
    .post_process = true,
  };
  shell_config.enable_camera_rig = false; // Non-interactive demo
  shell_config.get_active_pipeline
    = [this]() { return observer_ptr<RenderingPipeline> { pipeline_.get() }; };

  CHECK_F(shell_->Initialize(shell_config),
    "MultiView: DemoShell initialization failed");
  LOG_F(INFO, "[MultiView] DemoShell initialized (all panels disabled)");

  return true;
}

auto MainModule::OnShutdown() noexcept -> void
{
  if (shell_) {
    shell_->SetScene(std::unique_ptr<scene::Scene> {});
    shell_.reset();
  }
  active_scene_ = {};

  scene_bootstrapper_.BindToScene(observer_ptr<scene::Scene> { nullptr });
}

auto MainModule::HandleOnFrameStart(engine::FrameContext& context) -> void
{
  CHECK_NOTNULL_F(app_window_, "AppWindow must exist in MultiView");
  CHECK_NOTNULL_F(shell_, "DemoShell must exist in MultiView");

  // CRITICAL: Ensure scene is created and set on context
  if (!active_scene_.IsValid()) {
    auto scene = std::make_unique<scene::Scene>("MultiViewScene");
    active_scene_ = shell_->SetScene(std::move(scene));
    scene_bootstrapper_.BindToScene(shell_->TryGetScene());

    // Ensure cameras exist
    if (auto s = shell_->TryGetScene()) {
      main_camera_node_ = s->CreateNode("MainCamera");
      main_camera_node_.AttachCamera(
        std::make_unique<scene::PerspectiveCamera>());
      shell_->SetActiveCamera(main_camera_node_);

      pip_camera_node_ = s->CreateNode("PipCamera");
      pip_camera_node_.AttachCamera(
        std::make_unique<scene::PerspectiveCamera>());

      UpdateCameras(app_window_->GetWindow()->Size());
    }
  }

  const auto scene_ptr = shell_->TryGetScene();
  CHECK_F(static_cast<bool>(scene_ptr), "Scene must be available");
  context.SetScene(oxygen::observer_ptr { scene_ptr.get() });

  // Ensure content exists
  (void)scene_bootstrapper_.EnsureSceneWithContent();

  // Ensure drone is configured once the rig is available
  const auto rig = shell_->GetCameraRig();
  if (rig != last_camera_rig_) {
    last_camera_rig_ = rig;
  }
}

auto MainModule::UpdateCameras(const platform::window::ExtentT& extent) -> void
{
  auto& camera_lifecycle = shell_->GetCameraLifecycle();
  camera_lifecycle.EnsureViewport(extent);
  camera_lifecycle.ApplyPendingSync();
  camera_lifecycle.ApplyPendingReset();

  // Update Main camera (match legacy)
  if (main_camera_node_.IsAlive()) {
    const auto cam_opt
      = main_camera_node_.GetCameraAs<scene::PerspectiveCamera>();
    if (cam_opt) {
      auto& cam = cam_opt->get();
      constexpr auto kMainCamPos = glm::vec3(0.0F, 0.0F, 5.0F);
      constexpr float kMainCamFov = 45.0F;
      constexpr float kMainCamNear = 0.1F;
      constexpr float kMainCamFar = 100.0F;

      main_camera_node_.GetTransform().SetLocalPosition(kMainCamPos);
      cam.SetFieldOfView(glm::radians(kMainCamFov));
      cam.SetAspectRatio(extent.height > 0
          ? (static_cast<float>(extent.width)
              / static_cast<float>(extent.height))
          : 1.0F);
      cam.SetNearPlane(kMainCamNear);
      cam.SetFarPlane(kMainCamFar);
    }
  }

  // Update PiP camera
  if (pip_camera_node_.IsAlive()) {
    const auto cam_opt
      = pip_camera_node_.GetCameraAs<scene::PerspectiveCamera>();
    if (cam_opt) {
      auto& cam = cam_opt->get();

      // Position the PiP camera (match legacy)
      constexpr glm::vec3 pip_position = glm::vec3(-5.0F, 0.4F, 4.0F);
      pip_camera_node_.GetTransform().SetLocalPosition(pip_position);

      constexpr glm::vec3 target = glm::vec3(0.0F, 0.0F, -2.0F);
      constexpr glm::vec3 world_up = glm::vec3(0.0F, 1.0F, 0.0F);
      const glm::mat4 view_mat = glm::lookAt(pip_position, target, world_up);
      const glm::quat pip_rot = glm::quat_cast(glm::inverse(view_mat));
      pip_camera_node_.GetTransform().SetLocalRotation(pip_rot);

      // PiP aspect ratio from its intended viewport
      constexpr float kPipWidthRatio = 0.45F;
      constexpr float kPipHeightRatio = 0.45F;
      const float sw = static_cast<float>(extent.width);
      const float sh = static_cast<float>(extent.height);
      const float pip_w = sw * kPipWidthRatio;
      const float pip_h = sh * kPipHeightRatio;

      constexpr float kPipCamFov = 35.0F;
      constexpr float kPipCamNear = 0.05F;
      constexpr float kPipCamFar = 100.0F;

      cam.SetFieldOfView(glm::radians(kPipCamFov));
      cam.SetAspectRatio(pip_h > 0 ? (pip_w / pip_h) : 1.0F);
      cam.SetNearPlane(kPipCamNear);
      cam.SetFarPlane(kPipCamFar);
    }
  }
}

auto MainModule::OnFrameStart(oxygen::engine::FrameContext& context) -> void
{
  Base::OnFrameStart(context);
}

auto MainModule::OnFrameEnd(engine::FrameContext& context) -> void
{
  Base::OnFrameEnd(context);
}

auto MainModule::OnGuiUpdate(engine::FrameContext& context) -> oxygen::co::Co<>
{
  if (!app_window_ || !app_window_->GetWindow()) {
    co_return;
  }

  CHECK_NOTNULL_F(shell_, "DemoShell required for GUI update");
  shell_->Draw(context);

  co_return;
}

auto MainModule::OnGameplay(engine::FrameContext& context) -> oxygen::co::Co<>
{
  shell_->Update(context.GetGameDeltaTime());
  co_return;
}

auto MainModule::OnSceneMutation(engine::FrameContext& context)
  -> oxygen::co::Co<>
{
  CHECK_NOTNULL_F(app_window_, "AppWindow required for scene mutation");
  if (!app_window_->GetWindow()) {
    co_return;
  }

  const auto extent = app_window_->GetWindow()->Size();
  if (extent.width != last_viewport_.width
    || extent.height != last_viewport_.height) {
    UpdateCameras(extent);
    last_viewport_ = extent;
  }

  shell_->Update(oxygen::time::CanonicalDuration {});

  co_await Base::OnSceneMutation(context);
  co_return;
}

auto MainModule::OnPreRender(engine::FrameContext& context) -> oxygen::co::Co<>
{
  auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>();
  if (imgui_module_ref) {
    auto& imgui_module = imgui_module_ref->get();
    if (auto* imgui_context = imgui_module.GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }
  }

  co_await Base::OnPreRender(context);
  co_return;
}

auto MainModule::UpdateComposition(engine::FrameContext& /*context*/,
  std::vector<CompositionView>& views) -> void
{
  if (!shell_) {
    return;
  }
  auto& active_camera = shell_->GetCameraLifecycle().GetActiveCamera();
  if (!active_camera.IsAlive()) {
    return;
  }

  const auto extent = app_window_->GetWindow()->Size();
  const float sw = static_cast<float>(extent.width);
  const float sh = static_cast<float>(extent.height);

  // 1. Main Scene (Full screen)
  View main_view {};
  main_view.viewport = ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = sw,
    .height = sh,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  auto main_comp
    = CompositionView::ForScene(main_view_id_, main_view, active_camera);
  const graphics::Color kMainClearColor { 0.1F, 0.2F, 0.38F, 1.0F };
  main_comp.clear_color = kMainClearColor;
  views.push_back(std::move(main_comp));

  // 2. PiP View
  if (pip_camera_node_.IsAlive()) {
    constexpr float kPipWidthRatio = 0.45F;
    constexpr float kPipHeightRatio = 0.45F;
    constexpr float kPipMargin = 24.0F;

    const float pip_w = std::floor(sw * kPipWidthRatio);
    const float pip_h = std::floor(sh * kPipHeightRatio);

    const float offset_x = std::max(0.0F, sw - pip_w - kPipMargin);
    const float offset_y
      = std::clamp(kPipMargin, 0.0F, std::max(0.0F, sh - pip_h));

    View pip_view {};
    pip_view.viewport = ViewPort {
      .top_left_x = offset_x,
      .top_left_y = offset_y,
      .width = pip_w,
      .height = pip_h,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    pip_view.scissor = Scissors {
      .left = static_cast<int32_t>(offset_x),
      .top = static_cast<int32_t>(offset_y),
      .right = static_cast<int32_t>(offset_x + pip_w),
      .bottom = static_cast<int32_t>(offset_y + pip_h),
    };

    auto pip_comp = CompositionView::ForPip(pip_view_id_,
      CompositionView::ZOrder { CompositionView::kZOrderScene.get() + 1 },
      pip_view, pip_camera_node_);

    // Match legacy PiP clear color (dark gray, half transparent)
    const graphics::Color kPipClearColor { 0.1F, 0.1F, 0.1F, 0.5F };
    pip_comp.clear_color = kPipClearColor;
    pip_comp.opacity = 1.0F;
    pip_comp.force_wireframe = true;

    views.push_back(std::move(pip_comp));
  }

  // 3. ImGui
  const auto imgui_view_id = this->GetOrCreateViewId("ImGuiView");
  views.push_back(CompositionView::ForImGui(
    imgui_view_id, main_view, [](graphics::CommandRecorder&) { }));
}

auto MainModule::OnCompositing(engine::FrameContext& context)
  -> oxygen::co::Co<>
{
  co_await Base::OnCompositing(context);
}

auto MainModule::ClearBackbufferReferences() -> void
{
  if (pipeline_) {
    pipeline_->ClearBackbufferReferences();
  }
}

auto MainModule::BuildDefaultWindowProperties() const
  -> oxygen::platform::window::Properties
{
  auto props = Base::BuildDefaultWindowProperties();
  props.title = "Oxygen Engine - MultiView Example";
  return props;
}

} // namespace oxygen::examples::multiview
