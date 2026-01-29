//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iterator>
#include <string_view>
#include <vector>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Renderer/Renderer.h>

#include "MultiView/MainModule.h"
#include "MultiView/MainView.h"
#include "MultiView/PipView.h"

namespace oxygen::examples::multiview {

MainModule::MainModule(const DemoAppContext& app) noexcept
  : Base(app)
  , app_(app)
{
  // Create views
  views_.push_back(std::make_unique<MainView>());
  views_.push_back(std::make_unique<PipView>());
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
  shell_config.input_system = observer_ptr { app_.input_system.get() };
  shell_config.get_renderer
    = [this]() { return observer_ptr { app_.renderer }; };
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

auto MainModule::OnExampleFrameStart(engine::FrameContext& context) -> void
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

auto MainModule::OnGuiUpdate(engine::FrameContext& /*context*/) -> co::Co<>
{
  if (!app_window_ || !app_window_->GetWindow()) {
    co_return;
  }

  CHECK_NOTNULL_F(app_.engine, "Engine must exist for GUI update");
  auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>();
  CHECK_F(imgui_module_ref.has_value(), "ImGui module required");
  auto& imgui_module = imgui_module_ref->get();
  if (!imgui_module.IsWitinFrameScope()) {
    LOG_F(INFO, "[MultiView] ImGui frame not active; skipping GUI update");
    co_return;
  }
  if (auto* imgui_context = imgui_module.GetImGuiContext()) {
    ImGui::SetCurrentContext(imgui_context);
  }

  for (auto& view : views_) {
    view->SetImGuiModule(observer_ptr { &imgui_module });
  }

  CHECK_NOTNULL_F(shell_, "DemoShell required for GUI update");
  shell_->Draw();

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

  const auto fb_weak = app_window_->GetCurrentFrameBuffer();
  CHECK_F(!fb_weak.expired(), "Swapchain framebuffer must exist");
  const auto fb = fb_weak.lock();

  const auto& fb_desc = fb->GetDescriptor();
  CHECK_F(!fb_desc.color_attachments.empty(),
    "Backbuffer must have a color attachment");
  CHECK_F(static_cast<bool>(fb_desc.color_attachments[0].texture),
    "Backbuffer color attachment missing");
  auto& backbuffer = *fb_desc.color_attachments[0].texture;
  LOG_F(INFO, "[MultiView] Backbuffer texture acquired");

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder
    = gfx->AcquireCommandRecorder(queue_key, "MultiView Compositing");

  CHECK_F(
    static_cast<bool>(recorder), "Compositing recorder acquisition failed");
  LOG_F(INFO, "[MultiView] Command recorder acquired");
  TrackSwapchainFramebuffer(*recorder, *fb);
  LOG_F(INFO, "[MultiView] Swapchain framebuffer tracked");

  std::vector<DemoView*> view_ptrs;
  view_ptrs.reserve(views_.size());
  std::ranges::transform(views_, std::back_inserter(view_ptrs),
    [](const auto& view) { return view.get(); });

  LOG_F(INFO, "[MultiView] Compositing {} views", views_.size());
  CompositorGraph::Inputs inputs {
    .views = view_ptrs,
    .recorder = *recorder,
    .backbuffer = backbuffer,
    .backbuffer_framebuffer = *fb,
  };
  co_await compositor_graph_.Execute(inputs);

  // CRITICAL: Transition backbuffer to present state
  LOG_F(INFO, "[MultiView] Transitioning backbuffer to kPresent");
  recorder->RequireResourceStateFinal(
    backbuffer, graphics::ResourceStates::kPresent);
  recorder->FlushBarriers();

  // CRITICAL: Mark surface as presentable
  LOG_F(INFO, "[MultiView] Marking surface as presentable");
  MarkSurfacePresentable(context, surface);

  LOG_F(INFO, "[MultiView] OnCompositing complete");
  co_return;
}

auto MainModule::ClearBackbufferReferences() -> void
{
  // This example does offscreen rendering and only composites to the swapchain,
  // which only uses tyemporary references to the backbuffers.
}

auto MainModule::BuildDefaultWindowProperties() const
  -> platform::window::Properties
{
  auto props = Base::BuildDefaultWindowProperties();
  props.title = "Oxygen Engine - MultiView Example";
  return props;
}

// Helpers
auto MainModule::AcquireCommandRecorder(Graphics& gfx)
  -> std::shared_ptr<graphics::CommandRecorder>
{
  const auto queue_key = gfx.QueueKeyFor(graphics::QueueRole::kGraphics);
  return gfx.AcquireCommandRecorder(queue_key, "MultiView");
}

auto MainModule::TrackSwapchainFramebuffer(graphics::CommandRecorder& recorder,
  const graphics::Framebuffer& framebuffer) -> void
{
  const auto& fb_desc = framebuffer.GetDescriptor();
  for (const auto& attachment : fb_desc.color_attachments) {
    if (attachment.texture) {
      recorder.BeginTrackingResourceState(
        *attachment.texture, graphics::ResourceStates::kPresent);
    }
  }

  if (fb_desc.depth_attachment.texture) {
    recorder.BeginTrackingResourceState(
      *fb_desc.depth_attachment.texture, graphics::ResourceStates::kDepthWrite);
    recorder.FlushBarriers();
  }
}

auto MainModule::MarkSurfacePresentable(engine::FrameContext& context,
  const std::shared_ptr<graphics::Surface>& surface) -> void
{
  CHECK_F(static_cast<bool>(surface), "Surface must be valid");

  const auto surfaces = context.GetSurfaces();
  for (size_t i = 0; i < surfaces.size(); ++i) {
    if (surfaces[i].get() == surface.get()) {
      context.SetSurfacePresentable(i, true);
      break;
    }
  }
}

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

} // namespace oxygen::examples::multiview
