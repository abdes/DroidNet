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

#include "MultiView/ImGuiView.h"
#include "MultiView/MainModule.h"
#include "MultiView/MainView.h"
#include "MultiView/PipView.h"

namespace oxygen::examples::multiview {

MainModule::MainModule(
  const DemoAppContext& app, CompositingMode compositing_mode) noexcept
  : Base(app)
  , app_(app)
  , compositing_mode_(compositing_mode)
{
  // Create views
  auto main_view = std::make_unique<MainView>();
  main_view_.reset(main_view.get());
  views_.push_back(std::move(main_view));

  auto pip_view = std::make_unique<PipView>();
  pip_view_.reset(pip_view.get());
  views_.push_back(std::move(pip_view));

  auto imgui_view = std::make_unique<ImGuiView>();
  imgui_view_.reset(imgui_view.get());
  views_.push_back(std::move(imgui_view));
}

auto MainModule::GetSupportedPhases() const noexcept -> engine::ModulePhaseMask
{
  return engine::MakeModuleMask<core::PhaseId::kFrameStart,
    core::PhaseId::kSceneMutation, core::PhaseId::kGuiUpdate,
    core::PhaseId::kPreRender, core::PhaseId::kCompositing>();
}

auto MainModule::OnAttached(observer_ptr<AsyncEngine> engine) noexcept -> bool
{
  CHECK_F(static_cast<bool>(engine), "MultiView requires a valid engine");

  CHECK_F(Base::OnAttached(engine), "MultiView base attach failed");

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
  };
  shell_config.enable_camera_rig = false; // Non-interactive demo

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

  ReleaseAllViews("module shutdown");
}

auto MainModule::HandleOnFrameStart(engine::FrameContext& context) -> void
{
  CHECK_NOTNULL_F(app_window_, "AppWindow must exist in MultiView");
  CHECK_NOTNULL_F(shell_, "DemoShell must exist in MultiView");

  // Check if we need to drop resources (e.g. resize)
  if (app_window_->ShouldResize()) {
    LOG_F(INFO, "[MultiView] Window resize detected, releasing view resources");
    ReleaseAllViews("window resize");
  }

  // CRITICAL: Ensure scene is created and set on context
  if (!active_scene_.IsValid()) {
    auto scene = std::make_unique<scene::Scene>("MultiViewScene");
    active_scene_ = shell_->SetScene(std::move(scene));
    scene_bootstrapper_.BindToScene(shell_->TryGetScene());
  }

  const auto scene_ptr = shell_->TryGetScene();
  CHECK_F(static_cast<bool>(scene_ptr), "Scene must be available");
  context.SetScene(oxygen::observer_ptr { scene_ptr.get() });

  // Initialize views on first frame
  if (!initialized_) {
    // Ensure scene+content exists and use the returned scene (nodiscard)
    const auto content_scene = scene_bootstrapper_.EnsureSceneWithContent();
    CHECK_F(static_cast<bool>(content_scene),
      "SceneBootstrapper must produce content scene");
    for (auto& view : views_) {
      view->Initialize(*content_scene.get());
    }
    initialized_ = true;
  }
}

auto MainModule::OnGuiUpdate(engine::FrameContext& context) -> co::Co<>
{
  if (!app_window_ || !app_window_->GetWindow()) {
    co_return;
  }

  CHECK_NOTNULL_F(app_.engine, "Engine must exist for GUI update");
  auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>();
  CHECK_F(imgui_module_ref.has_value(), "ImGui module required");
  auto& imgui_module = imgui_module_ref->get();

  for (auto& view : views_) {
    view->SetImGuiModule(observer_ptr { &imgui_module });
  }

  if (imgui_view_) {
    imgui_view_->SetImGui(observer_ptr { &imgui_module });
  }

  CHECK_NOTNULL_F(shell_, "DemoShell required for GUI update");
  shell_->Draw(context);

  co_return;
}

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  CHECK_NOTNULL_F(app_window_, "AppWindow required for scene mutation");
  if (!app_window_->GetWindow()) {
    co_return;
  }

  const auto surface_weak = app_window_->GetSurface();
  if (surface_weak.expired()) {
    co_return;
  }
  const auto surface = surface_weak.lock();

  CHECK_NOTNULL_F(app_.renderer, "Renderer required for scene mutation");

  auto gfx = app_.renderer->GetGraphics();
  CHECK_F(static_cast<bool>(gfx), "Graphics required for scene mutation");

  // CRITICAL: Mark sphere transform as dirty (workaround)
  const auto sphere_node = scene_bootstrapper_.GetSphereNode();
  if (sphere_node.IsAlive()) {
    auto pos = sphere_node.GetTransform().GetLocalPosition();
    if (pos.has_value()) {
      sphere_node.GetTransform().SetLocalPosition(pos.value());
    }
  }

  // Create a SINGLE command recorder for all resource tracking
  // This matches the OLDMainModule pattern and ensures all tracking is in one
  // command list
  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(queue_key, "MultiView Setup");
  CHECK_F(static_cast<bool>(recorder), "Command recorder acquisition failed");

  // Call OnSceneMutation on each view, passing the shared recorder
  // This will also call RegisterView() which assigns real ViewIds
  LOG_F(INFO, "[MultiView] OnSceneMutation: {} views", views_.size());

  // Create rendering context to be shared by all views
  const DemoViewContext view_ctx {
    .frame_context = context,
    .graphics = *gfx,
    .surface = *surface,
    .recorder = *recorder,
  };

  for (auto& view : views_) {
    // Set graphics context for deferred resource release
    view->SetGraphicsContext(app_.gfx_weak);
    // Set rendering context (now used by views for resource creation)
    view->SetRenderingContext(view_ctx);
    // Call OnSceneMutation - no parameters needed anymore
    view->OnSceneMutation();
    view->RegisterViewForRendering(*app_.renderer);
  }

  // Clear the phase-specific recorder - it's no longer valid after
  // OnSceneMutation
  for (auto& view : views_) {
    view->ClearPhaseRecorder();
  }

  co_return;
}

auto MainModule::OnPreRender(engine::FrameContext& context) -> co::Co<>
{
  CHECK_NOTNULL_F(app_window_, "AppWindow required for pre-render");
  if (!app_window_->GetWindow()) {
    co_return;
  }

  const auto surface_weak = app_window_->GetSurface();
  if (surface_weak.expired()) {
    co_return;
  }

  // CRITICAL: Call OnPreRender on each view to configure their renderers
  CHECK_NOTNULL_F(app_.renderer, "Renderer required for pre-render");
  auto gfx = app_.renderer->GetGraphics();
  CHECK_F(static_cast<bool>(gfx), "Graphics required for pre-render");

  for (auto& view : views_) {
    co_await view->OnPreRender(*app_.renderer);
  }

  co_return;
}

auto MainModule::OnCompositing(engine::FrameContext& context) -> co::Co<>
{
  LOG_F(INFO, "[MultiView] OnCompositing start: initialized={}, app_window={}",
    initialized_, static_cast<bool>(app_window_));

  CHECK_F(initialized_, "Views must be initialized before compositing");
  CHECK_NOTNULL_F(app_window_, "AppWindow required for compositing");
  if (!app_window_->GetWindow()) {
    co_return;
  }

  const auto surface_weak = app_window_->GetSurface();
  if (surface_weak.expired()) {
    co_return;
  }
  auto surface = surface_weak.lock();

  CHECK_NOTNULL_F(app_.renderer, "Renderer required for compositing");
  auto gfx = app_.renderer->GetGraphics();
  CHECK_F(static_cast<bool>(gfx), "Graphics required for compositing");

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);

  const auto fb_weak = app_window_->GetCurrentFrameBuffer();
  CHECK_F(!fb_weak.expired(), "Swapchain framebuffer must exist");
  const auto fb = fb_weak.lock();

  CHECK_NOTNULL_F(main_view_, "MainView must exist for compositing");
  CHECK_NOTNULL_F(pip_view_, "PipView must exist for compositing");
  CHECK_NOTNULL_F(imgui_view_, "ImGuiView must exist for compositing");

  engine::CompositingTaskList tasks {};
  const auto fullscreen = BuildFullscreenViewport(*fb);
  CHECK_F(fullscreen.IsValid(), "Fullscreen viewport must be valid");

  if (compositing_mode_ == CompositingMode::kBlend) {
    auto main_texture = main_view_->GetColorTexture();
    CHECK_F(static_cast<bool>(main_texture),
      "MainView color texture required for blend");
    tasks.push_back(engine::CompositingTask::MakeTextureBlend(
      std::move(main_texture), fullscreen, 1.0F));
  } else {
    CHECK_F(
      main_view_->GetViewId().get() != 0, "MainView view id required for copy");
    tasks.push_back(
      engine::CompositingTask::MakeCopy(main_view_->GetViewId(), fullscreen));
  }

  const auto viewport = pip_view_->GetViewport();
  CHECK_F(viewport && viewport->IsValid(), "PiP viewport must be valid");

  // Keep task alpha at 1.0F so the PiP texture's own alpha (set on creation)
  // controls opacity. This preserves the clear color while making it half
  // transparent.
  const float alpha = 1.0F;
  if (compositing_mode_ == CompositingMode::kBlend) {
    auto pip_texture = pip_view_->GetColorTexture();
    CHECK_F(static_cast<bool>(pip_texture),
      "PipView color texture required for blend");
    tasks.push_back(engine::CompositingTask::MakeTextureBlend(
      std::move(pip_texture), *viewport, alpha));
  } else {
    CHECK_F(
      pip_view_->GetViewId().get() != 0, "PipView view id required for copy");
    tasks.push_back(
      engine::CompositingTask::MakeCopy(pip_view_->GetViewId(), *viewport));
  }

  // Add ImGui overlay
  if (imgui_view_->IsViewReady()) {
    auto ui_texture = imgui_view_->GetColorTexture();
    // Assuming UI covers the whole screen or we want to composite it as such
    if (ui_texture) {
      tasks.push_back(engine::CompositingTask::MakeTextureBlend(
        std::move(ui_texture), fullscreen, 1.0F));
    }
  }

  oxygen::engine::CompositionSubmission submission;
  submission.target_framebuffer = fb;
  submission.target_surface = surface;
  submission.tasks = std::move(tasks);
  app_.renderer->RegisterComposition(std::move(submission));

  LOG_F(INFO, "[MultiView] OnCompositing submit complete");
  co_return;
}

auto MainModule::ClearBackbufferReferences() -> void
{
  if (pipeline_) {
    pipeline_->ClearBackbufferReferences();
  }
}

auto MainModule::BuildDefaultWindowProperties() const
  -> platform::window::Properties
{
  auto props = Base::BuildDefaultWindowProperties();
  props.title = "Oxygen Engine - MultiView Example";
  return props;
}

// Helpers

auto MainModule::ReleaseAllViews(std::string_view reason) -> void
{
  LOG_F(INFO, "[MultiView] Releasing all views ({})", reason);
  for (auto& view : views_) {
    if (view) {
      view->ReleaseResources();
    }
  }

  initialized_ = false;
}

auto MainModule::BuildFullscreenViewport(
  const graphics::Framebuffer& target_framebuffer) const -> ViewPort
{
  const auto& fb_desc = target_framebuffer.GetDescriptor();
  if (fb_desc.color_attachments.empty()
    || !fb_desc.color_attachments[0].texture) {
    return {};
  }

  const auto& target_desc
    = fb_desc.color_attachments[0].texture->GetDescriptor();
  return ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(target_desc.width),
    .height = static_cast<float>(target_desc.height),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
}

} // namespace oxygen::examples::multiview
