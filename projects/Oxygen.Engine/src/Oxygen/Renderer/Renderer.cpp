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
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Engine/FrameContext.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/CameraView.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/CollectionConfig.h>
#include <Oxygen/Renderer/ScenePrep/FinalizationConfig.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepPipeline.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/Types/MaterialConstants.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/Types/SceneConstants.h>
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
  staging_provider_
    = uploader_->CreateRingBufferStaging(frame::kFramesInFlight, 16, 0.5f);

  auto geom_uploader = std::make_unique<renderer::resources::GeometryUploader>(
    observer_ptr { gfx.get() }, observer_ptr { uploader_.get() },
    observer_ptr { staging_provider_.get() });
  auto xform_uploader
    = std::make_unique<renderer::resources::TransformUploader>(
      observer_ptr { gfx.get() }, observer_ptr { uploader_.get() },
      observer_ptr { staging_provider_.get() });
  auto mat_binder = std::make_unique<renderer::resources::MaterialBinder>(
    observer_ptr { gfx.get() }, observer_ptr { uploader_.get() },
    observer_ptr { staging_provider_.get() });
  auto emitter = std::make_unique<renderer::resources::DrawMetadataEmitter>(
    observer_ptr { gfx.get() }, observer_ptr { uploader_.get() },
    observer_ptr { staging_provider_.get() },
    observer_ptr { geom_uploader.get() }, observer_ptr { mat_binder.get() });

  scene_prep_state_
    = std::make_unique<sceneprep::ScenePrepState>(std::move(geom_uploader),
      std::move(xform_uploader), std::move(mat_binder), std::move(emitter));
}

Renderer::~Renderer()
{
  scene_prep_state_.reset();
  staging_provider_.reset();
}

auto Renderer::GetGraphics() -> std::shared_ptr<Graphics>
{
  auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    throw std::runtime_error("Graphics expired in Renderer::GetGraphics");
  }
  return graphics_ptr;
}

auto Renderer::PreExecute(
  RenderContext& render_context, const FrameContext& frame_context) -> void
{
  // Contract checks (kept inline per style preference)
  DCHECK_F(!render_context.scene_constants,
    "RenderContext.scene_constants must be null; renderer sets it via "
    "ModifySceneConstants/PreExecute");
  // Legacy AoS draw list path (opaque_items_) removed from PreExecute. All
  // per-draw GPU residency assurance now happens during SoA finalization
  // (FinalizeScenePrepSoA) via EnsureMeshResources(). RenderContext
  // opaque_draw_list remains intentionally untouched (empty span) for
  // transitional compatibility with any remaining callers; passes should now
  // consume prepared_frame / draw_metadata_bytes instead.

  // Consolidated transform resource preparation
  if (const auto transforms = scene_prep_state_->GetTransformUploader()) {
    const auto worlds_srv = transforms->GetWorldsSrvIndex();
    const auto normals_srv = transforms->GetNormalsSrvIndex();

    scene_const_cpu_.SetBindlessWorldsSlot(
      BindlessWorldsSlot(worlds_srv.get()), SceneConstants::kRenderer);
    scene_const_cpu_.SetBindlessNormalMatricesSlot(
      BindlessNormalsSlot(normals_srv.get()), SceneConstants::kRenderer);
  }

  // Consolidated material resource preparation
  if (const auto materials = scene_prep_state_->GetMaterialBinder()) {
    const auto materials_srv = materials->GetMaterialsSrvIndex();
    scene_const_cpu_.SetBindlessMaterialConstantsSlot(
      BindlessMaterialConstantsSlot(materials_srv.get()),
      SceneConstants::kRenderer);
  }

  // Publish draw-metadata bindless slot from the ScenePrep emitter
  if (auto emitter = scene_prep_state_->GetDrawMetadataEmitter()) {
    const auto slot = emitter->GetDrawMetadataSrvIndex();
    scene_const_cpu_.SetBindlessDrawMetadataSlot(
      BindlessDrawMetadataSlot(slot.get()), SceneConstants::kRenderer);
  }

  MaybeUpdateSceneConstants(frame_context);

  WireContext(render_context);

  // Wire PreparedSceneFrame pointer (SoA finalized snapshot). This enables
  // passes to start consuming SoA data incrementally. Null remains valid if
  // finalization produced an empty frame.
  render_context.prepared_frame.reset(&prepared_frame_);

  // Ensure any upload command lists are submitted promptly for this frame.
  // if (uploader_) {
  //   uploader_->Flush();
  //   gfx_weak_.lock()->Flush();
  // }
}

// ReSharper disable once CppMemberFunctionMayBeStatic
auto Renderer::PostExecute(RenderContext& render_context) -> void
{
  // RenderContext::Reset now clears per-frame injected buffers (scene &
  // material).
  render_context.Reset();
}

auto Renderer::OnFrameGraph(FrameContext& context) -> co::Co<>
{
  if (skip_frame_render_) {
    co_return;
  }

  // FIXME: temporary: single view rendering only
  // Pick the first available view from FrameContext's view-transform view
  bool found = false;
  for (const auto& viewRef : context.GetViews()) {
    const auto& view = viewRef.get();
    const auto resolved_view = view.Resolve();
    BuildFrame(resolved_view, context);
    found = true;
    break; // temporary single-view support
  }
  if (!found) {
    LOG_F(WARNING, "Renderer::OnFrameGraph: no views in FrameContext");
    skip_frame_render_ = true;
    co_return;
  }

  co_return;
}

auto Renderer::OnCommandRecord(FrameContext& context) -> co::Co<>
{
  // Currently App Module will call ExecuteRenderGraph directly on its main
  // thread.

  // Once done, mark surfaces for rendered views presentable.

  // Mark our surface as presentable after rendering is complete
  // This is part of the module contract - surfaces must be marked presentable
  // before the Present phase. Since FrameContext is recreated each frame,
  // we need to find and mark our surface every frame.
  for (const auto& viewRef : context.GetViews()) {
    const auto& view = viewRef.get();
    const auto surface_result = view.GetSurface();
    if (!surface_result) {
      LOG_F(WARNING, "Could not mark surface presentable for view {}: {}",
        view.GetName(), surface_result.error());
      continue;
    }
    const auto& surface = surface_result.value().get();
    auto surfaces = context.GetSurfaces();
    for (size_t i = 0; i < surfaces.size(); ++i) {
      if (surfaces[i].get() == &surface) {
        context.SetSurfacePresentable(i, true);
        LOG_F(2, "Surface '{}' marked as presentable at index {}",
          surface.GetName(), i);
        break;
      }
    }
  }

  co_return;
}

//===----------------------------------------------------------------------===//
// PreExecute helper implementations
//===----------------------------------------------------------------------===//

// Removed legacy draw-metadata helpers; lifecycle now handled by
// DrawMetadataEmitter via ScenePrepState

auto Renderer::MaybeUpdateSceneConstants(const FrameContext& frame_context)
  -> void
{
  // Ensure renderer-managed fields are refreshed for this frame prior to
  // snapshot/upload. This also bumps the version when they change.
  const auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    LOG_F(ERROR, "Graphics expired while updating scene constants");
    return;
  }

  // Set frame information from FrameContext
  scene_const_cpu_.SetFrameSlot(
    frame_context.GetFrameSlot(), SceneConstants::kRenderer);
  scene_const_cpu_.SetFrameSequenceNumber(
    frame_context.GetFrameSequenceNumber(), SceneConstants::kRenderer);
  const auto current_version = scene_const_cpu_.GetVersion();
  if (scene_const_buffer_
    && current_version == last_uploaded_scene_const_version_) {
    DLOG_F(2, "MaybeUpdateSceneConstants: skipping upload (up-to-date)");
    return; // up-to-date
  }

  auto& graphics = *graphics_ptr;
  constexpr auto size_bytes = sizeof(SceneConstants::GpuData);
  if (!scene_const_buffer_) {
    const BufferDesc desc {
      .size_bytes = size_bytes,
      .usage = BufferUsage::kConstant,
      .memory = BufferMemory::kUpload,
      .debug_name = std::string("SceneConstants"),
    };
    scene_const_buffer_ = graphics.CreateBuffer(desc);
    scene_const_buffer_->SetName(desc.debug_name);
    graphics.GetResourceRegistry().Register(scene_const_buffer_);
  }
  const auto& snapshot = scene_const_cpu_.GetSnapshot();
  void* mapped = scene_const_buffer_->Map();
  std::memcpy(mapped, &snapshot, size_bytes);
  scene_const_buffer_->UnMap();
  last_uploaded_scene_const_version_ = current_version;
}

auto Renderer::WireContext(RenderContext& render_context) -> void
{
  render_context.scene_constants = scene_const_buffer_;
  // Material constants are now accessed through bindless table, not direct CBV
  const auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    throw std::runtime_error("Graphics expired in Renderer::WireContext");
  }
  render_context.SetRenderer(this, graphics_ptr.get());
}

// Removed obsolete Set/GetDrawMetadata accessors

auto Renderer::BuildFrame(const View& view, const FrameContext& frame_context)
  -> std::size_t
{
  auto scene_ptr = frame_context.GetScene();
  CHECK_NOTNULL_F(scene_ptr, "FrameContext.scene is null in BuildFrame");
  auto& scene = *scene_ptr;

  auto frame_seq = frame_context.GetFrameSequenceNumber();
  scene_prep_->Collect(scene, view, frame_seq, *scene_prep_state_, true);
  scene_prep_->Finalize();

  PublishPreparedFrameSpans();
  UpdateSceneConstantsFromView(view);

  const auto draw_count = CurrentDrawCount();
  if (draw_count == 0) {
    // No draws; frame SHOULD NOT be rendered. Render passes will find nothing
    // to do.
    skip_frame_render_ = true;
  }

  DLOG_F(2, "BuildFrame: finalized {} draws", draw_count);
  return draw_count;
}

auto Renderer::PublishPreparedFrameSpans() -> void
{
  const auto transforms = scene_prep_state_->GetTransformUploader();
  const auto world_span = transforms->GetWorldMatrices();
  prepared_frame_.world_matrices = std::span<const float>(
    reinterpret_cast<const float*>(world_span.data()), world_span.size() * 16u);

  const auto normal_span = transforms->GetNormalMatrices();
  prepared_frame_.normal_matrices
    = std::span<const float>(reinterpret_cast<const float*>(normal_span.data()),
      normal_span.size() * 16u);

  // Publish draw metadata bytes and partitions from emitter accessors
  if (auto emitter = scene_prep_state_->GetDrawMetadataEmitter()) {
    prepared_frame_.draw_metadata_bytes = emitter->GetDrawMetadataBytes();
    using PR = oxygen::engine::PreparedSceneFrame::PartitionRange;
    const auto parts = emitter->GetPartitions();
    prepared_frame_.partitions
      = std::span<const PR>(parts.data(), parts.size());
  }
}

auto Renderer::BuildFrame(const renderer::CameraView& camera_view,
  const FrameContext& frame_context) -> std::size_t
{
  const auto view = camera_view.Resolve();
  return BuildFrame(view, frame_context);
}

auto Renderer::UpdateSceneConstantsFromView(const View& view) -> void
{
  // Update scene constants from the provided view snapshot
  ModifySceneConstants([&](SceneConstants& sc) {
    sc.SetViewMatrix(view.ViewMatrix())
      .SetProjectionMatrix(view.ProjectionMatrix())
      .SetCameraPosition(view.CameraPosition());
  });
}

auto Renderer::CurrentDrawCount() const noexcept -> std::size_t
{
  return prepared_frame_.draw_metadata_bytes.size() / sizeof(DrawMetadata);
}

auto Renderer::ModifySceneConstants(
  const std::function<void(SceneConstants&)>& mutator) -> void
{
  mutator(scene_const_cpu_);
}

auto Renderer::GetSceneConstants() const -> const SceneConstants&
{
  return scene_const_cpu_;
}

auto Renderer::OnFrameStart(FrameContext& context) -> void
{
  auto tag = oxygen::renderer::internal::RendererTagFactory::Get();
  auto frame_slot = context.GetFrameSlot();

  // Initially mark the frame as renderable
  skip_frame_render_ = false;

  // Initialize Upload Coordinator and its staging providers for the new frame
  // slot BEFORE any uploaders start allocating from them.
  uploader_->OnFrameStart(tag, frame_slot);
  // then uploaders
  scene_prep_state_->GetTransformUploader()->OnFrameStart(tag, frame_slot);
  scene_prep_state_->GetGeometryUploader()->OnFrameStart(tag, frame_slot);
  scene_prep_state_->GetMaterialBinder()->OnFrameStart(tag, frame_slot);
  if (auto emitter = scene_prep_state_->GetDrawMetadataEmitter()) {
    emitter->OnFrameStart(tag, frame_slot);
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
  // Acquire scene pointer (non-owning). If absent, log once per frame in debug.
  auto scene_ptr = context.GetScene();
  if (!scene_ptr) {
    DLOG_F(
      1, "TransformsModule: no active scene set in FrameContext (skipping)");
    co_return; // Nothing to update
  }

  // Perform hierarchy propagation & world matrix updates.
  scene_ptr->Update();

  for (const auto& viewRef : context.GetViews()) {
    viewRef.get().Resolve();
  }

  co_return;
}
