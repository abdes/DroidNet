//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numeric>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Internal/SceneConstantsManager.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/RenderContextPool.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/CollectionConfig.h>
#include <Oxygen/Renderer/ScenePrep/FinalizationConfig.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepPipeline.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/Types/MaterialConstants.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/Types/SceneConstants.h>
#include <Oxygen/Renderer/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Scene/Scene.h>

// Implementation of RendererTagFactory. Provides access to RendererTag
// capability tokens, only from the engine core. When building tests, allow
// tests to override by defining OXYGEN_ENGINE_TESTING.
#if !defined(OXYGEN_ENGINE_TESTING)
namespace oxygen::renderer::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::renderer::internal
#endif

using oxygen::Graphics;
using oxygen::data::Mesh;
using oxygen::data::detail::IndexType;
using oxygen::engine::MaterialConstants;
using oxygen::engine::Renderer;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::SingleQueueStrategy;

//===----------------------------------------------------------------------===//
// Renderer Implementation
//===----------------------------------------------------------------------===//

Renderer::Renderer(std::weak_ptr<Graphics> graphics, RendererConfig config)
  : gfx_weak_(std::move(graphics))
  , scene_prep_(std::make_unique<sceneprep::ScenePrepPipelineImpl<
        decltype(sceneprep::CreateBasicCollectionConfig()),
        decltype(sceneprep::CreateStandardFinalizationConfig())>>(
      sceneprep::CreateBasicCollectionConfig(),
      sceneprep::CreateStandardFinalizationConfig()))
{
  LOG_F(
    2, "Renderer::Renderer [this={}] - constructor", static_cast<void*>(this));

  CHECK_F(!gfx_weak_.expired(), "Renderer constructed with expired Graphics");
  auto gfx = gfx_weak_.lock();

  // Require a non-empty upload queue key in the renderer configuration.
  CHECK_F(!config.upload_queue_key.empty(),
    "RendererConfig.upload_queue_key must not be empty");

  // Build upload policy and honour configured upload queue from Renderer
  // configuration.
  auto policy = upload::DefaultUploadPolicy();
  policy.upload_queue_key = graphics::QueueKey { config.upload_queue_key };

  uploader_ = std::make_unique<upload::UploadCoordinator>(
    observer_ptr { gfx.get() }, policy);
  upload_staging_provider_
    = uploader_->CreateRingBufferStaging(frame::kFramesInFlight, 16, 0.5f);

  inline_transfers_ = std::make_unique<upload::InlineTransfersCoordinator>(
    observer_ptr { gfx.get() });
  inline_staging_provider_
    = uploader_->CreateRingBufferStaging(frame::kFramesInFlight, 16, 0.5f);
  inline_transfers_->RegisterProvider(inline_staging_provider_);

  auto geom_uploader = std::make_unique<renderer::resources::GeometryUploader>(
    observer_ptr { gfx.get() }, observer_ptr { uploader_.get() },
    observer_ptr { upload_staging_provider_.get() });
  auto xform_uploader
    = std::make_unique<renderer::resources::TransformUploader>(
      observer_ptr { gfx.get() },
      observer_ptr { inline_staging_provider_.get() },
      observer_ptr { inline_transfers_.get() });
  auto mat_binder = std::make_unique<renderer::resources::MaterialBinder>(
    observer_ptr { gfx.get() }, observer_ptr { uploader_.get() },
    observer_ptr { upload_staging_provider_.get() });
  auto emitter = std::make_unique<renderer::resources::DrawMetadataEmitter>(
    observer_ptr { gfx.get() }, observer_ptr { inline_staging_provider_.get() },
    observer_ptr { geom_uploader.get() }, observer_ptr { mat_binder.get() },
    observer_ptr { inline_transfers_.get() });

  scene_prep_state_
    = std::make_unique<sceneprep::ScenePrepState>(std::move(geom_uploader),
      std::move(xform_uploader), std::move(mat_binder), std::move(emitter));

  // Initialize scene constants manager for per-view, per-slot Upload heap
  // buffers
  scene_const_manager_ = std::make_unique<internal::SceneConstantsManager>(
    observer_ptr { gfx.get() },
    static_cast<std::uint32_t>(sizeof(SceneConstants::GpuData)));

  // Initialize the render-context pool helper used to claim per-frame
  // render contexts during PreRender/Render phases.
  render_context_pool_ = std::make_unique<RenderContextPool>();
}

Renderer::~Renderer()
{
  scene_const_manager_.reset();
  scene_prep_state_.reset();
  uploader_.reset();
  upload_staging_provider_.reset();
  inline_transfers_.reset();
  inline_staging_provider_.reset();
}

auto Renderer::GetGraphics() -> std::shared_ptr<Graphics>
{
  auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    throw std::runtime_error("Graphics expired in Renderer::GetGraphics");
  }
  return graphics_ptr;
}

auto Renderer::RegisterView(
  ViewId view_id, ViewResolver resolver, RenderGraphFactory factory) -> void
{
  std::unique_lock lock(view_registration_mutex_);
  view_resolvers_.insert_or_assign(view_id, std::move(resolver));
  render_graphs_.insert_or_assign(view_id, std::move(factory));
  LOG_F(INFO, "[Renderer] RegisterView: view_id={}, total_views={}",
    view_id.get(), render_graphs_.size());
}

auto Renderer::UnregisterView(ViewId view_id) -> void
{
  std::size_t removed_resolver = 0;
  std::size_t removed_graph = 0;
  {
    std::unique_lock lock(view_registration_mutex_);
    removed_resolver = view_resolvers_.erase(view_id);
    removed_graph = render_graphs_.erase(view_id);
  }

  LOG_F(INFO,
    "[Renderer] UnregisterView: view_id={}, removed_resolver={}, "
    "removed_factory={}",
    view_id.get(), removed_resolver, removed_graph);

  std::size_t pending_size = 0;
  {
    std::lock_guard lock(pending_cleanup_mutex_);
    pending_cleanup_.insert(view_id);
    pending_size = pending_cleanup_.size();
  }

  LOG_F(
    INFO, "[Renderer] UnregisterView: pending_cleanup_count={}", pending_size);

  {
    std::unique_lock state_lock(view_state_mutex_);
    view_ready_states_.erase(view_id);
  }
}

auto Renderer::IsViewReady(ViewId view_id) const -> bool
{
  std::shared_lock lock(view_state_mutex_);
  const auto it = view_ready_states_.find(view_id);
  return it != view_ready_states_.end() && it->second;
}

auto Renderer::OnPreRender(FrameContext& context) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  DrainPendingViewCleanup("OnPreRender");

  {
    std::shared_lock lock(view_registration_mutex_);
    if (render_graphs_.empty()) {
      DLOG_F(WARNING, "no render graphs registered; skipping");
      co_return;
    }

    if (view_resolvers_.empty()) {
      DLOG_F(WARNING, "no view resolvers registered; skipping");
      co_return;
    }
  }

  // Failing to acquire a slot will throw, and drop the frame.
  render_context_
    = observer_ptr { &render_context_pool_->Acquire(context.GetFrameSlot()) };

  // Clear the per-frame and per-view state (per-frame caches are refreshed
  // at the start of PreRender). Deferred cleanup of unregistered views is
  // performed at frame end (OnFrameEnd) to avoid destroying entries while
  // other modules may add registrations during the frame start.
  {
    std::unique_lock state_lock(view_state_mutex_);
    view_ready_states_.clear();
  }
  resolved_views_.clear();
  prepared_frames_.clear();
  per_view_storage_.clear();

  // Iterate all views registered in FrameContext and prepare each one
  auto views_range = context.GetViews();
  bool first = true;

  for (const auto& view_ref : views_range) {
    const auto& view_ctx = view_ref.get();
    DLOG_SCOPE_F(2,
      fmt::format(
        "View {} ({})", nostd::to_string(view_ctx.id), view_ctx.metadata.name)
        .c_str());
    try {
      ViewResolver resolver_copy;
      {
        std::shared_lock lock(view_registration_mutex_);
        const auto resolver_it = view_resolvers_.find(view_ctx.id);
        if (resolver_it == view_resolvers_.end()) {
          LOG_F(2, "View {} has no resolver; skipping", view_ctx.id.get());
          continue;
        }
        resolver_copy = resolver_it->second; // copy function
      }

      // Invoke resolver outside of the registration lock to avoid locking
      // user-provided code paths.
      const auto resolved = resolver_copy(view_ctx);

      // Cache the resolved view for use in OnRender
      resolved_views_.insert_or_assign(view_ctx.id, resolved);

      // Build frame data for this view (scene prep, culling, draw list)
      const auto draw_count
        = RunScenePrep(view_ctx.id, resolved, context, first);
      first = false;

      DLOG_F(2, "view prepared with {} draws", draw_count);

      // Mark view as ready for rendering
      // Note: ViewId not directly available from GetViews() range
      // Apps should use ViewContext metadata to identify views if needed

    } catch (const std::exception& ex) {
      LOG_F(WARNING, "-failed- : {}", ex.what());
    }
  }

  LOG_SCOPE_F(2, "Populating renderer-level scene constants");

  if (const auto transforms = scene_prep_state_->GetTransformUploader()) {
    const auto worlds_srv = transforms->GetWorldsSrvIndex();
    const auto normals_srv = transforms->GetNormalsSrvIndex();
    DLOG_F(3, "Worlds: {}", worlds_srv);
    DLOG_F(3, "Normals: {}", normals_srv);
    scene_const_cpu_.SetBindlessWorldsSlot(
      BindlessWorldsSlot(worlds_srv.get()), SceneConstants::kRenderer);
    scene_const_cpu_.SetBindlessNormalMatricesSlot(
      BindlessNormalsSlot(normals_srv.get()), SceneConstants::kRenderer);
  }

  if (const auto materials = scene_prep_state_->GetMaterialBinder()) {
    const auto materials_srv = materials->GetMaterialsSrvIndex();
    DLOG_F(3, "Materials: {}", materials_srv);
    scene_const_cpu_.SetBindlessMaterialConstantsSlot(
      BindlessMaterialConstantsSlot(materials_srv.get()),
      SceneConstants::kRenderer);
  }

  if (auto emitter = scene_prep_state_->GetDrawMetadataEmitter()) {
    const auto draw_metadata_srv = emitter->GetDrawMetadataSrvIndex();
    DLOG_F(3, "Draw Metadata: {}", draw_metadata_srv);
    scene_const_cpu_.SetBindlessDrawMetadataSlot(
      BindlessDrawMetadataSlot(draw_metadata_srv.get()),
      SceneConstants::kRenderer);
  }

  co_return;
}

auto Renderer::OnRender(FrameContext& context) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  // Early exit if no render render context
  if (!render_context_) {
    DLOG_F(WARNING, "no render context available; skipping");
    co_return;
  }

  auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    LOG_F(WARNING, "Graphics expired; skipping");
    co_return;
  }
  auto& graphics = *graphics_ptr;

  // Iterate all views and execute their registered render graphs.
  // Take a snapshot of the registered factories under lock so
  // UnregisterView() can safely mutate the underlying containers
  // without invalidating our iteration.
  std::vector<std::pair<ViewId, RenderGraphFactory>> graphs_snapshot;
  {
    std::shared_lock lock(view_registration_mutex_);
    graphs_snapshot.reserve(render_graphs_.size());
    for (const auto& kv : render_graphs_) {
      graphs_snapshot.emplace_back(kv.first, kv.second);
    }
  }

  for (const auto& [view_id, factory] : graphs_snapshot) {
    DLOG_SCOPE_F(2, fmt::format("View {}", nostd::to_string(view_id)).c_str());

    // Mark view as not ready initially
    {
      std::unique_lock state_lock(view_state_mutex_);
      view_ready_states_[view_id] = false;
    }

    try {
      // Get the ViewContext for this view to access output framebuffer
      const auto& view_ctx = context.GetViewContext(view_id);

      // Skip if no output framebuffer assigned
      if (!view_ctx.output) {
        LOG_F(WARNING, "View {} has no output framebuffer; skipping",
          view_id.get());
        continue;
      }

      // Acquire command recorder for this view
      auto recorder_ptr = AcquireRecorderForView(view_id, graphics);
      if (!recorder_ptr) {
        LOG_F(ERROR, "Could not acquire recorder for view {}; skipping",
          view_id.get());
        continue;
      }
      auto recorder = recorder_ptr.get();

      if (!SetupFramebufferForView(
            context, view_id, *recorder, *render_context_)) {
        LOG_F(ERROR, "Failed to setup framebuffer for view {}; skipping",
          view_id.get());
        continue;
      }

      auto update_view_state = [&](ViewId view_id, bool success) -> void {
        std::unique_lock state_lock(view_state_mutex_);
        view_ready_states_[view_id] = success;
      };

      // Prepare and wire per-view SceneConstants and populate the
      // render_context.current_view when available. The helper handles
      // the resolved/prepared checks, logging and buffer writes.
      if (!PrepareAndWireSceneConstantsForView(
            view_id, context, *render_context_)) {
        // Failure already logged inside helper; mark the view failed and
        // skip this view's render graph.
        update_view_state(view_id, false);
        continue;
      }

      // Execute the registered render graph for this view
      const bool rv = co_await ExecuteRenderGraphForView(
        view_id, factory, *render_context_, *recorder);

      // Finalize state and instrumentation
      update_view_state(view_id, rv);
    } catch (const std::exception& ex) {
      LOG_F(ERROR, "Failed to render view {}: {}", view_id.get(), ex.what());
      std::unique_lock state_lock(view_state_mutex_);
      view_ready_states_[view_id] = false;
    }
  }

  // Return the pooled context for this slot to a clean state and clear the
  // debug in-use marker.
  render_context_pool_->Release(context.GetFrameSlot());
  render_context_.reset();

  co_return;
}

auto Renderer::OnFrameEnd(FrameContext& /*context*/) -> void
{
  LOG_SCOPE_FUNCTION(2);

  DrainPendingViewCleanup("OnFrameEnd");
}

auto Renderer::DrainPendingViewCleanup(std::string_view reason) -> void
{
  std::unordered_set<ViewId> pending;
  {
    std::lock_guard lock(pending_cleanup_mutex_);
    if (pending_cleanup_.empty()) {
      LOG_F(INFO, "[Renderer] Pending cleanup: none ({})", reason);
      return;
    }
    pending.swap(pending_cleanup_);
  }

  LOG_F(INFO, "[Renderer] Pending cleanup: processing {} views ({})",
    pending.size(), reason);

  for (const auto& id : pending) {
    resolved_views_.erase(id);
    prepared_frames_.erase(id);
    per_view_storage_.erase(id);
  }

  {
    std::unique_lock state_lock(view_state_mutex_);
    for (const auto& id : pending) {
      view_ready_states_.erase(id);
    }
  }
}

//===----------------------------------------------------------------------===//
// PreExecute helper implementations
//===----------------------------------------------------------------------===//

// Removed legacy draw-metadata helpers; lifecycle now handled by
// DrawMetadataEmitter via ScenePrepState

auto Renderer::WireContext(RenderContext& render_context,
  const std::shared_ptr<graphics::Buffer>& scene_consts) -> void
{
  DLOG_SCOPE_FUNCTION(3);

  render_context.scene_constants = scene_consts;
  const auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    throw std::runtime_error("Graphics expired in Renderer::WireContext");
  }
  render_context.SetRenderer(this, graphics_ptr.get());
}

auto Renderer::AcquireRecorderForView(ViewId view_id, Graphics& gfx)
  -> std::shared_ptr<oxygen::graphics::CommandRecorder>
{
  DLOG_SCOPE_FUNCTION(3);

  const auto queue_key
    = gfx.QueueKeyFor(oxygen::graphics::QueueRole::kGraphics);
  return gfx.AcquireCommandRecorder(
    queue_key, std::string("View_") + std::to_string(view_id.get()));
}

auto Renderer::SetupFramebufferForView(const FrameContext& frame_context,
  ViewId view_id, graphics::CommandRecorder& recorder,
  RenderContext& render_context) -> bool
{
  DLOG_SCOPE_FUNCTION(3);

  const auto& view_ctx = frame_context.GetViewContext(view_id);

  if (!view_ctx.output) {
    LOG_F(WARNING, "View {} has no output", view_id.get());
    return false;
  }

  const auto& fb_desc = view_ctx.output->GetDescriptor();
  for (const auto& attachment : fb_desc.color_attachments) {
    if (attachment.texture) {
      // Use the texture's own descriptor initial_state when available.
      // Previously we assumed swapchain backbuffers for all color
      // attachments and used kPresent which breaks for render-to-texture
      // targets (e.g. EditorView). Honoring the texture descriptor avoids
      // conflicting initial states being tracked and prevents invalid
      // barrier sequences.
      auto initial = attachment.texture->GetDescriptor().initial_state;
      if (initial == graphics::ResourceStates::kUnknown
        || initial == graphics::ResourceStates::kUndefined) {
        initial = graphics::ResourceStates::kPresent;
      }
      recorder.BeginTrackingResourceState(*attachment.texture, initial, true);
      LOG_F(INFO, "Renderer: BeginTracking color attachment {} initial={}",
        static_cast<const void*>(attachment.texture.get()),
        nostd::to_string(initial));
      recorder.RequireResourceState(
        *attachment.texture, graphics::ResourceStates::kRenderTarget);
    }
  }

  if (fb_desc.depth_attachment.texture) {
    recorder.BeginTrackingResourceState(*fb_desc.depth_attachment.texture,
      graphics::ResourceStates::kDepthWrite, true);
    recorder.FlushBarriers();
  }

  recorder.BindFrameBuffer(*view_ctx.output);
  render_context.framebuffer = view_ctx.output;
  return true;
}

auto Renderer::PrepareAndWireSceneConstantsForView(ViewId view_id,
  const FrameContext& frame_context, RenderContext& render_context) -> bool
{
  DLOG_SCOPE_FUNCTION(3);

  auto resolved_it = resolved_views_.find(view_id);
  auto prepared_it = prepared_frames_.find(view_id);

  if (resolved_it == resolved_views_.end()
    || prepared_it == prepared_frames_.end()) {
    LOG_F(2, "No cached data for view {} (resolved={}, prepared={})",
      view_id.get(), resolved_it != resolved_views_.end(),
      prepared_it != prepared_frames_.end());
    return false;
  }

  // Create a per-view scene constants snapshot based on the last frame-level
  // scene_const_cpu_ and per-view SRV indices captured during RunScenePrep.
  SceneConstants view_scene_consts = scene_const_cpu_;
  const auto& prepared = prepared_it->second;
  DLOG_F(3, "   worlds: {}", prepared.bindless_worlds_slot);
  DLOG_F(3, "  normals: {}", prepared.bindless_normals_slot);
  DLOG_F(3, "materials: {}", prepared.bindless_materials_slot);
  DLOG_F(3, " metadata: {}", prepared.bindless_draw_metadata_slot);

  view_scene_consts.SetBindlessWorldsSlot(
    BindlessWorldsSlot(prepared.bindless_worlds_slot),
    SceneConstants::kRenderer);
  view_scene_consts.SetBindlessNormalMatricesSlot(
    BindlessNormalsSlot(prepared.bindless_normals_slot),
    SceneConstants::kRenderer);
  view_scene_consts.SetBindlessMaterialConstantsSlot(
    BindlessMaterialConstantsSlot(prepared.bindless_materials_slot),
    SceneConstants::kRenderer);
  view_scene_consts.SetBindlessDrawMetadataSlot(
    BindlessDrawMetadataSlot(prepared.bindless_draw_metadata_slot),
    SceneConstants::kRenderer);

  const auto& proj_matrix = resolved_it->second.ProjectionMatrix();
  view_scene_consts.SetViewMatrix(resolved_it->second.ViewMatrix())
    .SetProjectionMatrix(proj_matrix)
    .SetCameraPosition(resolved_it->second.CameraPosition())
    .SetFrameSlot(frame_context.GetFrameSlot(), SceneConstants::kRenderer)
    .SetFrameSequenceNumber(
      frame_context.GetFrameSequenceNumber(), SceneConstants::kRenderer);

  // Write constants into per-view mapped buffer
  const auto& snapshot = view_scene_consts.GetSnapshot();
  auto buffer_info = scene_const_manager_->WriteSceneConstants(
    view_id, &snapshot, sizeof(SceneConstants::GpuData));
  if (!buffer_info.buffer) {
    LOG_F(ERROR, "Failed to write scene constants for view {}", view_id);
    return false;
  }

  WireContext(render_context, buffer_info.buffer);

  // Populate render_context.current_view
  render_context.current_view.view_id = view_id;
  render_context.current_view.resolved_view.reset(&resolved_it->second);
  render_context.current_view.prepared_frame.reset(&prepared_it->second);

  return true;
}

auto Renderer::ExecuteRenderGraphForView(ViewId view_id,
  const RenderGraphFactory& factory, RenderContext& render_context,
  graphics::CommandRecorder& recorder) -> co::Co<bool>
{
  DLOG_SCOPE_FUNCTION(3);

  try {
    co_await factory(view_id, render_context, recorder);
    co_return true;
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "RenderGraph execution for view {} failed: {}", view_id,
      ex.what());
    co_return false;
  } catch (...) {
    LOG_F(ERROR, "RenderGraph execution for view {} failed: unknown error",
      view_id.get());
    co_return false;
  }
}

auto Renderer::RunScenePrep(ViewId view_id, const ResolvedView& view,
  const FrameContext& frame_context, bool run_frame_phase) -> std::size_t
{
  DLOG_SCOPE_FUNCTION(3);

  auto scene_ptr = frame_context.GetScene();
  CHECK_NOTNULL_F(scene_ptr, "FrameContext.scene is null in RunScenePrep");
  auto& scene = *scene_ptr;

  // Get or create the prepared frame for this specific view
  auto& prepared_frame = prepared_frames_[view_id];

  auto frame_seq = frame_context.GetFrameSequenceNumber();

  if (run_frame_phase) {
    DLOG_SCOPE_F(3,
      fmt::format("frame-phase for frame seq {}", nostd::to_string(frame_seq))
        .c_str());
    scene_prep_->Collect(
      scene, std::nullopt, frame_seq, *scene_prep_state_, true);
    scene_prep_->Finalize();
  }

  ::oxygen::observer_ptr<const ::oxygen::ResolvedView> view_ptr(&view);
  DLOG_SCOPE_F(3,
    fmt::format("view-phase for view {}", nostd::to_string(view_id)).c_str());
  {
    scene_prep_->Collect(scene,
      std::optional<::oxygen::observer_ptr<const ::oxygen::ResolvedView>>(
        view_ptr),
      frame_seq, *scene_prep_state_,
      run_frame_phase); // Only reset on first view
    scene_prep_->Finalize();

    // CRITICAL: Capture bindless SRV indices IMMEDIATELY after Finalize
    // These indices are valid only for THIS view's finalization and will be
    // overwritten when the next view calls Finalize. Store them in THIS view's
    // prepared_frame so OnRender can use the correct indices.
    if (const auto transforms = scene_prep_state_->GetTransformUploader()) {
      prepared_frame.bindless_worlds_slot
        = transforms->GetWorldsSrvIndex().get();
      DLOG_F(3, " captured worlds: {}", prepared_frame.bindless_worlds_slot);
      prepared_frame.bindless_normals_slot
        = transforms->GetNormalsSrvIndex().get();
      DLOG_F(3, "captured normals: {}", prepared_frame.bindless_normals_slot);
    }
    if (const auto materials = scene_prep_state_->GetMaterialBinder()) {
      prepared_frame.bindless_materials_slot
        = materials->GetMaterialsSrvIndex().get();
    }
    if (auto emitter = scene_prep_state_->GetDrawMetadataEmitter()) {
      prepared_frame.bindless_draw_metadata_slot
        = emitter->GetDrawMetadataSrvIndex().get();
    }
  }

  PublishPreparedFrameSpans(view_id, prepared_frame);
  UpdateSceneConstantsFromView(view);

  const auto draw_count
    = prepared_frame.draw_metadata_bytes.size() / sizeof(DrawMetadata);

  DLOG_F(3, "draw count: {}", draw_count);
  return draw_count;
}

auto Renderer::PublishPreparedFrameSpans(
  ViewId view_id, PreparedSceneFrame& prepared_frame) -> void
{
  DLOG_SCOPE_FUNCTION(3);

  // Ensure per-view backing storage exists
  auto& storage = per_view_storage_[view_id];

  const auto transforms = scene_prep_state_->GetTransformUploader();
  const auto world_span = transforms->GetWorldMatrices();

  // Copy matrix floats into per-view storage so spans stay valid
  storage.world_matrix_storage.assign(
    reinterpret_cast<const float*>(world_span.data()),
    reinterpret_cast<const float*>(world_span.data())
      + world_span.size() * 16u);

  prepared_frame.world_matrices = std::span<const float>(
    storage.world_matrix_storage.data(), storage.world_matrix_storage.size());

  const auto normal_span = transforms->GetNormalMatrices();
  storage.normal_matrix_storage.assign(
    reinterpret_cast<const float*>(normal_span.data()),
    reinterpret_cast<const float*>(normal_span.data())
      + normal_span.size() * 16u);

  prepared_frame.normal_matrices = std::span<const float>(
    storage.normal_matrix_storage.data(), storage.normal_matrix_storage.size());

  // Publish draw metadata bytes and partitions from emitter accessors
  if (auto emitter = scene_prep_state_->GetDrawMetadataEmitter()) {
    const auto src_bytes = emitter->GetDrawMetadataBytes();
    storage.draw_metadata_storage.assign(src_bytes.begin(), src_bytes.end());

    prepared_frame.draw_metadata_bytes
      = std::span<const std::byte>(storage.draw_metadata_storage.data(),
        storage.draw_metadata_storage.size());

    using PR = oxygen::engine::PreparedSceneFrame::PartitionRange;
    const auto parts = emitter->GetPartitions();
    storage.partition_storage.assign(parts.begin(), parts.end());

    prepared_frame.partitions = std::span<const PR>(
      storage.partition_storage.data(), storage.partition_storage.size());
  } else {
    // No emitter -> empty spans
    prepared_frame.draw_metadata_bytes = {};
    prepared_frame.partitions = {};
  }
}

auto Renderer::UpdateSceneConstantsFromView(const ResolvedView& view) -> void
{
  // Update scene constants from the provided view snapshot
  scene_const_cpu_.SetViewMatrix(view.ViewMatrix())
    .SetProjectionMatrix(view.ProjectionMatrix())
    .SetCameraPosition(view.CameraPosition());
}

auto Renderer::OnFrameStart(FrameContext& context) -> void
{
  DLOG_SCOPE_FUNCTION(2);

  auto tag = oxygen::renderer::internal::RendererTagFactory::Get();
  auto frame_slot = context.GetFrameSlot();
  auto frame_sequence = context.GetFrameSequenceNumber();

  // Initialize Upload Coordinator and its staging providers for the new frame
  // slot BEFORE any uploaders start allocating from them.
  inline_transfers_->OnFrameStart(tag, frame_slot);
  uploader_->OnFrameStart(tag, frame_slot);
  // then uploaders and scene constants manager
  scene_const_manager_->OnFrameStart(frame_slot);
  scene_prep_state_->GetTransformUploader()->OnFrameStart(
    tag, frame_sequence, frame_slot);
  scene_prep_state_->GetGeometryUploader()->OnFrameStart(tag, frame_slot);
  scene_prep_state_->GetMaterialBinder()->OnFrameStart(tag, frame_slot);
  if (auto emitter = scene_prep_state_->GetDrawMetadataEmitter()) {
    emitter->OnFrameStart(tag, frame_sequence, frame_slot);
  }
}

/*!
 Executes the scene transform propagation phase.

 Flow
 1. Acquire non-owning scene pointer from the frame context.
 2. If absent: early return (benign no-op, keeps frame deterministic).
 3. Call Scene::Update() which performs:
    - Pass 1: Dense linear scan processing dirty node flags (non-transform).
    - Pass 2: Pre-order filtered traversal (DirtyTransformFilter) resolving
      world transforms only along dirty chains (parent first).
 4. Return; no extra state retained by this module.

 Invariants / Guarantees
 - Invoked exactly once per frame in kTransformPropagation phase.
 - Parent world matrix valid before any child transform recompute.
 - Clean descendants of a dirty ancestor incur only an early-out check.
 - kIgnoreParentTransform subtrees intentionally skipped per design.
 - No scene graph structural mutation occurs here.
 - No GPU resource mutation or uploads here (CPU authoritative only).

 Never Do
 - Do not reparent / create / destroy nodes here.
 - Do not call Scene::Update() more than once per frame.
 - Do not cache raw pointers across frames.
 - Do not allocate large transient buffers (Scene owns traversal memory).
 - Do not introduce side-effects dependent on sibling visitation order.

 Performance Characteristics
 - Time: O(F + T) where F = processed dirty flags, T = visited transform
   chain nodes (<= total nodes, typically sparse).
 - Memory: No steady-state allocations.
 - Optimization: Early-exit for clean transforms; dense flag pass for cache
 locality.

 Future Improvement (Parallel Chains)
 - The scene's root hierarchies are independent for transform propagation.
 - A future optimization can collect the subset of root hierarchies that have
   at least one dirty descendant and dispatch each qualifying root subtree to
   a worker task (parent-first order preserved inside each task, no sharing).
 - Synchronize (join) all tasks before proceeding to later phases to maintain
   frame determinism. Skip parallel dispatch below a configurable dirty-node
   threshold to avoid overhead on small scenes.
 - This preserves all existing invariants (no graph mutation, parent-first,
   single update per node) while offering scalable speedups on large scenes.

 @note Dirty flag semantics, traversal filtering, and no-mutation policy are
       deliberate and should be preserved.
 @see oxygen::scene::Scene::Update
 @see oxygen::scene::SceneTraversal::UpdateTransforms
 @see oxygen::scene::DirtyTransformFilter
*/
auto Renderer::OnTransformPropagation(FrameContext& context) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  // Acquire scene pointer (non-owning). If absent, log once per frame in debug.
  auto scene_ptr = context.GetScene();
  if (!scene_ptr) {
    DLOG_F(WARNING,
      "No active scene set in FrameContext; skipping transform propagation");
    co_return; // Nothing to update
  }

  // Perform hierarchy propagation & world matrix updates.
  scene_ptr->Update();

  co_return;
}
