//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Host/HostWindow.h>
#include <Oxygen/Host/ViewPublisher.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneCameraViewResolver.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>

namespace oxygen::host {

namespace {

  constexpr std::uint64_t kFirstViewId = 1000U;

  auto ClampDimension(const float value) -> std::uint32_t
  {
    return std::max(1U, static_cast<std::uint32_t>(value));
  }

  auto IsSceneView(const vortex::CompositionView& view) -> bool
  {
    return view.camera.has_value() && view.id != kInvalidViewId;
  }

  auto MakeColorFramebuffer(Graphics& gfx, const Extent<std::uint32_t> size,
    const Format format, std::string debug_name)
    -> std::shared_ptr<graphics::Framebuffer>
  {
    graphics::TextureDesc desc {};
    desc.width = size.width;
    desc.height = size.height;
    desc.format = format;
    desc.texture_type = TextureType::kTexture2D;
    desc.is_render_target = true;
    desc.is_shader_resource = true;
    desc.initial_state = graphics::ResourceStates::kCommon;
    desc.use_clear_value = true;
    desc.clear_value = graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
    desc.debug_name = std::move(debug_name);
    auto texture = gfx.CreateTexture(desc);
    CHECK_F(static_cast<bool>(texture), "Failed to create texture '{}'",
      desc.debug_name);
    auto framebuffer
      = gfx.CreateFramebuffer(graphics::FramebufferDesc {}.AddColorAttachment(
        { .texture = std::move(texture) }));
    CHECK_F(static_cast<bool>(framebuffer),
      "Failed to create the framebuffer for '{}'", desc.debug_name);
    return framebuffer;
  }

} // namespace

ViewPublisher::ViewPublisher(const observer_ptr<AsyncEngine> engine,
  std::weak_ptr<Graphics> gfx, const observer_ptr<HostWindow> window)
  : engine_(engine)
  , gfx_weak_(std::move(gfx))
  , window_(window)
  , next_view_id_(kFirstViewId)
{
  CHECK_NOTNULL_F(engine_);
}

ViewPublisher::~ViewPublisher() { ClearSceneTargets(); }

auto ViewPublisher::SetMainViewReadyHandler(MainViewReadyHandler handler)
  -> void
{
  main_view_ready_ = std::move(handler);
}

auto ViewPublisher::SetRetainInactiveViewPredicate(
  RetainInactiveViewPredicate predicate) -> void
{
  retain_inactive_view_ = std::move(predicate);
}

auto ViewPublisher::HasLiveWindow() const noexcept -> bool
{
  return window_ != nullptr && window_->GetWindow() != nullptr
    && !window_->IsShuttingDown();
}

auto ViewPublisher::ResolveRenderer() const noexcept
  -> observer_ptr<vortex::Renderer>
{
  if (const auto renderer = engine_->GetModule<vortex::Renderer>()) {
    return observer_ptr { &renderer->get() };
  }
  return nullptr;
}

auto ViewPublisher::BeginFrame(engine::FrameContext& context) -> void
{
  if (window_ == nullptr) {
    return;
  }
  if (!window_->GetWindow()) {
    if (last_surface_) {
      const auto surfaces = context.GetSurfaces();
      for (std::size_t i = 0; i < surfaces.size(); ++i) {
        if (surfaces[i] == last_surface_) {
          context.RemoveSurfaceAt(i);
          break;
        }
      }
      last_surface_ = nullptr;
    }
    return;
  }

  if (window_->ShouldResize()) {
    window_->ApplyPendingResize();
    ClearSceneTargets();
  }

  const auto surface = window_->GetSurface().lock();
  if (!surface) {
    last_surface_ = nullptr;
    return;
  }
  const auto surfaces = context.GetSurfaces();
  const bool registered = std::ranges::any_of(
    surfaces, [&](const auto& s) -> bool { return s.get() == surface.get(); });
  if (!registered) {
    context.AddSurface(observer_ptr { surface.get() });
  }
  last_surface_ = observer_ptr { surface.get() };
}

auto ViewPublisher::ResolveViewport(const vortex::CompositionView& view) const
  -> ViewPort
{
  if (view.view.viewport.IsValid()) {
    return view.view.viewport;
  }
  auto viewport = ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = 1.0F,
    .height = 1.0F,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  if (const auto window = window_ ? window_->GetWindow() : nullptr) {
    const auto extent = window->Size();
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
  }
  return viewport;
}

auto ViewPublisher::Normalize(const vortex::CompositionView& source) const
  -> vortex::CompositionView
{
  // A scene view renders into its own target, so its viewport starts at the
  // origin and the scissor moves with it.
  const auto viewport = ResolveViewport(source);
  auto target = source;
  target.view.viewport = viewport;
  target.view.viewport.top_left_x = 0.0F;
  target.view.viewport.top_left_y = 0.0F;
  auto& scissor = target.view.scissor;
  if (scissor.right > scissor.left && scissor.bottom > scissor.top) {
    const auto offset_x = static_cast<std::int32_t>(viewport.top_left_x);
    const auto offset_y = static_cast<std::int32_t>(viewport.top_left_y);
    scissor.left = std::max(0, scissor.left - offset_x);
    scissor.top = std::max(0, scissor.top - offset_y);
    scissor.right = std::max(scissor.left, scissor.right - offset_x);
    scissor.bottom = std::max(scissor.top, scissor.bottom - offset_y);
  }
  target.shading_mode
    = source.shading_mode.value_or(vortex::ShadingMode::kDeferred);
  return target;
}

auto ViewPublisher::Publish(engine::FrameContext& context,
  const std::span<const vortex::CompositionView> views,
  const std::optional<ViewId> main_view) -> void
{
  layers_.clear();
  const auto renderer = ResolveRenderer();
  if (!renderer) {
    return;
  }
  const auto frame = observer_ptr { &context };
  if ((window_ != nullptr && !HasLiveWindow()) || !context.GetScene()) {
    ReleaseInactiveViews(frame, {});
    return;
  }

  std::vector<ViewId> published;
  published.reserve(views.size());
  for (const auto& view : views) {
    if (IsSceneView(view)) {
      published.push_back(view.id);
    }
  }
  ReleaseInactiveViews(frame, published);

  for (const auto& view : views) {
    if (!view.camera.has_value() || view.id == kInvalidViewId) {
      continue;
    }
    const auto& camera = *view.camera;
    const auto viewport = ResolveViewport(view);
    layers_.push_back(Layer {
      .view_id = view.id,
      .z_order = view.z_order,
      .viewport = viewport,
      .opacity = view.opacity,
    });

    auto normalized = Normalize(view);
    // A Fixed camera renders only its content rectangle; the renderer
    // composes the bars around it.
    const auto content
      = vortex::ResolveCameraContentRect(camera, normalized.view.viewport);
    const auto target = EnsureSceneTarget(
      view.id, ClampDimension(content.width), ClampDimension(content.height));
    if (target == nullptr) {
      LOG_F(WARNING, "Skip publishing view {}: no framebuffer", view.id.get());
      continue;
    }
    renderer->PublishRuntimeCompositionView(context,
      vortex::Renderer::RuntimeViewPublishInput {
        .composition_view = std::move(normalized),
        .render_target = observer_ptr { target->scene_framebuffer.get() },
        .composite_source
        = observer_ptr { target->composite_framebuffer.get() },
      });
    if (main_view == view.id && main_view_ready_) {
      main_view_ready_(view.id, camera, viewport);
    }
  }

  std::ranges::stable_sort(
    layers_, [](const Layer& left, const Layer& right) -> bool {
      return left.z_order < right.z_order;
    });
}

auto ViewPublisher::Compose(const ExtraLayersHandler& extra_layers) -> void
{
  if (!HasLiveWindow()) {
    return;
  }
  const auto renderer = ResolveRenderer();
  if (!renderer) {
    return;
  }
  auto target = window_->GetCurrentFrameBuffer().lock();
  auto surface = window_->GetSurface().lock();
  if (!target || !surface) {
    LOG_F(WARNING, "Skip compositing: the window has no target");
    return;
  }

  vortex::Renderer::RuntimeCompositionInput input {};
  input.composite_target = std::move(target);
  input.target_surface = std::move(surface);
  input.layers.reserve(layers_.size());
  for (const auto& layer : layers_) {
    input.layers.push_back(vortex::Renderer::RuntimeCompositionLayer {
      .intent_view_id = layer.view_id,
      .viewport = layer.viewport,
      .opacity = layer.opacity,
    });
  }
  if (extra_layers) {
    extra_layers(input);
  }
  renderer->RegisterRuntimeComposition(input);
}

auto ViewPublisher::Shutdown() -> void
{
  if (const auto renderer = ResolveRenderer()) {
    for (const auto& [_, view_id] : view_ids_) {
      renderer->RemovePublishedRuntimeView(view_id);
    }
  }
  ClearSceneTargets();
  view_ids_.clear();
  layers_.clear();
  last_surface_ = nullptr;
}

auto ViewPublisher::GetOrCreateViewId(const std::string_view name) -> ViewId
{
  if (const auto it = view_ids_.find(name); it != view_ids_.end()) {
    return it->second;
  }
  const ViewId id { next_view_id_++ };
  view_ids_.emplace(std::string(name), id);
  return id;
}

auto ViewPublisher::ReleaseInactiveViews(
  const observer_ptr<engine::FrameContext> context,
  const std::span<const ViewId> retained) -> void
{
  const auto renderer = ResolveRenderer();
  if (!renderer) {
    return;
  }
  for (const auto& [_, view_id] : view_ids_) {
    if (std::ranges::find(retained, view_id) != retained.end()) {
      continue;
    }
    if (context != nullptr && retain_inactive_view_
      && retain_inactive_view_(view_id)) {
      // Keep the view's history; it publishes no outputs this frame.
      const auto published = renderer->ResolvePublishedRuntimeViewId(view_id);
      if (published != kInvalidViewId) {
        auto inactive = context->GetViewContext(published);
        inactive.render_target = {};
        inactive.composite_source = {};
        context->UpdateView(published, std::move(inactive));
        continue;
      }
    }
    if (context != nullptr) {
      renderer->RemovePublishedRuntimeView(*context, view_id);
    } else {
      renderer->RemovePublishedRuntimeView(view_id);
    }
    if (const auto it = scene_targets_.find(view_id);
      it != scene_targets_.end()) {
      RetireSceneTarget(it->second);
      scene_targets_.erase(it);
    }
  }
}

auto ViewPublisher::EnsureSceneTarget(
  const ViewId view_id, const std::uint32_t width, const std::uint32_t height)
  -> observer_ptr<SceneTarget>
{
  if (const auto it = scene_targets_.find(view_id); it != scene_targets_.end()
    && it->second.scene_framebuffer && it->second.composite_framebuffer
    && it->second.width == width && it->second.height == height) {
    return observer_ptr { &it->second };
  }
  const auto gfx = gfx_weak_.lock();
  if (!gfx) {
    return nullptr;
  }
  const auto size = Extent<std::uint32_t> { .width = width, .height = height };
  auto scene_framebuffer = MakeColorFramebuffer(*gfx, size,
    Format::kRGBA16Float, fmt::format("Host.SceneColor.{}", view_id.get()));
  auto composite_framebuffer = MakeColorFramebuffer(*gfx, size,
    Format::kRGBA8UNorm, fmt::format("Host.CompositeColor.{}", view_id.get()));

  auto& target = scene_targets_[view_id];
  RetireSceneTarget(target);
  target = SceneTarget {
    .scene_framebuffer = std::move(scene_framebuffer),
    .composite_framebuffer = std::move(composite_framebuffer),
    .width = width,
    .height = height,
  };
  return observer_ptr { &target };
}

auto ViewPublisher::RetireSceneTarget(SceneTarget& target) -> void
{
  // Earlier frames may still read the attachments; release them only when
  // their frame slot retires.
  if (const auto gfx = gfx_weak_.lock()) {
    graphics::DeferredObjectRelease(
      target.scene_framebuffer, gfx->GetDeferredReclaimer());
    graphics::DeferredObjectRelease(
      target.composite_framebuffer, gfx->GetDeferredReclaimer());
  } else {
    target.scene_framebuffer.reset();
    target.composite_framebuffer.reset();
  }
}

auto ViewPublisher::ClearSceneTargets() -> void
{
  for (auto& [_, target] : scene_targets_) {
    RetireSceneTarget(target);
  }
  scene_targets_.clear();
}

} // namespace oxygen::host
