//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Renderer/Renderer.h>

#include "MultiView/MainModule.h"
#include "MultiView/MainView.h"
#include "MultiView/PipView.h"

namespace oxygen::examples::multiview {

MainModule::MainModule(const common::AsyncEngineApp& app) noexcept
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
    core::PhaseId::kSceneMutation, core::PhaseId::kPreRender,
    core::PhaseId::kCompositing>();
}

auto MainModule::OnExampleFrameStart(engine::FrameContext& context) -> void
{
  // Check if we need to drop resources (e.g. resize)
  if (app_window_ && app_window_->ShouldResize()) {
    LOG_F(INFO, "[MultiView] Window resize detected, releasing view resources");
    ReleaseAllViews("window resize");
  }

  // CRITICAL: Ensure scene is created and set on context
  const auto scene = scene_bootstrapper_.EnsureScene();
  if (scene) {
    context.SetScene(oxygen::observer_ptr { scene.get() });
  }

  // Initialize views on first frame
  if (!initialized_ && scene) {
    // Ensure scene+content exists and use the returned shared_ptr (nodiscard)
    const auto content_scene = scene_bootstrapper_.EnsureSceneWithContent();
    if (content_scene) {
      for (auto& view : views_) {
        view->Initialize(*content_scene);
      }
      initialized_ = true;
    }
  }
}

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  if (!app_window_) {
    ReleaseAllViews("app window unavailable");
    co_return;
  }

  const auto surface_weak = app_window_->GetSurface();
  if (surface_weak.expired()) {
    LOG_F(WARNING, "[MultiView] No surface available");
    ReleaseAllViews("surface expired");
    co_return;
  }
  const auto surface = surface_weak.lock();

  if (!app_.renderer) {
    ReleaseAllViews("renderer unavailable");
    co_return;
  }

  auto gfx = app_.renderer->GetGraphics();
  if (!gfx) {
    ReleaseAllViews("graphics device unavailable");
    co_return;
  }

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
  if (!recorder) {
    co_return;
  }

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
  if (!app_window_) {
    co_return;
  }

  const auto surface_weak = app_window_->GetSurface();
  if (surface_weak.expired()) {
    co_return;
  }

  // CRITICAL: Call OnPreRender on each view to configure their renderers
  auto gfx = app_.renderer->GetGraphics();
  if (!gfx) {
    co_return;
  }

  for (auto& view : views_) {
    co_await view->OnPreRender(*app_.renderer);
  }

  co_return;
}

auto MainModule::OnCompositing(engine::FrameContext& context) -> co::Co<>
{
  LOG_F(INFO, "[MultiView] OnCompositing start: initialized={}, app_window={}",
    initialized_, static_cast<bool>(app_window_));

  if (!initialized_ || !app_window_) {
    co_return;
  }

  const auto surface_weak = app_window_->GetSurface();
  if (surface_weak.expired()) {
    LOG_F(WARNING, "[MultiView] No surface available");
    co_return;
  }
  auto surface = surface_weak.lock();

  auto gfx = app_.renderer->GetGraphics();
  if (!gfx) {
    LOG_F(ERROR, "[MultiView] Graphics context is null");
    co_return;
  }

  const auto fb_weak = app_window_->GetCurrentFrameBuffer();
  if (fb_weak.expired()) {
    LOG_F(ERROR, "[MultiView] Framebuffer is not valid");
    co_return;
  }
  const auto fb = fb_weak.lock();

  const auto& fb_desc = fb->GetDescriptor();
  if (fb_desc.color_attachments.empty()
    || !fb_desc.color_attachments[0].texture) {
    LOG_F(ERROR, "[MultiView] Backbuffer has no color attachment");
    co_return;
  }
  auto& backbuffer = *fb_desc.color_attachments[0].texture;
  LOG_F(INFO, "[MultiView] Backbuffer texture acquired");

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder
    = gfx->AcquireCommandRecorder(queue_key, "MultiView Compositing");

  if (recorder) {
    LOG_F(INFO, "[MultiView] Command recorder acquired");
    TrackSwapchainFramebuffer(*recorder, *fb);
    LOG_F(INFO, "[MultiView] Swapchain framebuffer tracked");

    // Composite all views
    LOG_F(INFO, "[MultiView] Compositing {} views", views_.size());
    for (auto& view : views_) {
      view->Composite(*recorder, backbuffer);
    }

    // CRITICAL: Transition backbuffer to present state
    LOG_F(INFO, "[MultiView] Transitioning backbuffer to kPresent");
    recorder->RequireResourceStateFinal(
      backbuffer, graphics::ResourceStates::kPresent);
    recorder->FlushBarriers();
  }

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
  if (!surface) {
    return;
  }

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
}

} // namespace oxygen::examples::multiview
