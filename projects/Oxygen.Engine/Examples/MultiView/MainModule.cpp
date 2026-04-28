//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Core/Time/SimulationClock.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Renderer.h>

#include "DemoShell/UI/CameraRigController.h"
#include "MultiView/MainModule.h"

namespace oxygen::examples::multiview {

namespace {
  constexpr size_t kDefaultSceneCapacity = 128;
  constexpr uint32_t kOffscreenPreviewWidth = 512U;
  constexpr uint32_t kOffscreenPreviewHeight = 288U;
  constexpr uint32_t kOffscreenCaptureWidth = 256U;
  constexpr uint32_t kOffscreenCaptureHeight = 256U;

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

  auto ResolveFramebufferColorTexture(const graphics::Framebuffer& framebuffer)
    -> std::shared_ptr<graphics::Texture>
  {
    const auto& desc = framebuffer.GetDescriptor();
    if (desc.color_attachments.empty()) {
      return {};
    }
    return desc.color_attachments.front().texture;
  }

  auto MakeLocalView(const uint32_t width, const uint32_t height) -> View
  {
    View view {};
    view.viewport = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(width),
      .height = static_cast<float>(height),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view.scissor = Scissors {
      .left = 0,
      .top = 0,
      .right = static_cast<int32_t>(width),
      .bottom = static_cast<int32_t>(height),
    };
    return view;
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

  auto CreatePerspectiveCameraNode(scene::Scene& scene, std::string_view name)
    -> scene::SceneNode
  {
    auto node = scene.CreateNode(std::string(name));
    const bool attached
      = node.AttachCamera(std::make_unique<scene::PerspectiveCamera>());
    CHECK_F(attached, "Failed to attach {}", name);
    return node;
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
  top_view_id_ = this->GetOrCreateViewId("TopView");
  debug_view_id_ = this->GetOrCreateViewId("DebugView");
  shadow_view_id_ = this->GetOrCreateViewId("ShadowView");
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

  main_camera_node_ = CreatePerspectiveCameraNode(*staged_scene, "MainCamera");
  pip_camera_node_ = CreatePerspectiveCameraNode(*staged_scene, "PipCamera");
  if (config_.proof_layout) {
    top_camera_node_ = CreatePerspectiveCameraNode(*staged_scene, "TopCamera");
    debug_camera_node_
      = CreatePerspectiveCameraNode(*staged_scene, "DebugCamera");
    shadow_camera_node_
      = CreatePerspectiveCameraNode(*staged_scene, "ShadowCamera");
  }
  if (config_.offscreen_proof_layout) {
    offscreen_preview_camera_node_
      = CreatePerspectiveCameraNode(*staged_scene, "M06BOffscreenPreviewCamera");
    offscreen_capture_camera_node_
      = CreatePerspectiveCameraNode(*staged_scene, "M06BOffscreenCaptureCamera");
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
  for (auto* product : { &offscreen_preview_, &offscreen_capture_ }) {
    product->framebuffer.reset();
  }

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

  if (!config_.proof_layout && !config_.aux_proof_layout
    && !config_.offscreen_proof_layout) {
    return;
  }

  const float quadrant_aspect = extent.height > 0
    ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
    : 1.0F;
  const auto configure_camera = [quadrant_aspect](scene::SceneNode& node,
                                  const glm::vec3& position,
                                  const glm::vec3& target,
                                  const float fov_degrees) {
    if (!node.IsAlive()) {
      return;
    }
    const auto cam_opt = node.GetCameraAs<scene::PerspectiveCamera>();
    if (!cam_opt) {
      return;
    }

    auto& cam = cam_opt->get();
    const glm::mat4 view_mat = glm::lookAt(position, target, space::move::Up);
    node.GetTransform().SetLocalPosition(position);
    node.GetTransform().SetLocalRotation(glm::quat_cast(glm::inverse(view_mat)));
    cam.SetFieldOfView(glm::radians(fov_degrees));
    cam.SetAspectRatio(quadrant_aspect);
    cam.SetNearPlane(0.05F);
    cam.SetFarPlane(160.0F);
  };

  configure_camera(top_camera_node_, glm::vec3(0.0F, 8.0F, 0.1F),
    glm::vec3(0.0F, 0.0F, -2.0F), 45.0F);
  configure_camera(debug_camera_node_, glm::vec3(-5.0F, 2.5F, 5.5F),
    glm::vec3(0.0F, 0.0F, -2.0F), 42.0F);
  configure_camera(shadow_camera_node_, glm::vec3(5.0F, 3.5F, 4.5F),
    glm::vec3(0.0F, 0.0F, -2.0F), 42.0F);

  const auto configure_offscreen_camera = [](scene::SceneNode& node,
                                           const glm::vec3& position,
                                           const glm::vec3& target,
                                           const float fov_degrees,
                                           const uint32_t width,
                                           const uint32_t height) {
    if (!node.IsAlive()) {
      return;
    }
    const auto cam_opt = node.GetCameraAs<scene::PerspectiveCamera>();
    if (!cam_opt) {
      return;
    }

    auto& cam = cam_opt->get();
    const glm::mat4 view_mat = glm::lookAt(position, target, space::move::Up);
    node.GetTransform().SetLocalPosition(position);
    node.GetTransform().SetLocalRotation(glm::quat_cast(glm::inverse(view_mat)));
    cam.SetFieldOfView(glm::radians(fov_degrees));
    cam.SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));
    cam.SetNearPlane(0.05F);
    cam.SetFarPlane(160.0F);
    cam.SetViewport(MakeLocalView(width, height).viewport);
  };

  configure_offscreen_camera(offscreen_preview_camera_node_,
    glm::vec3(-3.2F, 2.2F, 4.8F), glm::vec3(0.0F, 0.0F, -1.6F), 38.0F,
    kOffscreenPreviewWidth, kOffscreenPreviewHeight);
  configure_offscreen_camera(offscreen_capture_camera_node_,
    glm::vec3(3.8F, 3.0F, 3.4F), glm::vec3(0.0F, 0.1F, -1.8F), 32.0F,
    kOffscreenCaptureWidth, kOffscreenCaptureHeight);
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

auto MainModule::EnsureOffscreenProofProduct(OffscreenProofProduct& product,
  const uint32_t width, const uint32_t height, std::string_view debug_name)
  -> bool
{
  auto gfx = app_.gfx_weak.lock();
  if (!gfx) {
    return false;
  }

  if (product.framebuffer && product.width == width && product.height == height) {
    return true;
  }

  product.framebuffer.reset();
  product.width = width;
  product.height = height;

  auto color_desc = graphics::TextureDesc {};
  color_desc.width = width;
  color_desc.height = height;
  color_desc.format = Format::kRGBA8UNorm;
  color_desc.texture_type = TextureType::kTexture2D;
  color_desc.is_render_target = true;
  color_desc.is_shader_resource = true;
  color_desc.initial_state = graphics::ResourceStates::kCommon;
  color_desc.use_clear_value = true;
  color_desc.clear_value = graphics::Color { 0.02F, 0.02F, 0.025F, 1.0F };
  color_desc.debug_name = std::string(debug_name);

  auto color = gfx->CreateTexture(color_desc);
  if (!color) {
    return false;
  }

  auto fb_desc = graphics::FramebufferDesc {};
  fb_desc.AddColorAttachment({ .texture = std::move(color) });
  product.framebuffer = gfx->CreateFramebuffer(fb_desc);
  return static_cast<bool>(product.framebuffer);
}

auto MainModule::RenderOffscreenProofProducts(engine::FrameContext& context)
  -> void
{
  auto renderer = ResolveVortexRenderer();
  if (!renderer) {
    return;
  }
  if (!active_scene_.IsValid() || !offscreen_preview_camera_node_.IsAlive()
    || !offscreen_capture_camera_node_.IsAlive()) {
    return;
  }

  const bool preview_ready = EnsureOffscreenProofProduct(offscreen_preview_,
    kOffscreenPreviewWidth, kOffscreenPreviewHeight,
    "M06B.OffscreenPreview.Deferred.Color");
  const bool capture_ready = EnsureOffscreenProofProduct(offscreen_capture_,
    kOffscreenCaptureWidth, kOffscreenCaptureHeight,
    "M06B.OffscreenCapture.Forward.Color");
  if (!preview_ready || !capture_ready) {
    LOG_F(WARNING, "[MultiView] M06B offscreen proof targets are not ready");
    return;
  }

  const auto delta_seconds
    = std::chrono::duration<float>(context.GetGameDeltaTime().get()).count();
  const auto frame_session = vortex::Renderer::FrameSessionInput {
    .frame_slot = context.GetFrameSlot(),
    .frame_sequence = context.GetFrameSequenceNumber(),
    .delta_time_seconds = std::max(
      delta_seconds, time::SimulationClock::kMinDeltaTimeSeconds),
    .scene = observer_ptr<scene::Scene> { active_scene_.operator->() },
  };

  auto render_product =
    [&](OffscreenProofProduct& product, const char* name, const ViewId view_id,
      const scene::SceneNode& camera,
      const vortex::Renderer::OffscreenPipelineInput pipeline,
      const bool force_wireframe) -> void {
    auto facade = renderer->ForOffscreenScene();
    facade.SetFrameSession(frame_session);
    facade.SetSceneSource(vortex::Renderer::SceneSourceInput {
      .scene = observer_ptr<scene::Scene> { active_scene_.operator->() },
    });
    facade.SetViewIntent(vortex::Renderer::OffscreenSceneViewInput::FromCamera(
                           name, view_id, MakeLocalView(product.width, product.height),
                           camera)
                           .SetWithAtmosphere(true)
                           .SetForceWireframe(force_wireframe)
                           .SetClearColor(
                             graphics::Color { 0.025F, 0.035F, 0.05F, 1.0F }));
    facade.SetOutputTarget(vortex::Renderer::OutputTargetInput {
      .framebuffer = observer_ptr<graphics::Framebuffer> {
        product.framebuffer.get() },
    });
    facade.SetPipeline(pipeline);

    const auto report = facade.Validate();
    if (!report.Ok()) {
      for (const auto& issue : report.issues) {
        LOG_F(WARNING, "[MultiView] M06B offscreen proof {} validation {}: {}",
          name, issue.code, issue.message);
      }
      return;
    }

    auto session = facade.Finalize();
    if (!session.has_value()) {
      LOG_F(WARNING, "[MultiView] M06B offscreen proof {} finalize failed",
        name);
      return;
    }

    session->ExecuteInsideFrame(context);
    LOG_F(INFO, "Vortex.OffscreenProof.Render name={} view_id={} texture={}",
      name, view_id.get(),
      ResolveFramebufferColorTexture(*product.framebuffer)
        ->GetDescriptor()
        .debug_name);
  };

  render_product(offscreen_preview_, "M06B.OffscreenPreview.Deferred",
    ViewId { 0x060B0101ULL }, offscreen_preview_camera_node_,
    vortex::Renderer::OffscreenPipelineInput::Deferred(), false);
  render_product(offscreen_capture_, "M06B.OffscreenCapture.Forward",
    ViewId { 0x060B0102ULL }, offscreen_capture_camera_node_,
    vortex::Renderer::OffscreenPipelineInput::Forward(), false);
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

    main_camera_node_ = CreatePerspectiveCameraNode(*staged_scene, "MainCamera");
    pip_camera_node_ = CreatePerspectiveCameraNode(*staged_scene, "PipCamera");
    if (config_.proof_layout || config_.aux_proof_layout) {
      top_camera_node_ = CreatePerspectiveCameraNode(*staged_scene, "TopCamera");
      debug_camera_node_
        = CreatePerspectiveCameraNode(*staged_scene, "DebugCamera");
      shadow_camera_node_
        = CreatePerspectiveCameraNode(*staged_scene, "ShadowCamera");
    }
    if (config_.offscreen_proof_layout) {
      offscreen_preview_camera_node_ = CreatePerspectiveCameraNode(
        *staged_scene, "M06BOffscreenPreviewCamera");
      offscreen_capture_camera_node_ = CreatePerspectiveCameraNode(
        *staged_scene, "M06BOffscreenCaptureCamera");
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
  if (config_.offscreen_proof_layout) {
    RenderOffscreenProofProducts(*context);
  }

  co_await Base::OnPreRender(context);
  co_return;
}

auto MainModule::AppendRuntimeCompositionLayers(engine::FrameContext& /*context*/,
  vortex::Renderer::RuntimeCompositionInput& input) -> void
{
  if (!config_.offscreen_proof_layout || !offscreen_preview_.framebuffer
    || !offscreen_capture_.framebuffer) {
    return;
  }

  const auto preview_texture
    = ResolveFramebufferColorTexture(*offscreen_preview_.framebuffer);
  const auto capture_texture
    = ResolveFramebufferColorTexture(*offscreen_capture_.framebuffer);
  if (!preview_texture || !capture_texture) {
    return;
  }

  const auto extent = ResolveRenderExtent();
  if (!HasPositiveExtent(extent)) {
    return;
  }

  constexpr float kMargin = 28.0F;
  constexpr float kGap = 18.0F;
  const float sw = static_cast<float>(extent.width);
  const float sh = static_cast<float>(extent.height);
  const float preview_w = std::min(512.0F, std::floor(sw * 0.30F));
  const float preview_h = std::floor(preview_w * 9.0F / 16.0F);
  const float capture_size = std::min(256.0F, std::floor(sh * 0.22F));
  const float panel_y = std::max(kMargin, sh - kMargin - preview_h);

  input.texture_layers.push_back(vortex::Renderer::RuntimeTextureCompositionLayer {
    .source_texture = preview_texture,
    .viewport = ViewPort {
      .top_left_x = kMargin,
      .top_left_y = panel_y,
      .width = preview_w,
      .height = preview_h,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    },
    .opacity = 1.0F,
    .debug_name = "M06B.OffscreenPreview.Deferred.Composite",
  });

  input.texture_layers.push_back(vortex::Renderer::RuntimeTextureCompositionLayer {
    .source_texture = capture_texture,
    .viewport = ViewPort {
      .top_left_x = kMargin + preview_w + kGap,
      .top_left_y = std::max(kMargin, sh - kMargin - capture_size),
      .width = capture_size,
      .height = capture_size,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    },
    .opacity = 1.0F,
    .debug_name = "M06B.OffscreenCapture.Forward.Composite",
  });
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

  if (config_.aux_proof_layout) {
    const float half_w = std::floor(sw * 0.5F);
    const float half_h = std::floor(sh * 0.5F);
    const auto make_view = [](const float x, const float y, const float width,
                             const float height) -> View {
      View view {};
      view.viewport = ViewPort {
        .top_left_x = x,
        .top_left_y = y,
        .width = width,
        .height = height,
        .min_depth = 0.0F,
        .max_depth = 1.0F,
      };
      view.scissor = Scissors {
        .left = static_cast<int32_t>(x),
        .top = static_cast<int32_t>(y),
        .right = static_cast<int32_t>(x + width),
        .bottom = static_cast<int32_t>(y + height),
      };
      return view;
    };

    auto consumer_comp = vortex::CompositionView::ForScene(main_view_id_,
      make_view(0.0F, 0.0F, half_w, half_h), main_camera_node_);
    consumer_comp.name = "M06A.AuxConsumer.Main";
    consumer_comp.with_atmosphere = true;
    consumer_comp.with_height_fog = true;
    consumer_comp.clear_color = graphics::Color { 0.05F, 0.09F, 0.16F, 1.0F };
    consumer_comp.consumed_aux_outputs.push_back(
      vortex::CompositionView::AuxInputDesc {
        .id = vortex::CompositionView::AuxOutputId { 7001U },
        .kind = vortex::CompositionView::AuxOutputKind::kColorTexture,
        .required = true,
      });
    shell.OnMainViewReady(context, consumer_comp);
    views.push_back(std::move(consumer_comp));

    auto producer_comp = vortex::CompositionView::ForScene(debug_view_id_,
      make_view(half_w, 0.0F, sw - half_w, half_h), debug_camera_node_);
    producer_comp.name = "M06A.AuxProducer.Color";
    producer_comp.view_kind = vortex::CompositionView::ViewKind::kAuxiliary;
    producer_comp.render_settings.shader_debug_mode
      = vortex::ShaderDebugMode::kWorldNormals;
    producer_comp.clear_color = graphics::Color { 0.09F, 0.04F, 0.12F, 1.0F };
    producer_comp.produced_aux_outputs.push_back(
      vortex::CompositionView::AuxOutputDesc {
        .id = vortex::CompositionView::AuxOutputId { 7001U },
        .kind = vortex::CompositionView::AuxOutputKind::kColorTexture,
        .debug_name = "M06A.AuxProducer.ColorTexture",
      });
    views.push_back(std::move(producer_comp));

    auto top_comp = vortex::CompositionView::ForScene(top_view_id_,
      make_view(0.0F, half_h, half_w, sh - half_h), top_camera_node_);
    top_comp.name = "M06A.AuxProof.WireframeTop";
    top_comp.force_wireframe = true;
    top_comp.feature_mask.bits = vortex::CompositionView::ViewFeatureBits {
      vortex::CompositionView::ViewFeatureMask::kSceneLighting
      | vortex::CompositionView::ViewFeatureMask::kDiagnostics
    };
    top_comp.clear_color = graphics::Color { 0.02F, 0.08F, 0.06F, 1.0F };
    views.push_back(std::move(top_comp));

    auto shadow_comp = vortex::CompositionView::ForScene(shadow_view_id_,
      make_view(half_w, half_h, sw - half_w, sh - half_h),
      shadow_camera_node_);
    shadow_comp.name = "M06A.AuxProof.DirectionalShadowMask";
    shadow_comp.render_settings.shader_debug_mode
      = vortex::ShaderDebugMode::kDirectionalShadowMask;
    shadow_comp.feature_mask.bits = vortex::CompositionView::ViewFeatureBits {
      vortex::CompositionView::ViewFeatureMask::kSceneLighting
      | vortex::CompositionView::ViewFeatureMask::kShadows
      | vortex::CompositionView::ViewFeatureMask::kDiagnostics
    };
    shadow_comp.clear_color = graphics::Color { 0.08F, 0.07F, 0.02F, 1.0F };
    views.push_back(std::move(shadow_comp));

    const auto imgui_view_id = this->GetOrCreateViewId("ImGuiView");
    views.push_back(vortex::CompositionView::ForImGui(
      imgui_view_id, make_view(0.0F, 0.0F, sw, sh),
      [](graphics::CommandRecorder&) { }));
    return;
  }

  if (config_.proof_layout) {
    const float half_w = std::floor(sw * 0.5F);
    const float half_h = std::floor(sh * 0.5F);
    const auto make_view = [](const float x, const float y, const float width,
                             const float height) -> View {
      View view {};
      view.viewport = ViewPort {
        .top_left_x = x,
        .top_left_y = y,
        .width = width,
        .height = height,
        .min_depth = 0.0F,
        .max_depth = 1.0F,
      };
      view.scissor = Scissors {
        .left = static_cast<int32_t>(x),
        .top = static_cast<int32_t>(y),
        .right = static_cast<int32_t>(x + width),
        .bottom = static_cast<int32_t>(y + height),
      };
      return view;
    };

    auto lit_comp = vortex::CompositionView::ForScene(main_view_id_,
      make_view(0.0F, 0.0F, half_w, half_h), main_camera_node_);
    lit_comp.name = "M06A.LitPerspective";
    lit_comp.with_atmosphere = true;
    lit_comp.with_height_fog = true;
    lit_comp.clear_color = graphics::Color { 0.05F, 0.11F, 0.20F, 1.0F };
    lit_comp.produced_aux_outputs.push_back(vortex::CompositionView::AuxOutputDesc {
      .id = vortex::CompositionView::AuxOutputId { 6001U },
      .kind = vortex::CompositionView::AuxOutputKind::kColorTexture,
      .debug_name = "M06A.LitPerspective.Color",
    });
    shell.OnMainViewReady(context, lit_comp);
    views.push_back(std::move(lit_comp));

    auto top_comp = vortex::CompositionView::ForScene(top_view_id_,
      make_view(half_w, 0.0F, sw - half_w, half_h), top_camera_node_);
    top_comp.name = "M06A.WireframeTop";
    top_comp.force_wireframe = true;
    top_comp.feature_mask.bits = vortex::CompositionView::ViewFeatureBits {
      vortex::CompositionView::ViewFeatureMask::kSceneLighting
      | vortex::CompositionView::ViewFeatureMask::kDiagnostics
    };
    top_comp.clear_color = graphics::Color { 0.02F, 0.08F, 0.06F, 1.0F };
    views.push_back(std::move(top_comp));

    auto debug_comp = vortex::CompositionView::ForScene(debug_view_id_,
      make_view(0.0F, half_h, half_w, sh - half_h), debug_camera_node_);
    debug_comp.name = "M06A.WorldNormals";
    debug_comp.render_settings.shader_debug_mode
      = vortex::ShaderDebugMode::kWorldNormals;
    debug_comp.consumed_aux_outputs.push_back(vortex::CompositionView::AuxInputDesc {
      .id = vortex::CompositionView::AuxOutputId { 6001U },
      .kind = vortex::CompositionView::AuxOutputKind::kColorTexture,
      .required = true,
    });
    debug_comp.clear_color = graphics::Color { 0.10F, 0.04F, 0.10F, 1.0F };
    views.push_back(std::move(debug_comp));

    auto shadow_comp = vortex::CompositionView::ForScene(shadow_view_id_,
      make_view(half_w, half_h, sw - half_w, sh - half_h),
      shadow_camera_node_);
    shadow_comp.name = "M06A.DirectionalShadowMask";
    shadow_comp.render_settings.shader_debug_mode
      = vortex::ShaderDebugMode::kDirectionalShadowMask;
    shadow_comp.feature_mask.bits = vortex::CompositionView::ViewFeatureBits {
      vortex::CompositionView::ViewFeatureMask::kSceneLighting
      | vortex::CompositionView::ViewFeatureMask::kShadows
      | vortex::CompositionView::ViewFeatureMask::kDiagnostics
    };
    shadow_comp.clear_color = graphics::Color { 0.08F, 0.07F, 0.02F, 1.0F };
    views.push_back(std::move(shadow_comp));

    const auto imgui_view_id = this->GetOrCreateViewId("ImGuiView");
    views.push_back(vortex::CompositionView::ForImGui(
      imgui_view_id, make_view(0.0F, 0.0F, sw, sh),
      [](graphics::CommandRecorder&) { }));
    return;
  }

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
