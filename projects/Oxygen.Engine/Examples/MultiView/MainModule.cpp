//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Vortex/CompositionView.h>

#include "DemoShell/UI/CameraRigController.h"
#include "MultiView/MainModule.h"

namespace oxygen::examples::multiview {

namespace {
  constexpr size_t kDefaultSceneCapacity = 128;

  auto HasPositiveExtent(const platform::window::ExtentT& extent) -> bool
  {
    return extent.width > 0U && extent.height > 0U;
  }

  auto GetFramebufferExtent(const graphics::Framebuffer& framebuffer)
    -> platform::window::ExtentT
  {
    const auto& fb_desc = framebuffer.GetDescriptor();
    if (!fb_desc.color_attachments.empty()
      && fb_desc.color_attachments[0].texture) {
      const auto& color_desc
        = fb_desc.color_attachments[0].texture->GetDescriptor();
      return { color_desc.width, color_desc.height };
    }
    if (fb_desc.depth_attachment.texture) {
      const auto& depth_desc
        = fb_desc.depth_attachment.texture->GetDescriptor();
      return { depth_desc.width, depth_desc.height };
    }
    return {};
  }

  struct PipLayout {
    float width { 0.0F };
    float height { 0.0F };
    float offset_x { 0.0F };
    float offset_y { 0.0F };
  };

  auto ComputePipLayout(const platform::window::ExtentT& extent) -> PipLayout
  {
    constexpr float kPipWidthRatio = 0.45F;
    constexpr float kPipHeightRatio = 0.45F;
    constexpr float kPipMargin = 24.0F;

    const float sw = static_cast<float>(extent.width);
    const float sh = static_cast<float>(extent.height);
    const float pip_w = std::floor(sw * kPipWidthRatio);
    const float pip_h = std::floor(sh * kPipHeightRatio);
    const float offset_x = std::max(0.0F, sw - pip_w - kPipMargin);
    const float offset_y
      = std::clamp(kPipMargin, 0.0F, std::max(0.0F, sh - pip_h));

    return {
      .width = pip_w,
      .height = pip_h,
      .offset_x = offset_x,
      .offset_y = offset_y,
    };
  }

} // namespace

MainModule::MainModule(
  const DemoAppContext& app, MainModuleConfig config) noexcept
  : Base(app)
  , app_(app)
  , config_(std::move(config))
{

  main_view_id_ = this->GetOrCreateViewId("MainView");
  pip_view_id_ = this->GetOrCreateViewId("PipView");
}

auto MainModule::BuildDefaultWindowProperties() const
  -> platform::window::Properties
{
  constexpr uint32_t kDefaultWidth = 2560;
  constexpr uint32_t kDefaultHeight = 1400;

  platform::window::Properties props("Oxygen :: Examples :: MultiView");
  props.extent = platform::window::ExtentT { .width = kDefaultWidth,
    .height = kDefaultHeight };
  props.flags = { .hidden = false,
    .always_on_top = false,
    .full_screen = app_.fullscreen,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false };
  return props;
}

auto MainModule::OnAttachedImpl(
  oxygen::observer_ptr<oxygen::IAsyncEngine> engine) noexcept
  -> std::unique_ptr<DemoShell>
{
  CHECK_F(static_cast<bool>(engine), "MultiView requires a valid engine");

  // Initialize DemoShell with camera controls enabled.
  auto shell = std::make_unique<DemoShell>();
  DemoShellConfig shell_config;
  shell_config.engine = engine;
  shell_config.panel_config = DemoShellPanelConfig {
    .content_loader = false,
    .camera_controls = true,
    .environment = false,
    .lighting = false,
    .rendering = false,
    .post_process = true,
  };
  shell_config.enable_camera_rig = true;
  shell_config.enable_renderer_bound_panels = false;

  CHECK_F(shell->Initialize(shell_config),
    "MultiView: DemoShell initialization failed");
  LOG_F(INFO, "[MultiView] DemoShell initialized (camera controls enabled)");

  shell->StageScene(
    std::make_unique<scene::Scene>("MultiViewScene", kDefaultSceneCapacity));
  const auto staged_scene = shell->GetStagedScene();
  CHECK_NOTNULL_F(staged_scene, "MultiView staged scene is null");

  main_camera_node_ = staged_scene->CreateNode("MainCamera");
  {
    const bool attached = main_camera_node_.AttachCamera(
      std::make_unique<scene::PerspectiveCamera>());
    CHECK_F(attached, "Failed to attach MainCamera");
  }

  pip_camera_node_ = staged_scene->CreateNode("PipCamera");
  {
    const bool attached = pip_camera_node_.AttachCamera(
      std::make_unique<scene::PerspectiveCamera>());
    CHECK_F(attached, "Failed to attach PipCamera");
  }

  shell->SetStagedMainCamera(main_camera_node_);
  scene_bootstrapper_.BindToScene(staged_scene);
  (void)scene_bootstrapper_.EnsureSceneWithContent();
  const auto extent = ResolveRenderExtent();
  if (HasPositiveExtent(extent)) {
    UpdateCameras(extent);
    last_viewport_ = extent;
  }

  return shell;
}

auto MainModule::OnShutdown() noexcept -> void
{
  auto& shell = GetShell();
  shell.SetScene(std::unique_ptr<scene::Scene> {});
  active_scene_ = {};

  scene_bootstrapper_.BindToScene(observer_ptr<scene::Scene> { nullptr });
}

auto MainModule::ResolveRenderExtent() const -> platform::window::ExtentT
{
  if (app_window_) {
    if (auto framebuffer = app_window_->GetCurrentFrameBuffer().lock()) {
      const auto framebuffer_extent = GetFramebufferExtent(*framebuffer);
      if (HasPositiveExtent(framebuffer_extent)) {
        return framebuffer_extent;
      }
    }
  }

  if (app_window_ && app_window_->GetWindow()) {
    const auto window_extent = app_window_->GetWindow()->Size();
    if (HasPositiveExtent(window_extent)) {
      return window_extent;
    }
  }

  return last_viewport_;
}

auto MainModule::UpdateCameras(const platform::window::ExtentT& extent) -> void
{
  if (!HasPositiveExtent(extent)) {
    DLOG_F(2, "[MultiView] Skip camera update for invalid extent {}x{}",
      extent.width, extent.height);
    return;
  }

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
      cam.SetViewport(ViewPort {
        .top_left_x = 0.0F,
        .top_left_y = 0.0F,
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .min_depth = 0.0F,
        .max_depth = 1.0F,
      });
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
      constexpr glm::vec3 world_up = space::move::Up;
      const glm::mat4 view_mat = glm::lookAt(pip_position, target, world_up);
      const glm::quat pip_rot = glm::quat_cast(glm::inverse(view_mat));
      pip_camera_node_.GetTransform().SetLocalRotation(pip_rot);

      // PiP aspect ratio from its intended viewport
      const auto pip_layout = ComputePipLayout(extent);
      constexpr float kPipCamFov = 35.0F;
      constexpr float kPipCamNear = 0.05F;
      constexpr float kPipCamFar = 100.0F;

      cam.SetFieldOfView(glm::radians(kPipCamFov));
      cam.SetAspectRatio(
        pip_layout.height > 0 ? (pip_layout.width / pip_layout.height) : 1.0F);
      cam.SetNearPlane(kPipCamNear);
      cam.SetFarPlane(kPipCamFar);
      cam.SetViewport(ViewPort {
        // The camera renders into a dedicated PiP-sized offscreen target.
        // Its local viewport must start at (0,0); the absolute on-screen
        // inset belongs to CompositionView, not the camera.
        .top_left_x = 0.0F,
        .top_left_y = 0.0F,
        .width = pip_layout.width,
        .height = pip_layout.height,
        .min_depth = 0.0F,
        .max_depth = 1.0F,
      });
    }
  }
}

auto MainModule::OnFrameStart(
  observer_ptr<oxygen::engine::FrameContext> context) -> void
{
  DCHECK_NOTNULL_F(context);
  auto& shell = GetShell();
  CHECK_NOTNULL_F(&shell, "DemoShell must exist in MultiView");

  if (shell.HasStagedScene()) {
    CHECK_F(shell.PublishStagedScene(),
      "expected staged scene before frame-start publish");
    active_scene_ = shell.GetActiveScene();
    auto published_camera = shell.TakePublishedMainCamera();
    if (published_camera.IsAlive()) {
      main_camera_node_ = std::move(published_camera);
    }
    scene_bootstrapper_.BindToScene(shell.TryGetScene());
  }

  shell.OnFrameStart(*context);
  Base::OnFrameStart(context);

  CHECK_NOTNULL_F(app_window_, "AppWindow must exist in MultiView");
  if (!HasRenderableWindow()) {
    return;
  }

  const auto scene_ptr = shell.TryGetScene();
  if (!scene_ptr) {
    return;
  }
  context->SetScene(oxygen::observer_ptr { scene_ptr.get() });

  // Ensure content exists
  (void)scene_bootstrapper_.EnsureSceneWithContent();

  // Ensure drone is configured once the rig is available
  const auto rig = shell.GetCameraRig();
  if (rig != last_camera_rig_) {
    last_camera_rig_ = rig;
  }
}

auto MainModule::OnFrameEnd(observer_ptr<engine::FrameContext> context) -> void
{
  Base::OnFrameEnd(context);
}

auto MainModule::OnGuiUpdate(observer_ptr<engine::FrameContext> context)
  -> oxygen::co::Co<>
{
  if (!app_window_ || !app_window_->GetWindow()) {
    co_return;
  }
  if (!active_scene_.IsValid() || !main_camera_node_.IsAlive()) {
    co_return;
  }

  auto& shell = GetShell();
  CHECK_NOTNULL_F(&shell, "DemoShell required for GUI update");
  shell.Draw(context);

  co_return;
}

auto MainModule::OnGameplay(observer_ptr<engine::FrameContext> context)
  -> oxygen::co::Co<>
{
  auto& shell = GetShell();
  shell.Update(context->GetGameDeltaTime());
  co_return;
}

auto MainModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> oxygen::co::Co<>
{
  auto& shell = GetShell();
  CHECK_NOTNULL_F(app_window_, "AppWindow required for scene mutation");
  if (!app_window_->GetWindow()) {
    co_return;
  }

  if (!active_scene_.IsValid() && !shell.HasStagedScene()) {
    shell.StageScene(
      std::make_unique<scene::Scene>("MultiViewScene", kDefaultSceneCapacity));
    const auto staged_scene = shell.GetStagedScene();
    CHECK_NOTNULL_F(staged_scene, "MultiView staged scene is null");

    main_camera_node_ = staged_scene->CreateNode("MainCamera");
    {
      const bool attached = main_camera_node_.AttachCamera(
        std::make_unique<scene::PerspectiveCamera>());
      CHECK_F(attached, "Failed to attach MainCamera");
    }

    pip_camera_node_ = staged_scene->CreateNode("PipCamera");
    {
      const bool attached = pip_camera_node_.AttachCamera(
        std::make_unique<scene::PerspectiveCamera>());
      CHECK_F(attached, "Failed to attach PipCamera");
    }

    shell.SetStagedMainCamera(main_camera_node_);
    scene_bootstrapper_.BindToScene(staged_scene);
    (void)scene_bootstrapper_.EnsureSceneWithContent();
    const auto extent = ResolveRenderExtent();
    if (HasPositiveExtent(extent)) {
      UpdateCameras(extent);
      last_viewport_ = extent;
    }
  }

  if (!active_scene_.IsValid()) {
    co_await Base::OnSceneMutation(context);
    co_return;
  }

  const auto extent = ResolveRenderExtent();
  if (HasPositiveExtent(extent)
    && (extent.width != last_viewport_.width
      || extent.height != last_viewport_.height)) {
    UpdateCameras(extent);
    last_viewport_ = extent;
  }

  co_await Base::OnSceneMutation(context);
  co_return;
}

auto MainModule::OnPreRender(observer_ptr<engine::FrameContext> context)
  -> oxygen::co::Co<>
{
  co_await Base::OnPreRender(context);
  co_return;
}

auto MainModule::UpdateComposition(oxygen::engine::FrameContext& context,
  std::vector<vortex::CompositionView>& views) -> void
{
  auto& shell = GetShell();
  if (!main_camera_node_.IsAlive()) {
    return;
  }

  const auto extent = ResolveRenderExtent();
  if (!HasPositiveExtent(extent)) {
    DLOG_F(2, "[MultiView] Skip composition update for invalid extent {}x{}",
      extent.width, extent.height);
    return;
  }
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
  main_view.scissor = Scissors {
    .left = 0,
    .top = 0,
    .right = static_cast<int32_t>(extent.width),
    .bottom = static_cast<int32_t>(extent.height),
  };
  auto main_comp = vortex::CompositionView::ForScene(
    main_view_id_, main_view, main_camera_node_);
  main_comp.with_atmosphere = true;
  shell.OnMainViewReady(context, main_comp);
  const graphics::Color kMainClearColor { 0.1F, 0.2F, 0.38F, 1.0F };
  main_comp.clear_color = kMainClearColor;
  views.push_back(std::move(main_comp));

  // 2. PiP View
  if (pip_camera_node_.IsAlive()) {
    const auto pip_layout = ComputePipLayout(extent);

    View pip_view {};
    pip_view.viewport = ViewPort {
      .top_left_x = pip_layout.offset_x,
      .top_left_y = pip_layout.offset_y,
      .width = pip_layout.width,
      .height = pip_layout.height,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    pip_view.scissor = Scissors {
      .left = static_cast<int32_t>(pip_layout.offset_x),
      .top = static_cast<int32_t>(pip_layout.offset_y),
      .right = static_cast<int32_t>(pip_layout.offset_x + pip_layout.width),
      .bottom = static_cast<int32_t>(pip_layout.offset_y + pip_layout.height),
    };

    if (config_.pip_scissor_inset_px > 0U) {
      const auto inset = static_cast<int32_t>(config_.pip_scissor_inset_px);
      pip_view.scissor.left
        = (std::min)(pip_view.scissor.left + inset, pip_view.scissor.right);
      pip_view.scissor.top
        = (std::min)(pip_view.scissor.top + inset, pip_view.scissor.bottom);
      pip_view.scissor.right
        = (std::max)(pip_view.scissor.left, pip_view.scissor.right - inset);
      pip_view.scissor.bottom
        = (std::max)(pip_view.scissor.top, pip_view.scissor.bottom - inset);
      CHECK_F(pip_view.scissor.left < pip_view.scissor.right
          && pip_view.scissor.top < pip_view.scissor.bottom,
        "PiP scissor inset {} collapses the PiP depth rect (viewport={}x{})",
        config_.pip_scissor_inset_px, pip_layout.width, pip_layout.height);
    }

    auto pip_comp = vortex::CompositionView::ForPip(pip_view_id_,
      vortex::CompositionView::ZOrder {
        vortex::CompositionView::kZOrderScene.get() + 1 },
      pip_view, pip_camera_node_);

    const graphics::Color kPipClearColor { 0.1F, 0.1F, 0.1F, 0.5F };
    pip_comp.clear_color = kPipClearColor;
    pip_comp.opacity = 1.0F;
    pip_comp.force_wireframe = config_.pip_force_wireframe;

    views.push_back(std::move(pip_comp));
  }

  // 3. ImGui
  const auto imgui_view_id = this->GetOrCreateViewId("ImGuiView");
  views.push_back(vortex::CompositionView::ForImGui(
    imgui_view_id, main_view, [](graphics::CommandRecorder&) { }));
}

auto MainModule::ClearBackbufferReferences() -> void { }

} // namespace oxygen::examples::multiview
