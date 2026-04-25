//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <memory>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Renderer.h>

#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/DemoModuleBase.h"

namespace oxygen::examples {

namespace {

  auto ClampViewportDimension(const float value) -> std::uint32_t
  {
    return std::max(1U, static_cast<std::uint32_t>(value));
  }

  auto ResolveViewport(const vortex::CompositionView& view_intent,
    const observer_ptr<AppWindow> app_window) -> ViewPort
  {
    if (view_intent.view.viewport.IsValid()) {
      return view_intent.view.viewport;
    }

    if (app_window != nullptr && app_window->GetWindow()) {
      const auto extent = app_window->GetWindow()->Size();
      return ViewPort {
        .top_left_x = 0.0F,
        .top_left_y = 0.0F,
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .min_depth = 0.0F,
        .max_depth = 1.0F,
      };
    }

    return ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 1.0F,
      .height = 1.0F,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
  }

  auto NormalizeVortexCompositionView(const vortex::CompositionView& source,
    const observer_ptr<AppWindow> app_window) -> vortex::CompositionView
  {
    const auto viewport = ResolveViewport(source, app_window);

    auto target = source;
    target.view.viewport = viewport;
    if (source.camera.has_value() && viewport.IsValid()) {
      target.view.viewport.top_left_x = 0.0F;
      target.view.viewport.top_left_y = 0.0F;
      if (target.view.scissor.right > target.view.scissor.left
        && target.view.scissor.bottom > target.view.scissor.top) {
        const auto offset_x = static_cast<int32_t>(viewport.top_left_x);
        const auto offset_y = static_cast<int32_t>(viewport.top_left_y);
        target.view.scissor.left
          = std::max(0, target.view.scissor.left - offset_x);
        target.view.scissor.top
          = std::max(0, target.view.scissor.top - offset_y);
        target.view.scissor.right = std::max(
          target.view.scissor.left, target.view.scissor.right - offset_x);
        target.view.scissor.bottom = std::max(
          target.view.scissor.top, target.view.scissor.bottom - offset_y);
      }
    }
    target.with_height_fog = source.camera.has_value() && source.with_height_fog;
    target.with_local_fog = source.camera.has_value() && source.with_local_fog;
    target.shading_mode = source.camera.has_value()
      ? std::optional<vortex::ShadingMode> { vortex::ShadingMode::kDeferred }
      : std::nullopt;
    return target;
  }

  auto IsSceneView(const vortex::CompositionView& view_intent) -> bool
  {
    return view_intent.camera.has_value() && view_intent.id != kInvalidViewId;
  }

} // namespace

DemoModuleBase::DemoModuleBase(const DemoAppContext& app) noexcept
  : app_(app)
{
  LOG_SCOPE_FUNCTION(1);
  if (!app_.headless) {
    auto& wnd = AddComponent<AppWindow>(app_);
    app_window_ = observer_ptr(&wnd);
  }
}

DemoModuleBase::~DemoModuleBase()
{
  ClearViewIds();
  ClearSceneFramebuffers();
}

auto DemoModuleBase::BuildDefaultWindowProperties() const
  -> platform::window::Properties
{
  platform::window::Properties p("Oxygen Example");
  p.extent = { .width = 1280U, .height = 720U };
  p.flags = { .hidden = false, .resizable = true };
  if (app_.fullscreen) {
    p.flags.full_screen = true;
  }
  return p;
}

auto DemoModuleBase::OnAttached(observer_ptr<IAsyncEngine> engine) noexcept
  -> bool
{
  DCHECK_NOTNULL_F(engine);
  LOG_SCOPE_FUNCTION(1);

  if (!app_.headless) {
    DCHECK_NOTNULL_F(app_window_);

    const auto props = BuildDefaultWindowProperties();
    if (!app_window_->CreateAppWindow(props)) {
      LOG_F(ERROR, "-failed- could not create application window");
      return false;
    }
  }

  shell_ = OnAttachedImpl(engine);
  if (!shell_) {
    LOG_F(ERROR, "-failed- DemoShell initialization");
    return false;
  }

  return true;
}

auto DemoModuleBase::OnShutdown() noexcept -> void
{
  if (auto renderer = ResolveVortexRenderer(); renderer != nullptr) {
    for (const auto& [_, view_id] : view_registry_) {
      renderer->RemovePublishedRuntimeView(view_id);
    }
  }

  ClearSceneFramebuffers();
  shell_.reset();
  view_registry_.clear();
  active_views_.clear();
}

auto DemoModuleBase::GetShell() -> DemoShell&
{
  DCHECK_NOTNULL_F(shell_);
  return *shell_;
}

auto DemoModuleBase::ResolveVortexRenderer() const noexcept
  -> observer_ptr<vortex::Renderer>
{
  if (app_.engine == nullptr) {
    return nullptr;
  }
  if (auto renderer = app_.engine->GetModule<vortex::Renderer>()) {
    return observer_ptr { &renderer->get() };
  }
  return nullptr;
}

auto DemoModuleBase::HasRenderableWindow() const noexcept -> bool
{
  if (app_.headless) {
    return true;
  }
  return app_window_ != nullptr && app_window_->GetWindow() != nullptr
    && !app_window_->IsShuttingDown();
}

auto DemoModuleBase::OnFrameStart(observer_ptr<engine::FrameContext> context)
  -> void
{
  DCHECK_NOTNULL_F(context);
  try {
    OnFrameStartCommon(*context);
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "Report OnFrameStart error: {}", ex.what());
  }
}

auto DemoModuleBase::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  active_views_.clear();
  if (!app_.headless && app_window_ && !app_window_->GetWindow()) {
    LOG_F(INFO, "Skipping UpdateComposition: app window is no longer valid");
    co_return;
  }
  UpdateComposition(*context, active_views_);
  co_return;
}

auto DemoModuleBase::ReleaseInactiveRuntimeViews(
  const observer_ptr<engine::FrameContext> context,
  const std::vector<ViewId>& retained_intent_ids) -> void
{
  auto renderer = ResolveVortexRenderer();
  if (!renderer) {
    return;
  }

  for (const auto& [_, view_id] : view_registry_) {
    const bool should_retain = std::ranges::find(retained_intent_ids, view_id)
      != retained_intent_ids.end();
    if (should_retain) {
      continue;
    }

    if (context != nullptr) {
      renderer->RemovePublishedRuntimeView(*context, view_id);
    } else {
      renderer->RemovePublishedRuntimeView(view_id);
    }
    scene_targets_.erase(view_id);
  }
}

auto DemoModuleBase::EnsureSceneFramebuffer(
  const ViewId view_id, const uint32_t width, const uint32_t height)
  -> RuntimeSceneTarget*
{
  if (auto it = scene_targets_.find(view_id); it != scene_targets_.end()
    && it->second.scene_framebuffer && it->second.composite_framebuffer
    && it->second.width == width
    && it->second.height == height) {
    return &it->second;
  }

  auto gfx = app_.gfx_weak.lock();
  if (!gfx) {
    return {};
  }

  graphics::TextureDesc color_desc {};
  color_desc.width = width;
  color_desc.height = height;
  color_desc.format = Format::kRGBA16Float;
  color_desc.texture_type = TextureType::kTexture2D;
  color_desc.is_render_target = true;
  color_desc.is_shader_resource = true;
  color_desc.initial_state = graphics::ResourceStates::kCommon;
  color_desc.use_clear_value = true;
  color_desc.clear_value = graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
  color_desc.debug_name
    = fmt::format("DemoRuntime.SceneColor.{}", view_id.get());

  auto color_texture = gfx->CreateTexture(color_desc);
  CHECK_F(static_cast<bool>(color_texture),
    "Failed to create Vortex runtime scene texture for view {}", view_id.get());

  auto framebuffer_desc = graphics::FramebufferDesc {};
  framebuffer_desc.AddColorAttachment({ .texture = std::move(color_texture) });
  auto framebuffer = gfx->CreateFramebuffer(framebuffer_desc);
  CHECK_F(static_cast<bool>(framebuffer),
    "Failed to create Vortex runtime framebuffer for view {}", view_id.get());

  graphics::TextureDesc composite_desc {};
  composite_desc.width = width;
  composite_desc.height = height;
  composite_desc.format = Format::kRGBA8UNorm;
  composite_desc.texture_type = TextureType::kTexture2D;
  composite_desc.is_render_target = true;
  composite_desc.is_shader_resource = true;
  composite_desc.initial_state = graphics::ResourceStates::kCommon;
  composite_desc.use_clear_value = true;
  composite_desc.clear_value = graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
  composite_desc.debug_name
    = fmt::format("DemoRuntime.CompositeColor.{}", view_id.get());

  auto composite_texture = gfx->CreateTexture(composite_desc);
  CHECK_F(static_cast<bool>(composite_texture),
    "Failed to create Vortex runtime composite texture for view {}", view_id.get());

  auto composite_desc_fb = graphics::FramebufferDesc {};
  composite_desc_fb.AddColorAttachment({ .texture = std::move(composite_texture) });
  auto composite_framebuffer = gfx->CreateFramebuffer(composite_desc_fb);
  CHECK_F(static_cast<bool>(composite_framebuffer),
    "Failed to create Vortex runtime composite framebuffer for view {}", view_id.get());

  scene_targets_[view_id] = RuntimeSceneTarget {
    .scene_framebuffer = framebuffer,
    .composite_framebuffer = composite_framebuffer,
    .width = width,
    .height = height,
  };
  return &scene_targets_.at(view_id);
}

auto DemoModuleBase::ClearSceneFramebuffers() -> void
{
  scene_targets_.clear();
}

auto DemoModuleBase::OnPublishViews(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  auto renderer = ResolveVortexRenderer();
  if (!renderer) {
    co_return;
  }

  if (!app_.headless && app_window_
    && (!app_window_->GetWindow() || app_window_->IsShuttingDown())) {
    ReleaseInactiveRuntimeViews(context, {});
    co_return;
  }

  if (!context->GetScene()) {
    ReleaseInactiveRuntimeViews(context, {});
    co_return;
  }

  std::vector<ViewId> retained_scene_view_ids {};
  retained_scene_view_ids.reserve(active_views_.size());

  const vortex::CompositionView* primary_scene_view = nullptr;
  for (const auto& view_intent : active_views_) {
    if (!IsSceneView(view_intent)) {
      continue;
    }

    retained_scene_view_ids.push_back(view_intent.id);
    if (primary_scene_view == nullptr
      || view_intent.z_order.get() < primary_scene_view->z_order.get()) {
      primary_scene_view = &view_intent;
    }
  }

  ReleaseInactiveRuntimeViews(context, retained_scene_view_ids);

  for (const auto& view_intent : active_views_) {
    if (!IsSceneView(view_intent)) {
      continue;
    }

    auto vortex_view = NormalizeVortexCompositionView(view_intent, app_window_);
    const auto width = ClampViewportDimension(vortex_view.view.viewport.width);
    const auto height
      = ClampViewportDimension(vortex_view.view.viewport.height);
    auto* target = EnsureSceneFramebuffer(view_intent.id, width, height);
    if (target == nullptr || !target->scene_framebuffer
      || !target->composite_framebuffer) {
      LOG_F(WARNING,
        "Skipping Vortex runtime publication for view {} due to missing "
        "framebuffer",
        view_intent.id.get());
      continue;
    }

    renderer->PublishRuntimeCompositionView(*context,
      vortex::Renderer::RuntimeViewPublishInput {
        .composition_view = std::move(vortex_view),
        .render_target = observer_ptr { target->scene_framebuffer.get() },
        .composite_source = observer_ptr { target->composite_framebuffer.get() },
      });

    if (primary_scene_view == &view_intent) {
      GetShell().OnRuntimeMainViewReady(view_intent.id,
        view_intent.camera.value(), ResolveViewport(view_intent, app_window_));
    }
  }

  co_return;
}

auto DemoModuleBase::OnPreRender(observer_ptr<engine::FrameContext> /*context*/)
  -> co::Co<>
{
  co_return;
}

auto DemoModuleBase::OnCompositing(
  observer_ptr<engine::FrameContext> /*context*/) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);

  if (app_.headless || !app_window_->GetWindow()
    || app_window_->IsShuttingDown()) {
    co_return;
  }

  auto renderer = ResolveVortexRenderer();
  if (!renderer) {
    co_return;
  }

  auto target_fb = app_window_->GetCurrentFrameBuffer().lock();
  if (!target_fb) {
    LOG_F(WARNING, "Skip compositing: no valid framebuffer target");
    co_return;
  }

  auto surface = app_window_->GetSurface().lock();
  if (!surface) {
    LOG_F(WARNING, "Skip compositing: no valid target surface");
    co_return;
  }

  vortex::Renderer::RuntimeCompositionInput input {};
  input.composite_target = std::move(target_fb);
  input.target_surface = std::move(surface);
  input.layers.reserve(active_views_.size());

  for (const auto& view_intent : active_views_) {
    if (!IsSceneView(view_intent)) {
      continue;
    }

    const auto viewport = ResolveViewport(view_intent, app_window_);
    input.layers.push_back(vortex::Renderer::RuntimeCompositionLayer {
      .intent_view_id = view_intent.id,
      .viewport = viewport,
      .opacity = view_intent.opacity,
    });
  }

  renderer->RegisterRuntimeComposition(input);
  co_return;
}

auto DemoModuleBase::GetOrCreateViewId(std::string_view name) -> ViewId
{
  const std::string name_str(name);
  if (auto it = view_registry_.find(name_str); it != view_registry_.end()) {
    return it->second;
  }

  static std::atomic<uint64_t> s_next_view_id { 1000 };
  const ViewId new_id { s_next_view_id++ };
  view_registry_[name_str] = new_id;
  return new_id;
}

auto DemoModuleBase::ClearViewIds() -> void { view_registry_.clear(); }

auto DemoModuleBase::OnFrameStartCommon(engine::FrameContext& context) -> void
{
  if (app_.headless || !app_window_) {
    return;
  }
  if (!app_window_->GetWindow()) {
    if (last_surface_) {
      const auto surfaces = context.GetSurfaces();
      for (size_t i = 0; i < surfaces.size(); ++i) {
        if (surfaces[i] == last_surface_) {
          context.RemoveSurfaceAt(i);
          break;
        }
      }
      last_surface_ = nullptr;
    }
    return;
  }

  if (app_window_->ShouldResize()) {
    ClearBackbufferReferences();
    app_window_->ApplyPendingResize();
    ClearSceneFramebuffers();
  }

  auto surfaces = context.GetSurfaces();
  auto surface = app_window_->GetSurface().lock();
  if (surface) {
    const bool already_registered = std::ranges::any_of(
      surfaces, [&](const auto& s) { return s.get() == surface.get(); });
    if (!already_registered) {
      context.AddSurface(observer_ptr { surface.get() });
      DLOG_F(1, "Add surface: '{}'", surface->GetName());
    }
    last_surface_ = observer_ptr { surface.get() };
  } else {
    last_surface_ = nullptr;
  }
}

} // namespace oxygen::examples
