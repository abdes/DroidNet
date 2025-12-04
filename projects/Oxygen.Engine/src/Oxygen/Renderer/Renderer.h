//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewResolver.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/RenderContextPool.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/Types/SceneConstants.h>
#include <Oxygen/Renderer/Upload/SceneConstantsManager.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
}

namespace oxygen::scene {
class Scene;
}

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen::data {
class Mesh;
} // namespace oxygen::data

namespace oxygen::engine {

namespace upload {
  class UploadCoordinator;
  class StagingProvider;
  class InlineTransfersCoordinator;
} // namespace upload

namespace sceneprep {
  class ScenePrepState;
  class ScenePrepPipeline;
} // namespace sceneprep

struct MaterialConstants;

//! Renderer: backend-agnostic, manages mesh-to-GPU resource mapping and
//! eviction.
class Renderer : public EngineModule {
  OXYGEN_TYPED(Renderer)

public:
  //! Type-safe render graph factory for per-view rendering.
  /*!
    A render graph factory is a callable that receives the ViewId,
    RenderContext, and CommandRecorder, and returns a coroutine that performs
    rendering work. Apps register factories for each view they want rendered.
  */
  using RenderGraphFactory = std::function<co::Co<void>(
    ViewId view_id, const RenderContext&, graphics::CommandRecorder&)>;
  // Renderer must be constructed with a valid RendererConfig containing a
  // non-empty upload_queue_key key.
  OXGN_RNDR_API explicit Renderer(
    std::weak_ptr<Graphics> graphics, RendererConfig config);

  OXYGEN_MAKE_NON_COPYABLE(Renderer)
  OXYGEN_DEFAULT_MOVABLE(Renderer)

  OXGN_RNDR_API ~Renderer() override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "RendererModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept -> ModulePriority override
  {
    // This module must run last, after all modules that may contribute to the
    // frame context.
    return ModulePriority { kModulePriorityLowest };
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> ModulePhaseMask override
  {
    // Participate in frame start, transform propagation and command record.
    return MakeModuleMask<core::PhaseId::kFrameStart,
      core::PhaseId::kTransformPropagation, core::PhaseId::kPreRender,
      core::PhaseId::kRender>();
    // Note: PreRender work is performed via OnPreRender; Render work is in
    // kRender. Renderer participates in both via its module hooks.
  }

  OXGN_RNDR_NDAPI auto OnFrameStart(FrameContext& context) -> void override;

  OXGN_RNDR_NDAPI auto OnTransformPropagation(FrameContext& context)
    -> co::Co<> override;

  OXGN_RNDR_NDAPI auto OnPreRender(FrameContext& context) -> co::Co<> override;

  // Submit deferred uploads and retire completed ones during render phase.
  OXGN_RNDR_NDAPI auto OnRender(FrameContext& context) -> co::Co<> override;

  //! Register a view resolver that maps ViewContext to ResolvedView.
  /*!
    The resolver is called during OnPreRender to obtain camera and projection
    data for each registered view. Apps should register a resolver before
    the PreRender phase begins.

    @param resolver Callable that takes ViewContext and returns ResolvedView
  */
  OXGN_RNDR_API auto RegisterViewResolver(ViewResolver resolver) -> void;

  //! Register a view-specific resolver that overrides the default resolver.
  /*!
    Allows applications to register a resolver for a specific view. When a
    view-specific resolver is registered, it takes precedence over the
    default resolver registered via RegisterViewResolver. Use this to let
    individual views manage their own camera resolution logic without
    central dispatch in the main module.

    @param view_id The unique identifier for the view
    @param resolver Callable that resolves the view using its ViewContext
  */
  OXGN_RNDR_API auto RegisterViewResolverForView(
    ViewId view_id, ViewResolver resolver) -> void;

  //! Register a render graph factory for a specific view.
  /*!
    The factory is called during OnRender to execute rendering commands for
    the specified view. Apps register factories in OnPreRender after adding
    views to the FrameContext.

    @param view_id The unique identifier for the view
    @param factory Callable that performs rendering work for this view
  */
  OXGN_RNDR_API auto RegisterRenderGraph(
    ViewId view_id, RenderGraphFactory factory) -> void;

  //! Query if a view completed rendering successfully this frame.
  /*!
    Returns true if the view was rendered without errors. Can be used by
    compositing passes to determine if a view's output is valid.

    @param view_id The unique identifier for the view
    @return true if view rendered successfully, false otherwise
  */
  OXGN_RNDR_API auto IsViewReady(ViewId view_id) const -> bool;

  // Legacy AoS accessors removed (opaque_items_ no longer drives frame build).
  // Temporary: any external caller relying on OpaqueItems()/GetOpaqueItems()
  // must migrate to PreparedSceneFrame consumption.

  //! Returns the Graphics system used by this renderer.
  OXGN_RNDR_API auto GetGraphics() -> std::shared_ptr<Graphics>;

private:
  //! Build frame data for a specific view (scene prep, culling, draw list).
  /*!\n    Internal method called by OnPreRender for each registered view.
    @param view The resolved view containing camera and projection data
    @param frame_context The current frame context
    @param run_frame_phase Whether to run the frame-phase collection
    @return Number of draws prepared for this view
  */
  OXGN_RNDR_API auto RunScenePrep(ViewId view_id, const ResolvedView& view,
    const FrameContext& frame_context, bool run_frame_phase = true)
    -> std::size_t;

  //! Update scene constants from resolved view matrices & camera state.
  auto UpdateSceneConstantsFromView(const ResolvedView& view) -> void;

  //! Publish spans into PreparedSceneFrame using TransformUploader and
  //! DrawMetadataEmitter data. The spans are non-owning and must refer to
  //! stable renderer-owned per-view backing storage. The caller must provide
  //! the view id so we can put backing bytes into per-view storage.
  auto PublishPreparedFrameSpans(
    ViewId view_id, PreparedSceneFrame& prepared_frame) -> void;

  //! Wires updated buffers into the provided render context for the frame.
  auto WireContext(RenderContext& context,
    const std::shared_ptr<graphics::Buffer>& scene_consts) -> void;

  // Cleans up a RenderContext after a run. Kept as a small helper to centralize
  // context reset behavior.
  auto PostExecute(RenderContext& context) -> void;

  // Helper extractions for OnRender to keep the main coroutine body concise.
  auto AcquireRecorderForView(ViewId view_id, Graphics& gfx)
    -> std::shared_ptr<oxygen::graphics::CommandRecorder>;

  auto SetupFramebufferForView(const FrameContext& frame_context,
    ViewId view_id, graphics::CommandRecorder& recorder,
    RenderContext& render_context) -> bool;

  auto PrepareAndWireSceneConstantsForView(ViewId view_id,
    const FrameContext& frame_context, RenderContext& render_context) -> bool;

  // Execute the view's render graph factory (awaits the coroutine). Returns
  // true on successful completion, false on exception.
  auto ExecuteRenderGraphForView(ViewId view_id,
    const RenderGraphFactory& factory, RenderContext& render_context,
    graphics::CommandRecorder& recorder) -> co::Co<bool>;

  // Set final state for a view after render (success or failure) and emit
  // logs.
  auto FinalizeViewState(ViewId view_id, bool success) -> void;

  std::weak_ptr<Graphics> gfx_weak_; // New AsyncEngine path

  // Managed draw item container removed (AoS path deprecated).

  // Scene constants management - uses dedicated slot-aware manager for root CBV
  // binding
  SceneConstants scene_const_cpu_;
  std::unique_ptr<upload::SceneConstantsManager> scene_const_manager_;

  // Persistent ScenePrep state (caches transforms/materials/geometry across
  // frames). ResetFrameData() is invoked each RunScenePrep while retaining
  // deduplicated caches inside contained managers.
  std::unique_ptr<sceneprep::ScenePrepState> scene_prep_state_;
  std::unique_ptr<sceneprep::ScenePrepPipeline> scene_prep_;

  // Frame sequence number from FrameContext
  frame::SequenceNumber frame_seq_num { 0ULL };

  // Upload coordinator: manages buffer/texture uploads and completion.
  std::unique_ptr<upload::UploadCoordinator> uploader_;
  std::shared_ptr<upload::StagingProvider> upload_staging_provider_;
  std::unique_ptr<upload::InlineTransfersCoordinator> inline_transfers_;
  std::shared_ptr<upload::StagingProvider> inline_staging_provider_;

  // View resolver and render graph registration (new streamlined API)
  ViewResolver view_resolver_;
  std::unordered_map<ViewId, ViewResolver> view_resolvers_;
  std::unordered_map<ViewId, RenderGraphFactory> render_graphs_;
  std::unordered_map<ViewId, bool> view_ready_states_;

  // Cache of resolved views from OnPreRender, used in OnRender to ensure
  // scene preparation and rendering use the same view state
  std::unordered_map<ViewId, ResolvedView> resolved_views_;

  std::unique_ptr<RenderContextPool> render_context_pool_;
  observer_ptr<RenderContext> render_context_ {};

  // Tracks whether a context was successfully acquired for a given frame
  // slot during OnPreRender. If false and a render graph exists for the
  // frame, the frame should be dropped (no OnRender work will be run for
  // that slot).
  // NOTE: we now use `render_context_pool_` as the single source of truth
  // for whether a per-frame RenderContext has been claimed for this frame
  // slot. The in-use flag is set atomically on Acquire and cleared on
  // Release; OnRender observes the in-use state to decide whether to run
  // render work for the slot.

  // Cache of prepared frames from OnPreRender, used in OnRender to ensure
  // each view renders with its own draw list (not the last view's data)
  std::unordered_map<ViewId, PreparedSceneFrame> prepared_frames_;

  // Per-view stable backing storage for the non-owning spans exposed by
  // PreparedSceneFrame. Each view gets a collection of vectors that hold the
  // raw bytes, partitions and matrix arrays so PreparedSceneFrame spans can
  // point into them safely without risk of being invalidated by the
  // DrawMetadataEmitter or TransformUploader when those mutate.
  struct PerViewStorage {
    std::vector<std::byte> draw_metadata_storage;
    std::vector<PreparedSceneFrame::PartitionRange> partition_storage;
    std::vector<float> world_matrix_storage;
    std::vector<float> normal_matrix_storage;
  };

  std::unordered_map<ViewId, PerViewStorage> per_view_storage_;
};

} // namespace oxygen::engine
