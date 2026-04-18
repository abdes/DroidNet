//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include <fmt/format.h>
#include <glm/vec2.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ImGui/ImGuiGraphicsBackend.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Profiling/CpuProfileScope.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/Internal/GpuTimelineProfiler.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/SceneSync/RuntimeMotionProducerModule.h>
#include <Oxygen/Vortex/Internal/CompositingPass.h>
#include <Oxygen/Vortex/Internal/ImGuiRuntime.h>
#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/Internal/RenderContextMaterializer.h>
#include <Oxygen/Vortex/Internal/RenderContextPool.h>
#include <Oxygen/Vortex/Internal/ViewConstantsManager.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/SceneCameraViewResolver.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderBuilder.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Vortex/Types/DrawFrameBindings.h>
#include <Oxygen/Vortex/Types/ViewFrameBindings.h>

namespace oxygen::vortex::internal {

auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}

} // namespace oxygen::vortex::internal

namespace oxygen::vortex {

struct RendererPublicationState {
  std::unique_ptr<internal::PerViewStructuredPublisher<DrawFrameBindings>>
    draw_frame_bindings_publisher;
  std::unique_ptr<internal::PerViewStructuredPublisher<ViewHistoryFrameBindings>>
    view_history_frame_bindings_publisher;
  std::unique_ptr<internal::PerViewStructuredPublisher<SceneTextureBindings>>
    scene_texture_bindings_publisher;
  std::unique_ptr<internal::PerViewStructuredPublisher<ViewFrameBindings>>
    view_frame_bindings_publisher;
  frame::SequenceNumber prepared_frame_sequence { 0U };
  frame::Slot prepared_frame_slot { frame::kInvalidSlot };
};

namespace {

  constexpr auto kRendererStagingAlignment
    = packing::kStructuredBufferAlignment;

  auto ClampViewportDimension(const float value) -> std::uint32_t
  {
    return std::max(1U, static_cast<std::uint32_t>(value));
  }

  auto ResolveBootstrapExtent(const CompositionView* composition_view)
    -> glm::uvec2
  {
    if (composition_view != nullptr
      && composition_view->view.viewport.IsValid()) {
      return {
        ClampViewportDimension(composition_view->view.viewport.width),
        ClampViewportDimension(composition_view->view.viewport.height),
      };
    }

    return { 1U, 1U };
  }

  auto ResolveViewOutputTexture(const engine::FrameContext& context,
    const ViewId view_id) -> std::shared_ptr<graphics::Texture>
  {
    try {
      const auto& view_ctx = context.GetViewContext(view_id);
      if (!view_ctx.composite_source) {
        LOG_F(ERROR,
          "View {} ('{}'/{}) missing composite_source framebuffer for "
          "compositing",
          view_id.get(), view_ctx.metadata.name, view_ctx.metadata.purpose);
        return {};
      }

      const auto& fb_desc = view_ctx.composite_source->GetDescriptor();
      if (fb_desc.color_attachments.empty()
        || !fb_desc.color_attachments[0].texture) {
        LOG_F(ERROR,
          "View {} ('{}'/{}) composite_source has no color attachment texture",
          view_id.get(), view_ctx.metadata.name, view_ctx.metadata.purpose);
        return {};
      }

      return fb_desc.color_attachments[0].texture;
    } catch (const std::exception& ex) {
      LOG_F(ERROR, "View {} output could not be resolved: {}", view_id.get(),
        ex.what());
      return {};
    }
  }

  auto TrackCompositionFramebuffer(graphics::CommandRecorder& recorder,
    const graphics::Framebuffer& framebuffer) -> void
  {
    const auto& fb_desc = framebuffer.GetDescriptor();
    for (const auto& attachment : fb_desc.color_attachments) {
      if (!attachment.texture) {
        continue;
      }

      auto initial = attachment.texture->GetDescriptor().initial_state;
      if (initial == graphics::ResourceStates::kUnknown
        || initial == graphics::ResourceStates::kUndefined) {
        initial = graphics::ResourceStates::kPresent;
      }
      recorder.BeginTrackingResourceState(*attachment.texture, initial, true);
    }

    if (fb_desc.depth_attachment.texture) {
      recorder.BeginTrackingResourceState(*fb_desc.depth_attachment.texture,
        graphics::ResourceStates::kDepthWrite, true);
      recorder.FlushBarriers();
    }
  }

  auto TrackCompositionSourceTexture(graphics::ResourceRegistry& registry,
    graphics::CommandRecorder& recorder, const graphics::Texture& texture)
    -> void
  {
    CHECK_F(registry.Contains(texture),
      "Vortex: composition source texture '{}' must be registered in the "
      "ResourceRegistry before compositing",
      texture.GetDescriptor().debug_name);
    if (recorder.IsResourceTracked(texture)) {
      return;
    }
    if (recorder.AdoptKnownResourceState(texture)) {
      return;
    }

    auto initial = texture.GetDescriptor().initial_state;
    if ((initial == graphics::ResourceStates::kUnknown
          || initial == graphics::ResourceStates::kUndefined)
      && texture.GetDescriptor().is_render_target) {
      initial = graphics::ResourceStates::kRenderTarget;
    }
    CHECK_F(initial != graphics::ResourceStates::kUnknown
        && initial != graphics::ResourceStates::kUndefined,
      "Vortex: composition source texture '{}' must either already have "
      "resource-state tracking or declare a valid initial_state",
      texture.GetDescriptor().debug_name);
    recorder.BeginTrackingResourceState(texture, initial);
  }

  auto CopyTextureToRegion(graphics::CommandRecorder& recorder,
    graphics::Texture& source, graphics::Texture& backbuffer,
    const ViewPort& viewport) -> void
  {
    CHECK_F(recorder.IsResourceTracked(source),
      "Vortex: copy source texture '{}' must already be tracked for "
      "compositing",
      source.GetDescriptor().debug_name);
    CHECK_F(recorder.IsResourceTracked(backbuffer),
      "Vortex: compositing backbuffer '{}' must already be tracked",
      backbuffer.GetDescriptor().debug_name);

    const auto& src_desc = source.GetDescriptor();
    const auto& dst_desc = backbuffer.GetDescriptor();

    const uint32_t dst_x = static_cast<uint32_t>(std::clamp(
      viewport.top_left_x, 0.0F, static_cast<float>(dst_desc.width)));
    const uint32_t dst_y = static_cast<uint32_t>(std::clamp(
      viewport.top_left_y, 0.0F, static_cast<float>(dst_desc.height)));

    const uint32_t max_dst_w
      = dst_desc.width > dst_x ? dst_desc.width - dst_x : 0U;
    const uint32_t max_dst_h
      = dst_desc.height > dst_y ? dst_desc.height - dst_y : 0U;

    const uint32_t copy_width = std::min(src_desc.width, max_dst_w);
    const uint32_t copy_height = std::min(src_desc.height, max_dst_h);

    if (copy_width == 0 || copy_height == 0) {
      return;
    }

    recorder.RequireResourceState(
      source, graphics::ResourceStates::kCopySource);
    recorder.RequireResourceState(
      backbuffer, graphics::ResourceStates::kCopyDest);
    recorder.FlushBarriers();

    const graphics::TextureSlice src_slice {
      .x = 0,
      .y = 0,
      .z = 0,
      .width = copy_width,
      .height = copy_height,
      .depth = 1,
    };

    const graphics::TextureSlice dst_slice {
      .x = dst_x,
      .y = dst_y,
      .z = 0,
      .width = copy_width,
      .height = copy_height,
      .depth = 1,
    };

    constexpr graphics::TextureSubResourceSet subresources {
      .base_mip_level = 0,
      .num_mip_levels = 1,
      .base_array_slice = 0,
      .num_array_slices = 1,
    };

    recorder.CopyTexture(
      source, src_slice, subresources, backbuffer, dst_slice, subresources);
  }

  auto FormatCompositingTaskScopeLabel(const CompositingTask& task)
    -> std::string
  {
    switch (task.type) {
    case CompositingTaskType::kCopy:
      return fmt::format(
        "Composite Copy View {}", task.copy.source_view_id.get());
    case CompositingTaskType::kBlend:
      return fmt::format("Composite Blend View {} (alpha {:.2f})",
        task.blend.source_view_id.get(), task.blend.alpha);
    case CompositingTaskType::kBlendTexture:
      if (task.texture_blend.source_texture) {
        const auto& name
          = task.texture_blend.source_texture->GetDescriptor().debug_name;
        if (!name.empty()) {
          return fmt::format("Composite Blend Texture {} (alpha {:.2f})", name,
            task.texture_blend.alpha);
        }
      }
      return fmt::format(
        "Composite Blend Texture (alpha {:.2f})", task.texture_blend.alpha);
    case CompositingTaskType::kTaa:
      return fmt::format(
        "Composite TAA (jitter {:.2f})", task.taa.jitter_scale);
    default:
      return "Composite Task";
    }
  }

} // namespace

Renderer::Renderer(std::weak_ptr<Graphics> graphics, RendererConfig config,
  const CapabilitySet capability_families)
  : gfx_weak_(std::move(graphics))
  , config_(std::move(config))
  , capability_families_(capability_families)
{
  CHECK_F(!gfx_weak_.expired(), "Renderer constructed with expired Graphics");
  CHECK_F(!config_.upload_queue_key.empty(),
    "RendererConfig.upload_queue_key must not be empty");

  auto gfx = gfx_weak_.lock();
  auto policy = upload::DefaultUploadPolicy();
  policy.upload_queue_key = graphics::QueueKey { config_.upload_queue_key };

  uploader_ = std::make_unique<upload::UploadCoordinator>(
    observer_ptr { gfx.get() }, policy);
  upload_staging_provider_ = uploader_->CreateRingBufferStaging(
    frame::kFramesInFlight, kRendererStagingAlignment,
    upload::kDefaultRingBufferStagingSlack, "Vortex.UploadStaging");

  inline_transfers_ = std::make_unique<upload::InlineTransfersCoordinator>(
    observer_ptr { gfx.get() });
  inline_staging_provider_ = std::make_shared<upload::RingBufferStaging>(
    upload::internal::UploaderTagFactory::Get(), observer_ptr { gfx.get() },
    frame::kFramesInFlight, kRendererStagingAlignment,
    upload::kDefaultRingBufferStagingSlack, "Vortex.InlineStaging");
  inline_transfers_->RegisterProvider(inline_staging_provider_);

  gpu_timeline_profiler_
    = std::make_unique<internal::GpuTimelineProfiler>(observer_ptr { gfx.get() });
  render_context_pool_
    = std::make_unique<internal::BasicRenderContextPool<RenderContext>>();
}

Renderer::Renderer(std::weak_ptr<Graphics> graphics, RendererConfig config)
  : Renderer(std::move(graphics), std::move(config),
      kPhase1DefaultRuntimeCapabilityFamilies)
{
}

Renderer::~Renderer()
{
  CHECK_F(shutdown_called_,
    "Renderer destroyed without prior OnShutdown(); EngineModule users must "
    "call OnShutdown() before destruction");
}

auto Renderer::OnAttached(observer_ptr<IAsyncEngine> engine) noexcept -> bool
{
  CHECK_F(!shutdown_called_,
    "Renderer::OnAttached() must not be called after shutdown");
  engine_ = engine;
  return true;
}

auto Renderer::RegisterConsoleBindings(
  observer_ptr<console::Console> console) noexcept -> void
{
  console_ = console;
}

auto Renderer::EnsureImGuiRuntime() -> internal::ImGuiRuntime*
{
  if (imgui_runtime_) {
    return imgui_runtime_.get();
  }
  if (!config_.enable_imgui
    || !HasCapability(RendererCapabilityFamily::kDiagnosticsAndProfiling)
    || engine_ == nullptr) {
    return nullptr;
  }

  const auto gfx = GetGraphics();
  if (!gfx) {
    return nullptr;
  }

  auto graphics_backend = gfx->CreateImGuiGraphicsBackend();
  if (!graphics_backend) {
    return nullptr;
  }

  auto platform = engine_->GetPlatformShared();
  if (!platform) {
    return nullptr;
  }

  auto runtime = std::make_unique<internal::ImGuiRuntime>(
    std::move(platform), std::move(graphics_backend));
  if (!runtime->Initialize(gfx)) {
    return nullptr;
  }

  imgui_runtime_ = std::move(runtime);
  return imgui_runtime_.get();
}

auto Renderer::ApplyConsoleCVars(
  observer_ptr<const console::Console> /*console*/) noexcept -> void
{
}

auto Renderer::OnShutdown() noexcept -> void
{
  if (shutdown_called_) {
    return;
  }
  shutdown_called_ = true;

  auto published_intent_ids = std::vector<ViewId> {};
  {
    std::shared_lock state_lock(view_state_mutex_);
    published_intent_ids.reserve(published_runtime_views_by_intent_.size());
    for (const auto& [intent_view_id, _] : published_runtime_views_by_intent_) {
      published_intent_ids.push_back(intent_view_id);
    }
  }
  for (const auto intent_view_id : published_intent_ids) {
    RemovePublishedRuntimeView(intent_view_id);
  }

  ResetPublicationState();
  scene_renderer_.reset();
  imgui_runtime_.reset();
  if (uploader_) {
    [[maybe_unused]] const auto result = uploader_->Shutdown();
  }
  view_const_manager_.reset();
  render_context_pool_.reset();
  gpu_timeline_profiler_.reset();
  compositing_pass_.reset();
  compositing_pass_config_.reset();
  inline_transfers_.reset();
  inline_staging_provider_.reset();
  upload_staging_provider_.reset();
  uploader_.reset();
  render_graphs_.clear();
  resolved_views_.clear();
  view_ready_states_.clear();
  published_runtime_views_by_intent_.clear();
  console_.reset();
  engine_.reset();
}

auto Renderer::EnsurePublicationState(Graphics& gfx)
  -> RendererPublicationState&
{
  if (!publication_state_) {
    publication_state_ = std::make_unique<RendererPublicationState>();
  }

  auto& state = *publication_state_;
  if (!state.scene_texture_bindings_publisher) {
    state.scene_texture_bindings_publisher = std::make_unique<
      internal::PerViewStructuredPublisher<SceneTextureBindings>>(
      observer_ptr { &gfx }, GetStagingProvider(),
      observer_ptr { &GetInlineTransfersCoordinator() },
      "SceneTextureBindings");
  }
  if (!state.draw_frame_bindings_publisher) {
    state.draw_frame_bindings_publisher = std::make_unique<
      internal::PerViewStructuredPublisher<DrawFrameBindings>>(
      observer_ptr { &gfx }, GetStagingProvider(),
      observer_ptr { &GetInlineTransfersCoordinator() }, "DrawFrameBindings");
  }
  if (!state.view_history_frame_bindings_publisher) {
    state.view_history_frame_bindings_publisher = std::make_unique<
      internal::PerViewStructuredPublisher<ViewHistoryFrameBindings>>(
      observer_ptr { &gfx }, GetStagingProvider(),
      observer_ptr { &GetInlineTransfersCoordinator() },
      "ViewHistoryFrameBindings");
  }
  if (!state.view_frame_bindings_publisher) {
    state.view_frame_bindings_publisher = std::make_unique<
      internal::PerViewStructuredPublisher<ViewFrameBindings>>(
      observer_ptr { &gfx }, GetStagingProvider(),
      observer_ptr { &GetInlineTransfersCoordinator() }, "ViewFrameBindings");
  }
  return state;
}

auto Renderer::BeginPublicationFrame(Graphics& gfx,
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  auto& state = EnsurePublicationState(gfx);
  if (state.prepared_frame_sequence == sequence
    && state.prepared_frame_slot == slot) {
    return;
  }

  state.scene_texture_bindings_publisher->OnFrameStart(sequence, slot);
  state.draw_frame_bindings_publisher->OnFrameStart(sequence, slot);
  state.view_history_frame_bindings_publisher->OnFrameStart(sequence, slot);
  state.view_frame_bindings_publisher->OnFrameStart(sequence, slot);
  state.prepared_frame_sequence = sequence;
  state.prepared_frame_slot = slot;
}

auto Renderer::RefreshCurrentViewFrameBindings(
  RenderContext& render_context, SceneRenderer& scene_renderer) -> void
{
  if (render_context.current_view.view_id == kInvalidViewId) {
    return;
  }
  auto gfx = GetGraphics();
  if (gfx == nullptr) {
    return;
  }

  BeginPublicationFrame(
    *gfx, render_context.frame_sequence, render_context.frame_slot);
  auto& publication_state = EnsurePublicationState(*gfx);

  auto view_bindings = scene_renderer.GetPublishedViewFrameBindings();
  if (render_context.current_view.resolved_view != nullptr) {
    view_bindings.history_frame_slot
      = EnsurePublicationState(*gfx)
          .view_history_frame_bindings_publisher->Publish(
            render_context.current_view.view_id,
            BuildViewHistoryFrameBindings(render_context.current_view.view_id,
              *render_context.current_view.resolved_view,
              observer_ptr<const scene::Scene> { render_context.scene.get() }));
  }
  const auto scene_texture_frame_slot
    = publication_state.scene_texture_bindings_publisher->Publish(
      render_context.current_view.view_id,
      scene_renderer.GetSceneTextureBindings());
  view_bindings.scene_texture_frame_slot = scene_texture_frame_slot;

  const auto view_frame_bindings_slot
    = publication_state.view_frame_bindings_publisher->Publish(
      render_context.current_view.view_id, view_bindings);
  scene_renderer.PublishViewFrameBindings(render_context.current_view.view_id,
    view_bindings, view_frame_bindings_slot);

  EnsureViewConstantsManager(*gfx);
  view_const_manager_->OnFrameStart(render_context.frame_slot);
  if (render_context.current_view.resolved_view != nullptr) {
    UpdateViewConstantsFromView(*render_context.current_view.resolved_view);
  }
  view_const_cpu_
    .SetTimeSeconds(last_frame_dt_seconds_, ViewConstants::kRenderer)
    .SetFrameSlot(render_context.frame_slot, ViewConstants::kRenderer)
    .SetFrameSequenceNumber(
      render_context.frame_sequence, ViewConstants::kRenderer)
    .SetBindlessViewFrameBindingsSlot(
      BindlessViewFrameBindingsSlot { view_frame_bindings_slot },
      ViewConstants::kRenderer);

  const auto snapshot = view_const_cpu_.GetSnapshot();
  const auto buffer_info = view_const_manager_->WriteViewConstants(
    render_context.current_view.view_id, &snapshot, sizeof(snapshot));
  render_context.view_constants = buffer_info.buffer;
}

auto Renderer::ResetPublicationState() -> void { publication_state_.reset(); }

auto Renderer::SetShaderDebugMode(const ShaderDebugMode mode) noexcept -> void
{
  shader_debug_mode_.store(
    static_cast<std::uint8_t>(mode), std::memory_order_relaxed);
}

auto Renderer::GetShaderDebugMode() const noexcept -> ShaderDebugMode
{
  return static_cast<ShaderDebugMode>(
    shader_debug_mode_.load(std::memory_order_relaxed));
}

auto Renderer::OnFrameStart(observer_ptr<engine::FrameContext> context) -> void
{
  profiling::CpuProfileScope frame_scope(
    "Vortex.OnFrameStart", profiling::ProfileCategory::kPass);

  resolved_views_.clear();
  {
    std::scoped_lock lock(composition_mutex_);
    pending_compositions_.clear();
    next_composition_sequence_in_frame_ = 0;
  }
  {
    std::unique_lock lock(view_state_mutex_);
    view_ready_states_.clear();
  }

  if (context == nullptr) {
    return;
  }

  if (auto* imgui_runtime = EnsureImGuiRuntime(); imgui_runtime != nullptr) {
    imgui_runtime->OnFrameStart();
  }

  frame_slot_ = context->GetFrameSlot();
  frame_seq_num_ = context->GetFrameSequenceNumber().get();
  rigid_transform_history_cache_.BeginFrame(frame_seq_num_);
  deformation_history_cache_.BeginFrame(
    frame_seq_num_, observer_ptr<const scene::Scene> { context->GetScene().get() });
  previous_view_history_cache_.BeginFrame(
    frame_seq_num_, observer_ptr<const scene::Scene> { context->GetScene().get() });
  const auto dt = context->GetModuleTimingData().game_delta_time;
  const auto dt_seconds
    = std::chrono::duration_cast<std::chrono::duration<float>>(dt.get())
        .count();
  CHECK_GT_F(dt_seconds, 0.0F, "Frame delta time must be positive");
  last_frame_dt_seconds_ = dt_seconds;
  if (gpu_timeline_profiler_) {
    gpu_timeline_profiler_->OnFrameStart(context->GetFrameSequenceNumber());
  }

  const auto tag = internal::RendererTagFactory::Get();
  if (uploader_) {
    uploader_->OnFrameStart(tag, frame_slot_);
  }
  if (inline_transfers_) {
    inline_transfers_->OnFrameStart(tag, frame_slot_);
  }
  if (view_const_manager_) {
    view_const_manager_->OnFrameStart(frame_slot_);
  }

  if (scene_renderer_) {
    scene_renderer_->OnFrameStart(*context);
    scene_renderer_started_frame_ = context->GetFrameSequenceNumber();
  }
}

auto Renderer::OnTransformPropagation(
  observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  if (context != nullptr) {
    if (auto scene = context->GetScene()) {
      scene->Update();
    }
  }
  co_return;
}

auto Renderer::OnPreRender(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  if (context != nullptr) {
    co_await DispatchSceneRendererPreRender(context);
  }
  co_return;
}

auto Renderer::OnRender(observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  if (context != nullptr) {
    co_await DispatchSceneRendererRender(context);
  }
  co_return;
}

auto Renderer::OnCompositing(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  const auto finalize_gpu_timestamps = oxygen::ScopeGuard {
    [this]() noexcept -> void {
      if (gpu_timeline_profiler_) {
        gpu_timeline_profiler_->OnFrameRecordTailResolve();
      }
    }
  };

  if (context != nullptr) {
    co_await DispatchSceneRendererCompositing(context);
  }

  if (context != nullptr && imgui_runtime_ != nullptr
    && imgui_runtime_->IsFrameActive()) {
    std::shared_ptr<graphics::Framebuffer> composite_target {};
    std::shared_ptr<graphics::Surface> target_surface {};
    {
      std::scoped_lock lock(composition_mutex_);
      if (!pending_compositions_.empty()) {
        composite_target = pending_compositions_.front().submission.composite_target;
        target_surface = pending_compositions_.front().target_surface;
      }
    }

    if (composite_target && target_surface) {
      if (const auto overlay
          = imgui_runtime_->RenderOverlay(
              *this, observer_ptr { composite_target.get() });
        overlay.has_value()) {
        auto submission = CompositionSubmission {};
        submission.composite_target = composite_target;
        submission.tasks.push_back(CompositingTask::MakeTextureBlend(
          overlay->texture, overlay->viewport, 1.0F));
        RegisterComposition(std::move(submission), std::move(target_surface));
      }
    }
  }

  if (context == nullptr) {
    std::scoped_lock lock(composition_mutex_);
    pending_compositions_.clear();
    co_return;
  }

  std::vector<PendingComposition> submissions;
  {
    std::scoped_lock lock(composition_mutex_);
    if (pending_compositions_.empty()) {
      co_return;
    }
    submissions = std::move(pending_compositions_);
    pending_compositions_.clear();
  }

  const auto& anchor = submissions.front();
  for (const auto& pending : submissions) {
    const bool same_composite_target = anchor.submission.composite_target.get()
      == pending.submission.composite_target.get();
    const bool same_target_surface
      = anchor.target_surface.get() == pending.target_surface.get();
    CHECK_F(same_composite_target && same_target_surface,
      "Phase-1 composition queue requires a single target per frame");
  }

  std::ranges::stable_sort(submissions,
    [](const PendingComposition& lhs, const PendingComposition& rhs) -> bool {
      return lhs.sequence_in_frame < rhs.sequence_in_frame;
    });

  auto gfx = GetGraphics();
  CHECK_F(static_cast<bool>(gfx), "Graphics required for compositing");
  auto& scene_renderer = EnsureSceneRenderer();

  if (!compositing_pass_) {
    compositing_pass_config_
      = std::make_shared<internal::CompositingPassConfig>();
    compositing_pass_config_->debug_name = "VortexCompositingPass";
    compositing_pass_
      = std::make_shared<internal::CompositingPass>(compositing_pass_config_);
  }

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  for (const auto& pending : submissions) {
    const auto& payload = pending.submission;
    if (payload.tasks.empty()) {
      continue;
    }

    CHECK_F(static_cast<bool>(payload.composite_target),
      "Compositing requires a target framebuffer");

    auto recorder_ptr
      = gfx->AcquireCommandRecorder(queue_key, "Vortex Renderer Compositing");
    CHECK_F(static_cast<bool>(recorder_ptr),
      "Compositing recorder acquisition failed");
    if (gpu_timeline_profiler_) {
      recorder_ptr->SetTelemetryCollector(
        observer_ptr<graphics::IGpuProfileCollector>(
          gpu_timeline_profiler_.get()));
    }

    auto& recorder = *recorder_ptr;
    auto& target_fb = *payload.composite_target;
    TrackCompositionFramebuffer(recorder, target_fb);

    const auto& fb_desc = target_fb.GetDescriptor();
    CHECK_F(!fb_desc.color_attachments.empty(),
      "Compositing requires a color attachment");
    CHECK_F(static_cast<bool>(fb_desc.color_attachments[0].texture),
      "Compositing target missing color texture");
    auto& backbuffer = *fb_desc.color_attachments[0].texture;

    RenderContext comp_context {};
    comp_context.SetRenderer(this, gfx.get());
    comp_context.pass_target = observer_ptr { payload.composite_target.get() };
    comp_context.frame_slot = frame_slot_;
    comp_context.frame_sequence = frame::SequenceNumber { frame_seq_num_ };

    const auto resolve_composition_source
      = [&](const ViewId view_id) -> std::shared_ptr<graphics::Texture> {
      if (view_id == scene_renderer.GetPublishedViewId()) {
        const auto& resolved_scene_color
          = scene_renderer.GetResolvedSceneColorTexture();
        if (resolved_scene_color) {
          DLOG_F(1,
            "Vortex compositing source for view {}: '{}' (resolved scene-color "
            "artifact)",
            view_id.get(), resolved_scene_color->GetDescriptor().debug_name);
          return resolved_scene_color;
        }
        auto fallback = ResolveViewOutputTexture(*context, view_id);
        if (fallback) {
          LOG_F(WARNING,
            "Vortex compositing source for published view {} is missing the "
            "resolved scene-color artifact; falling back to published "
            "composite source '{}'",
            view_id.get(), fallback->GetDescriptor().debug_name);
          return fallback;
        }
        LOG_F(ERROR,
          "Vortex compositing source for published view {} is missing both "
          "the resolved scene-color artifact and the published composite "
          "source",
          view_id.get());
        return {};
      }
      auto source = ResolveViewOutputTexture(*context, view_id);
      if (source) {
        DLOG_F(1,
          "Vortex compositing source for view {}: '{}' (published composite "
          "source)",
          view_id.get(), source->GetDescriptor().debug_name);
      }
      return source;
    };

    for (const auto& task : payload.tasks) {
      graphics::GpuEventScope task_scope(recorder, "Vortex.CompositingTask",
        profiling::ProfileGranularity::kTelemetry,
        profiling::ProfileCategory::kPass,
        profiling::Vars(
          profiling::Var("label", FormatCompositingTaskScopeLabel(task))));

      switch (task.type) {
      case CompositingTaskType::kCopy: {
        auto source = resolve_composition_source(task.copy.source_view_id);
        if (!source) {
          continue;
        }

        TrackCompositionSourceTexture(
          gfx->GetResourceRegistry(), recorder, *source);
        if (source->GetDescriptor().format
          != backbuffer.GetDescriptor().format) {
          compositing_pass_config_->source_texture = source;
          compositing_pass_config_->viewport = task.copy.viewport;
          compositing_pass_config_->alpha = 1.0F;
          co_await compositing_pass_->PrepareResources(comp_context, recorder);
          co_await compositing_pass_->Execute(comp_context, recorder);
          break;
        }

        CopyTextureToRegion(recorder, *source, backbuffer, task.copy.viewport);
        break;
      }
      case CompositingTaskType::kBlend: {
        auto source = resolve_composition_source(task.blend.source_view_id);
        if (!source) {
          continue;
        }

        TrackCompositionSourceTexture(
          gfx->GetResourceRegistry(), recorder, *source);
        compositing_pass_config_->source_texture = source;
        compositing_pass_config_->viewport = task.blend.viewport;
        compositing_pass_config_->alpha = task.blend.alpha;
        co_await compositing_pass_->PrepareResources(comp_context, recorder);
        co_await compositing_pass_->Execute(comp_context, recorder);
        break;
      }
      case CompositingTaskType::kBlendTexture: {
        if (!task.texture_blend.source_texture) {
          continue;
        }

        TrackCompositionSourceTexture(gfx->GetResourceRegistry(), recorder,
          *task.texture_blend.source_texture);
        compositing_pass_config_->source_texture
          = task.texture_blend.source_texture;
        compositing_pass_config_->viewport = task.texture_blend.viewport;
        compositing_pass_config_->alpha = task.texture_blend.alpha;
        co_await compositing_pass_->PrepareResources(comp_context, recorder);
        co_await compositing_pass_->Execute(comp_context, recorder);
        break;
      }
      case CompositingTaskType::kTaa:
      default:
        DLOG_F(1, "Skip compositing: task type not implemented");
        break;
      }
    }

    recorder.RequireResourceStateFinal(
      backbuffer, graphics::ResourceStates::kPresent);
    recorder.FlushBarriers();

    if (pending.target_surface) {
      const auto surfaces = context->GetSurfaces();
      for (size_t i = 0; i < surfaces.size(); ++i) {
        if (surfaces[i].get() == pending.target_surface.get()) {
          context->SetSurfacePresentable(i, true);
          break;
        }
      }
    }
  }

  co_return;
}

auto Renderer::OnFrameEnd(observer_ptr<engine::FrameContext> context) -> void
{
  if (imgui_runtime_ != nullptr) {
    imgui_runtime_->OnFrameEnd();
  }
  if (context != nullptr && scene_renderer_ != nullptr
    && scene_renderer_started_frame_ == context->GetFrameSequenceNumber()) {
    scene_renderer_->OnFrameEnd(*context);
    scene_renderer_started_frame_ = frame::SequenceNumber {};
  }
  rigid_transform_history_cache_.EndFrame();
  deformation_history_cache_.EndFrame();
  previous_view_history_cache_.EndFrame();
  resolved_views_.clear();
}

auto Renderer::GetRuntimeMotionProducerModule() const noexcept
  -> observer_ptr<scenesync::RuntimeMotionProducerModule>
{
  if (engine_ == nullptr) {
    return nullptr;
  }
  if (const auto module = engine_->GetModule<scenesync::RuntimeMotionProducerModule>();
    module.has_value()) {
    return observer_ptr<scenesync::RuntimeMotionProducerModule> {
      &module->get()
    };
  }
  return nullptr;
}

auto Renderer::RegisterViewRenderGraph(
  ViewId view_id, RenderGraphFactory factory, ResolvedView view) -> void
{
  std::unique_lock lock(view_registration_mutex_);
  render_graphs_.insert_or_assign(view_id, std::move(factory));
  resolved_views_.insert_or_assign(view_id, view);
}

auto Renderer::RegisterResolvedView(ViewId view_id, ResolvedView view) -> void
{
  std::unique_lock lock(view_registration_mutex_);
  resolved_views_.insert_or_assign(view_id, std::move(view));
}

auto Renderer::PublishRuntimeCompositionView(
  engine::FrameContext& frame_context, const RuntimeViewPublishInput& input,
  const std::optional<ShadingMode> shading_mode_override) -> ViewId
{
  const auto& composition_view = input.composition_view;
  CHECK_F(composition_view.id != kInvalidViewId,
    "Renderer::PublishRuntimeCompositionView requires a valid intent view id");

  auto render_target = input.render_target;
  auto composite_source = input.composite_source;
  if (render_target == nullptr) {
    render_target = composite_source;
  }
  if (composite_source == nullptr) {
    composite_source = render_target;
  }

  CHECK_NOTNULL_F(render_target.get(),
    "Renderer::PublishRuntimeCompositionView requires a render_target or "
    "composite_source framebuffer");
  CHECK_NOTNULL_F(composite_source.get(),
    "Renderer::PublishRuntimeCompositionView requires a composite_source or "
    "render_target framebuffer");

  engine::ViewContext view_context {};
  view_context.view = composition_view.view;
  view_context.metadata = {
    .name = std::string(composition_view.name),
    .purpose = composition_view.camera.has_value() ? "scene" : "overlay",
    .is_scene_view = composition_view.camera.has_value(),
    .with_atmosphere = composition_view.with_atmosphere,
    .exposure_view_id = composition_view.exposure_source_view_id,
  };
  view_context.render_target = render_target;
  view_context.composite_source = composite_source;

  const auto published_view_id = UpsertPublishedRuntimeView(frame_context,
    composition_view.id, std::move(view_context),
    shading_mode_override.has_value() ? shading_mode_override
                                      : composition_view.shading_mode);

  if (composition_view.camera.has_value()) {
    auto camera_node = composition_view.camera.value();
    auto resolver = SceneCameraViewResolver {
      [camera_node](const ViewId& /*view_id*/) { return camera_node; },
      composition_view.view.viewport,
    };
    RegisterResolvedView(published_view_id, resolver(published_view_id));
  }

  return published_view_id;
}

auto Renderer::UpsertPublishedRuntimeView(engine::FrameContext& frame_context,
  const ViewId intent_view_id, engine::ViewContext view,
  const std::optional<ShadingMode> shading_mode_override) -> ViewId
{
  CHECK_F(intent_view_id != kInvalidViewId,
    "Renderer::UpsertPublishedRuntimeView requires a valid intent view id");

  std::unique_lock state_lock(view_state_mutex_);
  if (const auto it = published_runtime_views_by_intent_.find(intent_view_id);
    it != published_runtime_views_by_intent_.end()) {
    frame_context.UpdateView(it->second.published_view_id, std::move(view));
    it->second.last_seen_frame = frame_context.GetFrameSequenceNumber();
    it->second.shading_mode_override = shading_mode_override;
    return it->second.published_view_id;
  }

  const auto published_view_id = frame_context.RegisterView(std::move(view));
  published_runtime_views_by_intent_[intent_view_id]
    = PublishedRuntimeViewState {
        .published_view_id = published_view_id,
        .last_seen_frame = frame_context.GetFrameSequenceNumber(),
        .shading_mode_override = shading_mode_override,
      };
  return published_view_id;
}

auto Renderer::ResolvePublishedRuntimeViewId(
  const ViewId intent_view_id) const noexcept -> ViewId
{
  if (intent_view_id == kInvalidViewId) {
    return kInvalidViewId;
  }

  std::shared_lock state_lock(view_state_mutex_);
  if (const auto it = published_runtime_views_by_intent_.find(intent_view_id);
    it != published_runtime_views_by_intent_.end()) {
    return it->second.published_view_id;
  }
  return kInvalidViewId;
}

auto Renderer::DetachPublishedRuntimeViewState(const ViewId intent_view_id)
  -> ViewId
{
  if (intent_view_id == kInvalidViewId) {
    return kInvalidViewId;
  }

  std::unique_lock state_lock(view_state_mutex_);
  if (const auto it = published_runtime_views_by_intent_.find(intent_view_id);
    it != published_runtime_views_by_intent_.end()) {
    const auto published_view_id = it->second.published_view_id;
    published_runtime_views_by_intent_.erase(it);
    return published_view_id;
  }

  return kInvalidViewId;
}

auto Renderer::RemovePublishedRuntimeView(const ViewId intent_view_id) -> void
{
  const auto published_view_id = DetachPublishedRuntimeViewState(intent_view_id);
  if (published_view_id == kInvalidViewId) {
    return;
  }

  UnregisterViewRenderGraph(published_view_id);
  if (view_const_manager_) {
    view_const_manager_->RemoveView(published_view_id);
  }
}

auto Renderer::RemovePublishedRuntimeView(
  engine::FrameContext& frame_context, const ViewId intent_view_id) -> void
{
  const auto published_view_id = DetachPublishedRuntimeViewState(intent_view_id);

  if (published_view_id == kInvalidViewId) {
    return;
  }

  frame_context.RemoveView(published_view_id);
  UnregisterViewRenderGraph(published_view_id);
  if (view_const_manager_) {
    view_const_manager_->RemoveView(published_view_id);
  }
}

auto Renderer::PruneStalePublishedRuntimeViews(
  engine::FrameContext& frame_context) -> std::vector<ViewId>
{
  const auto current_frame = frame_context.GetFrameSequenceNumber();
  auto stale_intent_ids = std::vector<ViewId> {};
  auto stale_published_ids = std::vector<ViewId> {};

  {
    std::unique_lock state_lock(view_state_mutex_);
    for (auto it = published_runtime_views_by_intent_.begin();
      it != published_runtime_views_by_intent_.end();) {
      if (current_frame - it->second.last_seen_frame
        > kPublishedRuntimeViewMaxIdleFrames) {
        stale_intent_ids.push_back(it->first);
        stale_published_ids.push_back(it->second.published_view_id);
        it = published_runtime_views_by_intent_.erase(it);
      } else {
        ++it;
      }
    }
  }

  for (const auto published_view_id : stale_published_ids) {
    frame_context.RemoveView(published_view_id);
    UnregisterViewRenderGraph(published_view_id);
    if (view_const_manager_) {
      view_const_manager_->RemoveView(published_view_id);
    }
  }

  return stale_intent_ids;
}

auto Renderer::UnregisterViewRenderGraph(ViewId view_id) -> void
{
  std::unique_lock lock(view_registration_mutex_);
  render_graphs_.erase(view_id);
  resolved_views_.erase(view_id);
}

auto Renderer::RegisterRuntimeComposition(
  const RuntimeCompositionInput& input) -> void
{
  if (input.composite_target == nullptr || input.target_surface == nullptr
    || input.layers.empty()) {
    return;
  }

  auto submission = CompositionSubmission {};
  submission.composite_target = input.composite_target;
  submission.tasks.reserve(input.layers.size());

  for (const auto& layer : input.layers) {
    if (layer.intent_view_id == kInvalidViewId || layer.opacity <= 0.0F) {
      continue;
    }

    const auto published_view_id
      = ResolvePublishedRuntimeViewId(layer.intent_view_id);
    CHECK_F(published_view_id != kInvalidViewId,
      "Renderer::RegisterRuntimeComposition requires published runtime view "
      "state for intent id {}",
      layer.intent_view_id.get());

    if (layer.opacity >= 1.0F) {
      submission.tasks.push_back(
        CompositingTask::MakeCopy(published_view_id, layer.viewport));
    } else {
      submission.tasks.push_back(CompositingTask::MakeBlend(
        published_view_id, layer.viewport, layer.opacity));
    }
  }

  if (submission.tasks.empty()) {
    return;
  }

  RegisterComposition(std::move(submission), input.target_surface);
}

auto Renderer::RegisterComposition(CompositionSubmission submission,
  std::shared_ptr<graphics::Surface> target_surface) -> void
{
  std::scoped_lock lock(composition_mutex_);
  if (!pending_compositions_.empty()) {
    const auto& anchor = pending_compositions_.front();
    const bool same_composite_target = anchor.submission.composite_target.get()
      == submission.composite_target.get();
    const bool same_target_surface
      = anchor.target_surface.get() == target_surface.get();
    CHECK_F(same_composite_target && same_target_surface,
      "Phase-1 composition queue requires a single target per frame");
  }
  pending_compositions_.push_back(PendingComposition {
    .submission = std::move(submission),
    .target_surface = std::move(target_surface),
    .sequence_in_frame = next_composition_sequence_in_frame_++,
  });
}

auto Renderer::ForSinglePassHarness() -> SinglePassHarnessFacade
{
  return SinglePassHarnessFacade(*this);
}

auto Renderer::ForRenderGraphHarness() -> RenderGraphHarnessFacade
{
  return RenderGraphHarnessFacade(*this);
}

auto Renderer::ForOffscreenScene() -> OffscreenSceneFacade
{
  return OffscreenSceneFacade(*this);
}

auto Renderer::GetStats() const noexcept -> Stats { return {}; }

auto Renderer::ResetStats() noexcept -> void { }

auto Renderer::IsViewReady(const ViewId view_id) const -> bool
{
  std::shared_lock lock(view_state_mutex_);
  const auto it = view_ready_states_.find(view_id);
  return it != view_ready_states_.end() && it->second;
}

auto Renderer::SetImGuiWindowId(const platform::WindowIdType window_id) -> void
{
  if (auto* imgui_runtime = EnsureImGuiRuntime(); imgui_runtime != nullptr) {
    imgui_runtime->SetWindowId(window_id);
  }
}

auto Renderer::GetImGuiContext() noexcept -> ImGuiContext*
{
  if (auto* imgui_runtime = EnsureImGuiRuntime(); imgui_runtime != nullptr) {
    return imgui_runtime->GetImGuiContext();
  }
  return nullptr;
}

auto Renderer::IsImGuiFrameActive() const noexcept -> bool
{
  return imgui_runtime_ != nullptr && imgui_runtime_->IsFrameActive();
}

auto Renderer::GetGraphics() -> std::shared_ptr<Graphics>
{
  return gfx_weak_.lock();
}

auto Renderer::GetStagingProvider() -> upload::StagingProvider&
{
  CHECK_NOTNULL_F(
    upload_staging_provider_.get(), "Renderer staging provider is unavailable");
  return *upload_staging_provider_;
}

auto Renderer::GetInlineTransfersCoordinator()
  -> upload::InlineTransfersCoordinator&
{
  CHECK_NOTNULL_F(inline_transfers_.get(),
    "Renderer inline transfers coordinator is unavailable");
  return *inline_transfers_;
}

auto Renderer::GetUploadCoordinator() -> upload::UploadCoordinator&
{
  CHECK_NOTNULL_F(uploader_.get(), "Renderer upload coordinator is unavailable");
  return *uploader_;
}

auto Renderer::GetAssetLoader() const noexcept
  -> observer_ptr<content::IAssetLoader>
{
  return engine_ != nullptr ? engine_->GetAssetLoader() : nullptr;
}

auto Renderer::EnsureViewConstantsManager(Graphics& gfx) -> void
{
  if (!view_const_manager_) {
    view_const_manager_
      = std::make_unique<internal::ViewConstantsManager>(observer_ptr { &gfx },
        static_cast<std::uint32_t>(sizeof(ViewConstants::GpuData)));
  }
}

auto Renderer::PopulateRenderContextViewState(RenderContext& render_context,
  engine::FrameContext& context, const bool prefer_composite_source) const
  -> void
{
  // Renderer Core owns the canonical published-view set and the current
  // scene-view cursor selection. SceneRenderer consumes the selected current
  // view; it does not own the outer view-selection loop.
  render_context.current_view = {};
  render_context.frame_views.clear();
  render_context.active_view_index = std::numeric_limits<std::size_t>::max();
  render_context.pass_target.reset(nullptr);

  auto fallback_index = std::optional<std::size_t> {};
  auto selected_scene_index = std::optional<std::size_t> {};

  for (const auto& view_ref : context.GetViews()) {
    const auto& view = view_ref.get();
    if (view.render_target != nullptr) {
      render_context.view_outputs.insert_or_assign(view.id, view.render_target);
    } else if (view.composite_source != nullptr) {
      render_context.view_outputs.insert_or_assign(view.id,
        observer_ptr<graphics::Framebuffer> { view.composite_source.get() });
    }

    auto primary_target = observer_ptr<graphics::Framebuffer> {};
    const auto should_use_composite_source = view.composite_source != nullptr
      && (prefer_composite_source || view.render_target == nullptr);
    if (should_use_composite_source) {
      primary_target.reset(view.composite_source.get());
    } else if (view.render_target != nullptr) {
      primary_target = view.render_target;
    }

    if (primary_target == nullptr) {
      continue;
    }

    auto entry = RenderContext::ViewExecutionEntry {
      .view_id = view.id,
      .is_scene_view = view.metadata.is_scene_view,
      .composition_view = {},
      .shading_mode_override = ResolvePublishedRuntimeShadingMode(view.id),
      .resolved_view = {},
      .primary_target = primary_target,
    };
    if (const auto it = resolved_views_.find(view.id);
      it != resolved_views_.end()) {
      entry.resolved_view = observer_ptr<const ResolvedView> { &it->second };
    }

    render_context.frame_views.push_back(entry);
    const auto entry_index = render_context.frame_views.size() - 1;
    if (!fallback_index.has_value()) {
      fallback_index = entry_index;
    }
    if (!selected_scene_index.has_value() && view.metadata.is_scene_view) {
      selected_scene_index = entry_index;
    }
  }

  const auto active_index
    = selected_scene_index.has_value() ? selected_scene_index : fallback_index;
  if (!active_index.has_value()) {
    return;
  }

  render_context.active_view_index = *active_index;
  const auto& selection = render_context.frame_views[*active_index];
  const auto& selected_view = context.GetViewContext(selection.view_id);
  render_context.current_view.view_id = selection.view_id;
  render_context.current_view.exposure_view_id
    = selected_view.metadata.exposure_view_id != kInvalidViewId
    ? selected_view.metadata.exposure_view_id
    : selection.view_id;
  render_context.current_view.composition_view = selection.composition_view;
  render_context.current_view.shading_mode_override
    = selection.shading_mode_override;
  render_context.current_view.resolved_view = selection.resolved_view;
  render_context.pass_target = selection.primary_target;
}

auto Renderer::ResolvePublishedRuntimeShadingMode(
  const ViewId published_view_id) const noexcept -> std::optional<ShadingMode>
{
  if (published_view_id == kInvalidViewId) {
    return std::nullopt;
  }

  std::shared_lock state_lock(view_state_mutex_);
  for (const auto& [_, state] : published_runtime_views_by_intent_) {
    if (state.published_view_id == published_view_id) {
      return state.shading_mode_override;
    }
  }
  return std::nullopt;
}

auto Renderer::EnsureSceneRenderer(const CompositionView* composition_view)
  -> SceneRenderer&
{
  if (scene_renderer_ == nullptr) {
    auto gfx = GetGraphics();
    CHECK_F(gfx != nullptr, "Renderer requires a live Graphics backend");
    scene_renderer_ = SceneRenderBuilder::Build(*this, *gfx,
      capability_families_, ResolveBootstrapExtent(composition_view));
  }

  return *scene_renderer_;
}

auto Renderer::EnsureSceneRendererFrameStarted(engine::FrameContext& context)
  -> void
{
  if (scene_renderer_ == nullptr
    || scene_renderer_started_frame_ == context.GetFrameSequenceNumber()) {
    return;
  }

  scene_renderer_->OnFrameStart(context);
  scene_renderer_started_frame_ = context.GetFrameSequenceNumber();
}

auto Renderer::ReleasePooledRenderContext(const frame::Slot slot) noexcept
  -> void
{
  try {
    if (render_context_pool_ != nullptr && slot != frame::kInvalidSlot
      && render_context_pool_->IsInUse(slot)) {
      render_context_pool_->Release(slot);
    }
  } catch (const std::exception& ex) {
    LOG_F(
      ERROR, "Renderer failed to release pooled render context: {}", ex.what());
  }
}

auto Renderer::UpdateViewConstantsFromView(const ResolvedView& view) -> void
{
  view_const_cpu_.SetViewMatrix(view.ViewMatrix())
    .SetProjectionMatrix(view.ProjectionMatrix())
    .SetStableProjectionMatrix(view.StableProjectionMatrix())
    .SetCameraPosition(view.CameraPosition());
}

auto Renderer::BuildViewHistoryFrameBindings(const ViewId view_id,
  const ResolvedView& view, const observer_ptr<const scene::Scene> scene)
  -> ViewHistoryFrameBindings
{
  static_cast<void>(scene);
  const auto snapshot = previous_view_history_cache_.TouchCurrent(view_id,
    internal::PreviousViewHistoryCache::CurrentState {
      .view_matrix = view.ViewMatrix(),
      .projection_matrix = view.ProjectionMatrix(),
      .stable_projection_matrix = view.StableProjectionMatrix(),
      .inverse_view_projection_matrix = view.InverseViewProjection(),
      .pixel_jitter = view.PixelJitter(),
      .viewport = view.Viewport(),
    });

  auto history = ViewHistoryFrameBindings {
    .current_view_matrix = snapshot.current.view_matrix,
    .current_projection_matrix = snapshot.current.projection_matrix,
    .current_stable_projection_matrix
    = snapshot.current.stable_projection_matrix,
    .current_inverse_view_projection_matrix
    = snapshot.current.inverse_view_projection_matrix,
    .previous_view_matrix = snapshot.previous.view_matrix,
    .previous_projection_matrix = snapshot.previous.projection_matrix,
    .previous_stable_projection_matrix
    = snapshot.previous.stable_projection_matrix,
    .previous_inverse_view_projection_matrix
    = snapshot.previous.inverse_view_projection_matrix,
    .current_pixel_jitter = snapshot.current.pixel_jitter,
    .previous_pixel_jitter = snapshot.previous.pixel_jitter,
  };
  if (snapshot.previous_valid) {
    history.validity_flags |= static_cast<std::uint32_t>(
      ViewHistoryValidityFlagBits::kPreviousViewValid);
  }
  return history;
}

auto Renderer::WireContext(RenderContext& context,
  const std::shared_ptr<graphics::Buffer>& view_constants) -> void
{
  auto gfx = gfx_weak_.lock();
  CHECK_F(gfx != nullptr, "Renderer requires a live Graphics backend");
  context.SetRenderer(this, gfx.get());
  context.frame_slot = frame_slot_;
  context.frame_sequence = frame::SequenceNumber { frame_seq_num_ };
  context.delta_time = last_frame_dt_seconds_;
  context.view_constants = view_constants;
  context.shader_debug_mode = GetShaderDebugMode();
}

auto Renderer::BeginStandaloneFrameExecution(const FrameSessionInput& session)
  -> void
{
  CHECK_GT_F(session.delta_time_seconds, 0.0F, "Delta time must be positive");
  frame_slot_ = session.frame_slot;
  frame_seq_num_ = session.frame_sequence.get();
  last_frame_dt_seconds_ = session.delta_time_seconds;
  previous_view_history_cache_.BeginFrame(
    frame_seq_num_, observer_ptr<const scene::Scene> { session.scene.get() });

  const auto tag = internal::RendererTagFactory::Get();
  if (uploader_) {
    uploader_->OnFrameStart(tag, frame_slot_);
  }
  if (inline_transfers_) {
    inline_transfers_->OnFrameStart(tag, frame_slot_);
  }
  if (view_const_manager_) {
    view_const_manager_->OnFrameStart(frame_slot_);
  }
}

auto Renderer::InitializeStandaloneCurrentView(RenderContext& render_context,
  std::optional<ResolvedView>& current_resolved_view,
  std::optional<PreparedSceneFrame>& current_prepared_frame,
  const ViewId view_id, const ResolvedView& resolved_view,
  const PreparedSceneFrame& prepared_frame,
  const std::optional<ViewConstants>& view_constants_override) -> void
{
  current_resolved_view = resolved_view;
  current_prepared_frame = prepared_frame;
  render_context.current_view.view_id = view_id;
  render_context.current_view.exposure_view_id = view_id;
  render_context.current_view.resolved_view.reset(&*current_resolved_view);
  render_context.current_view.prepared_frame.reset(&*current_prepared_frame);

  auto gfx = gfx_weak_.lock();
  if (!gfx) {
    return;
  }

  EnsureViewConstantsManager(*gfx);
  view_const_manager_->OnFrameStart(frame_slot_);

  view_const_cpu_ = view_constants_override.value_or(ViewConstants {});
  if (!view_constants_override.has_value()) {
    UpdateViewConstantsFromView(resolved_view);
  }
  view_const_cpu_
    .SetTimeSeconds(last_frame_dt_seconds_, ViewConstants::kRenderer)
    .SetFrameSlot(frame_slot_, ViewConstants::kRenderer)
    .SetFrameSequenceNumber(
      frame::SequenceNumber { frame_seq_num_ }, ViewConstants::kRenderer);

  const auto snapshot = view_const_cpu_.GetSnapshot();
  const auto buffer_info = view_const_manager_->WriteViewConstants(
    view_id, &snapshot, sizeof(snapshot));
  render_context.view_constants = buffer_info.buffer;
}

auto Renderer::EndOffscreenFrame() noexcept -> void
{
  previous_view_history_cache_.EndFrame();
}

auto Renderer::DispatchSceneRendererPreRender(
  const observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  CHECK_NOTNULL_F(context.get());
  auto& scene_renderer = EnsureSceneRenderer();
  EnsureSceneRendererFrameStarted(*context);
  scene_renderer.OnPreRender(*context);
  co_return;
}

auto Renderer::DispatchSceneRendererRender(
  const observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  profiling::CpuProfileScope render_scope(
    "Vortex.DispatchSceneRendererRender", profiling::ProfileCategory::kPass);

  CHECK_NOTNULL_F(context.get());
  auto& scene_renderer = EnsureSceneRenderer();
  EnsureSceneRendererFrameStarted(*context);

  auto& render_context = render_context_pool_->Acquire(context->GetFrameSlot());
  const auto release_guard = oxygen::ScopeGuard {
    [this, slot = context->GetFrameSlot()]() noexcept -> void {
      ReleasePooledRenderContext(slot);
    }
  };

  WireContext(render_context, {});
  render_context.scene = context->GetScene();
  PopulateRenderContextViewState(render_context, *context, false);
  scene_renderer.PrimePreparedView(render_context);

  auto view_bindings = ViewFrameBindings {};
  if (render_context.current_view.view_id == kInvalidViewId) {
    scene_renderer.InvalidatePublishedViewFrameBindings();
    view_const_cpu_.SetBindlessViewFrameBindingsSlot(
      BindlessViewFrameBindingsSlot {}, ViewConstants::kRenderer);
  } else if (auto gfx = GetGraphics(); gfx != nullptr) {
    BeginPublicationFrame(
      *gfx, context->GetFrameSequenceNumber(), context->GetFrameSlot());
    auto& publication_state = EnsurePublicationState(*gfx);

    if (const auto prepared_frame = render_context.current_view.prepared_frame;
      prepared_frame != nullptr) {
      auto draw_bindings = DrawFrameBindings {
        .draw_metadata_slot = BindlessDrawMetadataSlot {
          prepared_frame->bindless_draw_metadata_slot },
        .current_worlds_slot = BindlessWorldsSlot {
          prepared_frame->bindless_worlds_slot },
        .previous_worlds_slot = BindlessWorldsSlot {
          prepared_frame->bindless_previous_worlds_slot },
        .normal_matrices_slot = BindlessNormalsSlot {
          prepared_frame->bindless_normals_slot },
        .material_shading_constants_slot
        = BindlessMaterialShadingConstantsSlot {
            prepared_frame->bindless_material_shading_slot },
        .instance_data_slot = BindlessInstanceDataSlot {
          prepared_frame->bindless_instance_data_slot },
        .current_skinned_pose_slot = BindlessSkinnedPosePublicationsSlot {
          prepared_frame->bindless_current_skinned_pose_slot },
        .previous_skinned_pose_slot = BindlessSkinnedPosePublicationsSlot {
          prepared_frame->bindless_previous_skinned_pose_slot },
        .current_morph_slot = BindlessMorphPublicationsSlot {
          prepared_frame->bindless_current_morph_slot },
        .previous_morph_slot = BindlessMorphPublicationsSlot {
          prepared_frame->bindless_previous_morph_slot },
        .current_material_wpo_slot = BindlessMaterialWpoPublicationsSlot {
          prepared_frame->bindless_current_material_wpo_slot },
        .previous_material_wpo_slot = BindlessMaterialWpoPublicationsSlot {
          prepared_frame->bindless_previous_material_wpo_slot },
        .current_motion_vector_status_slot
        = BindlessMotionVectorStatusPublicationsSlot {
            prepared_frame->bindless_current_motion_vector_status_slot },
        .previous_motion_vector_status_slot
        = BindlessMotionVectorStatusPublicationsSlot {
            prepared_frame->bindless_previous_motion_vector_status_slot },
        .velocity_draw_metadata_slot = BindlessVelocityDrawMetadataSlot {
          prepared_frame->bindless_velocity_draw_metadata_slot },
      };
      view_bindings.draw_frame_slot
        = publication_state.draw_frame_bindings_publisher->Publish(
          render_context.current_view.view_id, draw_bindings);
    }
    if (render_context.current_view.resolved_view != nullptr) {
      view_bindings.history_frame_slot
        = publication_state.view_history_frame_bindings_publisher->Publish(
          render_context.current_view.view_id,
          BuildViewHistoryFrameBindings(render_context.current_view.view_id,
            *render_context.current_view.resolved_view,
            observer_ptr<const scene::Scene> { render_context.scene.get() }));
    }

    const auto view_frame_bindings_slot
      = publication_state.view_frame_bindings_publisher->Publish(
        render_context.current_view.view_id, view_bindings);
    scene_renderer.PublishViewFrameBindings(render_context.current_view.view_id,
      view_bindings, view_frame_bindings_slot);

    EnsureViewConstantsManager(*gfx);
    view_const_manager_->OnFrameStart(frame_slot_);
    if (render_context.current_view.resolved_view != nullptr) {
      UpdateViewConstantsFromView(*render_context.current_view.resolved_view);
    }
    view_const_cpu_
      .SetTimeSeconds(last_frame_dt_seconds_, ViewConstants::kRenderer)
      .SetFrameSlot(frame_slot_, ViewConstants::kRenderer)
      .SetFrameSequenceNumber(
        frame::SequenceNumber { frame_seq_num_ }, ViewConstants::kRenderer)
      .SetBindlessViewFrameBindingsSlot(
        BindlessViewFrameBindingsSlot { view_frame_bindings_slot },
        ViewConstants::kRenderer);

    const auto snapshot = view_const_cpu_.GetSnapshot();
    const auto buffer_info = view_const_manager_->WriteViewConstants(
      render_context.current_view.view_id, &snapshot, sizeof(snapshot));
    render_context.view_constants = buffer_info.buffer;
  }

  scene_renderer.OnRender(render_context);

  if (render_context.current_view.view_id == kInvalidViewId) {
    scene_renderer.InvalidatePublishedViewFrameBindings();
    view_const_cpu_.SetBindlessViewFrameBindingsSlot(
      BindlessViewFrameBindingsSlot {}, ViewConstants::kRenderer);
  } else if (auto gfx = GetGraphics(); gfx != nullptr) {
    auto& publication_state = EnsurePublicationState(*gfx);
    view_bindings = scene_renderer.GetPublishedViewFrameBindings();

    const auto scene_texture_frame_slot
      = publication_state.scene_texture_bindings_publisher->Publish(
        render_context.current_view.view_id,
        scene_renderer.GetSceneTextureBindings());
    view_bindings.scene_texture_frame_slot = scene_texture_frame_slot;
    if (render_context.current_view.resolved_view != nullptr) {
      view_bindings.history_frame_slot
        = publication_state.view_history_frame_bindings_publisher->Publish(
          render_context.current_view.view_id,
          BuildViewHistoryFrameBindings(render_context.current_view.view_id,
            *render_context.current_view.resolved_view,
            observer_ptr<const scene::Scene> { render_context.scene.get() }));
    }

    const auto view_frame_bindings_slot
      = publication_state.view_frame_bindings_publisher->Publish(
        render_context.current_view.view_id, view_bindings);
    scene_renderer.PublishViewFrameBindings(render_context.current_view.view_id,
      view_bindings, view_frame_bindings_slot);

    EnsureViewConstantsManager(*gfx);
    view_const_manager_->OnFrameStart(frame_slot_);
    if (render_context.current_view.resolved_view != nullptr) {
      UpdateViewConstantsFromView(*render_context.current_view.resolved_view);
    }
    view_const_cpu_
      .SetTimeSeconds(last_frame_dt_seconds_, ViewConstants::kRenderer)
      .SetFrameSlot(frame_slot_, ViewConstants::kRenderer)
      .SetFrameSequenceNumber(
        frame::SequenceNumber { frame_seq_num_ }, ViewConstants::kRenderer)
      .SetBindlessViewFrameBindingsSlot(
        BindlessViewFrameBindingsSlot { view_frame_bindings_slot },
        ViewConstants::kRenderer);

    const auto snapshot = view_const_cpu_.GetSnapshot();
    const auto buffer_info = view_const_manager_->WriteViewConstants(
      render_context.current_view.view_id, &snapshot, sizeof(snapshot));
    render_context.view_constants = buffer_info.buffer;
  }

  co_return;
}

auto Renderer::DispatchSceneRendererCompositing(
  const observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  profiling::CpuProfileScope compositing_scope(
    "Vortex.DispatchSceneRendererCompositing",
    profiling::ProfileCategory::kPass);

  CHECK_NOTNULL_F(context.get());
  auto& scene_renderer = EnsureSceneRenderer();
  EnsureSceneRendererFrameStarted(*context);

  auto& render_context = render_context_pool_->Acquire(context->GetFrameSlot());
  const auto release_guard = oxygen::ScopeGuard {
    [this, slot = context->GetFrameSlot()]() noexcept -> void {
      ReleasePooledRenderContext(slot);
    }
  };

  WireContext(render_context, {});
  render_context.scene = context->GetScene();
  PopulateRenderContextViewState(render_context, *context, true);
  scene_renderer.OnCompositing(render_context);
  co_return;
}

Renderer::ValidatedSinglePassHarnessContext::ValidatedSinglePassHarnessContext(
  Renderer& renderer, FrameSessionInput session, const ViewId view_id,
  const observer_ptr<const graphics::Framebuffer> pass_target,
  const ResolvedView& resolved_view, const PreparedSceneFrame& prepared_frame,
  std::optional<ViewConstants> core_shader_inputs)
  : renderer_(observer_ptr { &renderer })
  , active_(true)
{
  renderer.BeginStandaloneFrameExecution(session);
  try {
    renderer.EnsureSceneRenderer();
    renderer.WireContext(*render_context_, {});
    render_context_->scene = session.scene;
    render_context_->pass_target = pass_target;
    renderer.InitializeStandaloneCurrentView(*render_context_,
      current_resolved_view_, current_prepared_frame_, view_id, resolved_view,
      prepared_frame, core_shader_inputs);
  } catch (const std::exception&) {
    Release();
    throw;
  }
}

Renderer::ValidatedSinglePassHarnessContext::
  ~ValidatedSinglePassHarnessContext()
{
  Release();
}

Renderer::ValidatedSinglePassHarnessContext::ValidatedSinglePassHarnessContext(
  ValidatedSinglePassHarnessContext&& other) noexcept
  : renderer_(std::exchange(other.renderer_, nullptr))
  , render_context_(std::move(other.render_context_))
  , current_resolved_view_(other.current_resolved_view_)
  , current_prepared_frame_(other.current_prepared_frame_)
  , active_(std::exchange(other.active_, false))
{
  RebindCurrentViewPointers();
}

auto Renderer::ValidatedSinglePassHarnessContext::operator=(
  ValidatedSinglePassHarnessContext&& other) noexcept
  -> ValidatedSinglePassHarnessContext&
{
  if (this == &other) {
    return *this;
  }

  Release();
  renderer_ = std::exchange(other.renderer_, nullptr);
  render_context_ = std::move(other.render_context_);
  current_resolved_view_ = other.current_resolved_view_;
  current_prepared_frame_ = other.current_prepared_frame_;
  active_ = std::exchange(other.active_, false);
  RebindCurrentViewPointers();
  return *this;
}

auto Renderer::ValidatedSinglePassHarnessContext::
  RebindCurrentViewPointers() noexcept -> void
{
  render_context_->current_view.resolved_view
    = current_resolved_view_.has_value()
    ? observer_ptr<const ResolvedView> { &*current_resolved_view_ }
    : observer_ptr<const ResolvedView> {};
  render_context_->current_view.prepared_frame
    = current_prepared_frame_.has_value()
    ? observer_ptr<const PreparedSceneFrame> { &*current_prepared_frame_ }
    : observer_ptr<const PreparedSceneFrame> {};
}

auto Renderer::ValidatedSinglePassHarnessContext::Release() noexcept -> void
{
  if (!active_) {
    return;
  }
  if (renderer_ != nullptr) {
    renderer_->EndOffscreenFrame();
  }
  render_context_->current_view = {};
  current_resolved_view_.reset();
  current_prepared_frame_.reset();
  renderer_.reset();
  active_ = false;
}

Renderer::SinglePassHarnessFacade::SinglePassHarnessFacade(
  Renderer& renderer) noexcept
  : renderer_(observer_ptr { &renderer })
{
}

auto Renderer::SinglePassHarnessFacade::SetFrameSession(
  FrameSessionInput session) -> SinglePassHarnessFacade&
{
  frame_session_.emplace(session);
  return *this;
}

auto Renderer::SinglePassHarnessFacade::SetOutputTarget(
  OutputTargetInput target) -> SinglePassHarnessFacade&
{
  output_target_.emplace(target);
  return *this;
}

auto Renderer::SinglePassHarnessFacade::SetResolvedView(ResolvedViewInput view)
  -> SinglePassHarnessFacade&
{
  resolved_view_.emplace(view);
  return *this;
}

auto Renderer::SinglePassHarnessFacade::SetPreparedFrame(
  PreparedFrameInput frame) -> SinglePassHarnessFacade&
{
  prepared_frame_.emplace(frame);
  return *this;
}

auto Renderer::SinglePassHarnessFacade::SetCoreShaderInputs(
  CoreShaderInputsInput inputs) -> SinglePassHarnessFacade&
{
  core_shader_inputs_.emplace(inputs);
  return *this;
}

auto Renderer::SinglePassHarnessFacade::CanFinalize() const -> bool
{
  return frame_session_.has_value() && output_target_.has_value()
    && (resolved_view_.has_value() || core_shader_inputs_.has_value());
}

auto Renderer::SinglePassHarnessFacade::Validate() const -> ValidationReport
{
  auto materializer = internal::RenderContextMaterializer(*renderer_);
  return materializer.ValidateSinglePass(
    internal::RenderContextMaterializer::SinglePassHarnessStaging {
      .frame_session = frame_session_,
      .output_target = output_target_,
      .resolved_view = resolved_view_,
      .prepared_frame = prepared_frame_,
      .core_shader_inputs = core_shader_inputs_,
    });
}

auto Renderer::SinglePassHarnessFacade::Finalize()
  -> std::expected<ValidatedSinglePassHarnessContext, ValidationReport>
{
  auto materializer = internal::RenderContextMaterializer(*renderer_);
  return materializer.MaterializeSinglePass(
    internal::RenderContextMaterializer::SinglePassHarnessStaging {
      .frame_session = frame_session_,
      .output_target = output_target_,
      .resolved_view = resolved_view_,
      .prepared_frame = prepared_frame_,
      .core_shader_inputs = core_shader_inputs_,
    });
}

Renderer::RenderGraphHarnessFacade::RenderGraphHarnessFacade(
  Renderer& renderer) noexcept
  : renderer_(observer_ptr { &renderer })
{
}

auto Renderer::RenderGraphHarnessFacade::SetFrameSession(
  FrameSessionInput session) -> RenderGraphHarnessFacade&
{
  frame_session_.emplace(session);
  return *this;
}

auto Renderer::RenderGraphHarnessFacade::SetOutputTarget(
  OutputTargetInput target) -> RenderGraphHarnessFacade&
{
  output_target_.emplace(target);
  return *this;
}

auto Renderer::RenderGraphHarnessFacade::SetResolvedView(ResolvedViewInput view)
  -> RenderGraphHarnessFacade&
{
  resolved_view_.emplace(view);
  return *this;
}

auto Renderer::RenderGraphHarnessFacade::SetPreparedFrame(
  PreparedFrameInput frame) -> RenderGraphHarnessFacade&
{
  prepared_frame_.emplace(frame);
  return *this;
}

auto Renderer::RenderGraphHarnessFacade::SetCoreShaderInputs(
  CoreShaderInputsInput inputs) -> RenderGraphHarnessFacade&
{
  core_shader_inputs_.emplace(inputs);
  return *this;
}

auto Renderer::RenderGraphHarnessFacade::SetRenderGraph(
  RenderGraphHarnessInput graph) -> RenderGraphHarnessFacade&
{
  render_graph_ = std::move(graph);
  return *this;
}

auto Renderer::RenderGraphHarnessFacade::CanFinalize() const -> bool
{
  return frame_session_.has_value() && output_target_.has_value()
    && (resolved_view_.has_value() || core_shader_inputs_.has_value())
    && render_graph_.has_value();
}

auto Renderer::RenderGraphHarnessFacade::Validate() const -> ValidationReport
{
  auto materializer = internal::RenderContextMaterializer(*renderer_);
  auto report = materializer.ValidateSinglePass(
    internal::RenderContextMaterializer::SinglePassHarnessStaging {
      .frame_session = frame_session_,
      .output_target = output_target_,
      .resolved_view = resolved_view_,
      .prepared_frame = prepared_frame_,
      .core_shader_inputs = core_shader_inputs_,
    });

  if (!render_graph_.has_value()) {
    report.issues.push_back(ValidationIssue {
      .code = "render_graph.missing",
      .message = "Render-graph harness requires a caller-authored render graph",
    });
  }

  return report;
}

auto Renderer::RenderGraphHarnessFacade::Finalize()
  -> std::expected<ValidatedRenderGraphHarness, ValidationReport>
{
  auto report = Validate();
  if (!report.Ok()) {
    return std::unexpected<ValidationReport> { report };
  }

  auto materializer = internal::RenderContextMaterializer(*renderer_);
  auto materialized = materializer.MaterializeSinglePass(
    internal::RenderContextMaterializer::SinglePassHarnessStaging {
      .frame_session = frame_session_,
      .output_target = output_target_,
      .resolved_view = resolved_view_,
      .prepared_frame = prepared_frame_,
      .core_shader_inputs = core_shader_inputs_,
    });
  if (!materialized.has_value()) {
    return std::unexpected<ValidationReport> { materialized.error() };
  }

  CHECK_F(resolved_view_.has_value() || core_shader_inputs_.has_value(),
    "Render-graph harness requires either a resolved view or core shader "
    "inputs");
  const auto view_id = resolved_view_.has_value()
    ? resolved_view_->view_id
    : core_shader_inputs_.value().view_id;
  return ValidatedRenderGraphHarness(
    std::move(materialized.value()), *render_graph_, view_id);
}

Renderer::OffscreenSceneViewInput::OffscreenSceneViewInput()
{
  composition_view_
    = CompositionView::ForScene(kInvalidViewId, View {}, scene::SceneNode {});
  SyncName();
}

auto Renderer::OffscreenSceneViewInput::FromCamera(std::string name,
  const ViewId view_id, const View& view, const scene::SceneNode& camera)
  -> OffscreenSceneViewInput
{
  auto input = OffscreenSceneViewInput {};
  input.name_storage_ = std::move(name);
  input.composition_view_ = CompositionView::ForScene(view_id, view, camera);
  input.SyncName();
  return input;
}

auto Renderer::OffscreenSceneViewInput::SetWithAtmosphere(const bool enabled)
  -> OffscreenSceneViewInput&
{
  composition_view_.with_atmosphere = enabled;
  return *this;
}

auto Renderer::OffscreenSceneViewInput::SetClearColor(
  const graphics::Color& clear_color) -> OffscreenSceneViewInput&
{
  composition_view_.clear_color = clear_color;
  return *this;
}

auto Renderer::OffscreenSceneViewInput::SetForceWireframe(const bool enabled)
  -> OffscreenSceneViewInput&
{
  composition_view_.force_wireframe = enabled;
  return *this;
}

auto Renderer::OffscreenSceneViewInput::SetExposureSourceViewId(
  const ViewId view_id) -> OffscreenSceneViewInput&
{
  composition_view_.exposure_source_view_id = view_id;
  return *this;
}

auto Renderer::OffscreenSceneViewInput::SyncName() noexcept -> void
{
  composition_view_.name = name_storage_;
}

Renderer::ValidatedOffscreenSceneSession::ValidatedOffscreenSceneSession(
  Renderer& renderer, FrameSessionInput frame_session,
  SceneSourceInput scene_source, OffscreenSceneViewInput view_intent,
  OutputTargetInput output_target, OffscreenPipelineInput pipeline)
  : renderer_(observer_ptr { &renderer })
  , frame_session_(frame_session)
  , scene_source_(scene_source)
  , view_intent_(std::move(view_intent))
  , output_target_(output_target)
  , pipeline_(pipeline)
{
}

auto Renderer::ValidatedOffscreenSceneSession::Execute() -> co::Co<void>
{
  CHECK_NOTNULL_F(
    renderer_.get(), "ValidatedOffscreenSceneSession requires a live renderer");
  CHECK_NOTNULL_F(scene_source_.scene.get(),
    "ValidatedOffscreenSceneSession requires a live scene");
  CHECK_NOTNULL_F(output_target_.framebuffer.get(),
    "ValidatedOffscreenSceneSession requires an output target");
  static_cast<void>(renderer_->EnsureSceneRenderer(&view_intent_.ViewIntent()));
  co_return;
}

Renderer::OffscreenSceneFacade::OffscreenSceneFacade(
  Renderer& renderer) noexcept
  : renderer_(observer_ptr { &renderer })
{
}

auto Renderer::OffscreenSceneFacade::SetFrameSession(FrameSessionInput session)
  -> OffscreenSceneFacade&
{
  frame_session_.emplace(session);
  return *this;
}

auto Renderer::OffscreenSceneFacade::SetSceneSource(SceneSourceInput scene)
  -> OffscreenSceneFacade&
{
  scene_source_.emplace(scene);
  return *this;
}

auto Renderer::OffscreenSceneFacade::SetViewIntent(
  const OffscreenSceneViewInput& view) -> OffscreenSceneFacade&
{
  view_intent_.emplace(view);
  return *this;
}

auto Renderer::OffscreenSceneFacade::SetOutputTarget(OutputTargetInput target)
  -> OffscreenSceneFacade&
{
  output_target_.emplace(target);
  return *this;
}

auto Renderer::OffscreenSceneFacade::SetPipeline(
  OffscreenPipelineInput pipeline) -> OffscreenSceneFacade&
{
  pipeline_.emplace(pipeline);
  return *this;
}

auto Renderer::OffscreenSceneFacade::CanFinalize() const -> bool
{
  return frame_session_.has_value() && scene_source_.has_value()
    && view_intent_.has_value() && output_target_.has_value();
}

auto Renderer::OffscreenSceneFacade::Validate() const -> ValidationReport
{
  auto report = ValidationReport {};

  if (!frame_session_.has_value()) {
    report.issues.push_back(ValidationIssue {
      .code = "frame_session.missing",
      .message = "Offscreen scene requires a frame session",
    });
  } else if (frame_session_->frame_slot == frame::kInvalidSlot) {
    report.issues.push_back(ValidationIssue {
      .code = "frame_session.invalid_slot",
      .message = "Offscreen scene requires a valid frame slot",
    });
  } else if (!std::isfinite(frame_session_->delta_time_seconds)
    || frame_session_->delta_time_seconds <= 0.0F) {
    report.issues.push_back(ValidationIssue {
      .code = "frame_session.invalid_delta_time",
      .message = "Offscreen scene requires a finite positive delta time",
    });
  }

  if (!scene_source_.has_value() || scene_source_->scene == nullptr) {
    report.issues.push_back(ValidationIssue {
      .code = "scene_source.missing",
      .message = "Offscreen scene requires a live scene source",
    });
  }

  if (!view_intent_.has_value()) {
    report.issues.push_back(ValidationIssue {
      .code = "view_intent.missing",
      .message = "Offscreen scene requires a view intent",
    });
  } else {
    const auto& view_intent = view_intent_->ViewIntent();
    auto camera = view_intent.camera.value_or(scene::SceneNode {});
    if (!camera.IsAlive() || !camera.HasCamera()) {
      report.issues.push_back(ValidationIssue {
        .code = "view_intent.invalid_camera",
        .message = "Offscreen scene requires a live camera node",
      });
    }
  }

  if (!output_target_.has_value() || output_target_->framebuffer == nullptr) {
    report.issues.push_back(ValidationIssue {
      .code = "output_target.missing",
      .message = "Offscreen scene requires an output target framebuffer",
    });
  }

  return report;
}

auto Renderer::OffscreenSceneFacade::Finalize()
  -> std::expected<ValidatedOffscreenSceneSession, ValidationReport>
{
  auto report = Validate();
  if (!report.Ok()) {
    return std::unexpected<ValidationReport> { report };
  }

  const auto pipeline_input
    = pipeline_.has_value() ? *pipeline_ : OffscreenPipelineInput {};
  return std::expected<ValidatedOffscreenSceneSession, ValidationReport>(
    std::in_place, *renderer_, *frame_session_, *scene_source_, *view_intent_,
    *output_target_, pipeline_input);
}

} // namespace oxygen::vortex
