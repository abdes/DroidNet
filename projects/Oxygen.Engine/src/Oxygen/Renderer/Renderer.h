//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/fwd.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewResolver.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Types/CompositingTask.h>
#include <Oxygen/Renderer/Types/SceneConstants.h>
#include <Oxygen/Renderer/Types/SunState.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
class AsyncEngine;
}

namespace oxygen::scene {
class Scene;
}

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
class Surface;
} // namespace oxygen::graphics

namespace oxygen::content {
class IAssetLoader;
}

namespace oxygen::data {
class Mesh;
class MaterialAsset;
} // namespace oxygen::data

namespace oxygen::engine {
class RenderContextPool;
class IblComputePass;
class SkyCapturePass;
struct SkyCapturePassConfig;
class CompositingPass;
struct CompositingPassConfig;
namespace internal {
  class EnvironmentDynamicDataManager;
  class EnvironmentStaticDataManager;
  class SceneConstantsManager;
  class BrdfLutManager;
  class SkyAtmosphereLutManager;
} // namespace internal
namespace upload {
  class UploadCoordinator;
  class StagingProvider;
  class InlineTransfersCoordinator;
} // namespace upload

namespace sceneprep {
  class ScenePrepState;
  class ScenePrepPipeline;
} // namespace sceneprep

} // namespace oxygen::engine

namespace oxygen::renderer {
class LightManager;
} // namespace oxygen::renderer

namespace oxygen::renderer::resources {
class GeometryUploader;
class TransformUploader;
class TextureBinder;
} // namespace oxygen::renderer::resources

namespace oxygen::engine {
namespace internal {
  class IblManager;
} // namesapce internal

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
      core::PhaseId::kRender, core::PhaseId::kCompositing,
      core::PhaseId::kFrameEnd>();
    // Note: PreRender work is performed via OnPreRender; Render work is in
    // kRender. Renderer participates in both via its module hooks.
  }

  OXGN_RNDR_NDAPI auto OnAttached(observer_ptr<AsyncEngine> engine) noexcept
    -> bool override;

  OXGN_RNDR_API auto OnShutdown() noexcept -> void override;

  OXGN_RNDR_NDAPI auto OnFrameStart(FrameContext& context) -> void override;

  OXGN_RNDR_NDAPI auto OnTransformPropagation(FrameContext& context)
    -> co::Co<> override;

  OXGN_RNDR_NDAPI auto OnPreRender(FrameContext& context) -> co::Co<> override;

  // Submit deferred uploads and retire completed ones during render phase.
  OXGN_RNDR_NDAPI auto OnRender(FrameContext& context) -> co::Co<> override;

  // Execute compositor tasks submitted during kCompositing.
  OXGN_RNDR_NDAPI auto OnCompositing(FrameContext& context)
    -> co::Co<> override;

  // Perform deferred per-frame cleanup for views that were unregistered
  // during the frame. This runs after rendering completes.
  OXGN_RNDR_NDAPI auto OnFrameEnd(FrameContext& context) -> void override;

  //! Register a view for rendering (resolver + render-graph factory).
  /*!
    A convenience API to register both the ViewResolver and the RenderGraph
    factory for a particular view id in one call. This simplifies resource
    lifetime management since the renderer can now match resolvers with
    render graphs on a per-view basis and later remove them with
    UnregisterView().

    @param view_id The unique identifier for the view
    @param resolver Callable that resolves the view using its ViewContext
    @param factory Callable that performs rendering work for this view
  */
  OXGN_RNDR_API auto RegisterView(
    ViewId view_id, ViewResolver resolver, RenderGraphFactory factory) -> void;

  //! Unregister a previously registered view.
  /*!
    Removes resolver and render graph entries and clears any cached per-view
    prepared state in the renderer. Safe to call even when the view is not
    registered.

    @param view_id The unique identifier for the view to remove
  */
  OXGN_RNDR_API auto UnregisterView(ViewId view_id) -> void;

  //! Submit compositing tasks for the current frame.
  OXGN_RNDR_API auto RegisterComposition(CompositionSubmission submission,
    std::shared_ptr<graphics::Surface> target_surface) -> void;

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

  //! Dump estimated GPU texture memory usage for debugging.
  /*!
   Logs an estimated memory breakdown for the largest textures currently
   tracked by the renderer's texture binder.

   @param top_n Number of textures to include in the sorted output.
   */
  OXGN_RNDR_API auto DumpEstimatedTextureMemory(std::size_t top_n) const
    -> void;

  //=== Upload Services ===---------------------------------------------------//

  //! Returns the staging provider for transient GPU buffer allocation.
  /*!
   Render passes should use this to allocate transient buffers that live for
   the current frame only. The provider is obtained via RenderContext during
   pass execution:

   ```cpp
   auto& staging = context.GetRenderer().GetStagingProvider();
   ```

   @return Reference to the shared staging provider.
  */
  OXGN_RNDR_NDAPI auto GetStagingProvider() -> upload::StagingProvider&;

  //! Returns the inline transfers coordinator for immediate GPU uploads.
  /*!
   Used for uploading constant buffers and small per-frame data directly into
   the command stream without deferred copy queues.

   ```cpp
   auto& transfers = context.GetRenderer().GetInlineTransfersCoordinator();
   ```

   @return Reference to the inline transfers coordinator.
  */
  OXGN_RNDR_NDAPI auto GetInlineTransfersCoordinator()
    -> upload::InlineTransfersCoordinator&;

  //! Returns the light manager for accessing scene light data.
  /*!
   Provides access to the light manager which collects scene lights and
   maintains GPU-ready structured buffers for lighting. Render passes use
   this to obtain SRV indices for positional lights during culling.

   @return Observer pointer to the light manager, or nullptr if not available.
  */
  OXGN_RNDR_NDAPI auto GetLightManager() const noexcept
    -> observer_ptr<renderer::LightManager>;

  //! Returns the sky atmosphere LUT manager.
  /*!
   Provides access to the LUT manager that maintains transmittance and sky-view
   lookup textures for physically-based atmospheric scattering. The compute
   pass uses this to dispatch LUT generation when atmosphere parameters change.

   @return Observer pointer to the LUT manager, or nullptr if not initialized.
  */
  OXGN_RNDR_NDAPI auto GetSkyAtmosphereLutManager() const noexcept
    -> observer_ptr<internal::SkyAtmosphereLutManager>;

  //! Returns the environment static data manager.
  /*!
   Provides access to the static environment data like BRDF LUTs.

   @return Observer pointer to the manager, or nullptr if not initialized.
  */
  OXGN_RNDR_NDAPI auto GetEnvironmentStaticDataManager() const noexcept
    -> observer_ptr<internal::EnvironmentStaticDataManager>;

  //! Returns the IBL manager (non-owning observer).
  OXGN_RNDR_NDAPI auto GetIblManager() const noexcept
    -> observer_ptr<internal::IblManager>;

  //! Returns the IBL compute pass.
  OXGN_RNDR_NDAPI auto GetIblComputePass() const noexcept
    -> observer_ptr<IblComputePass>;

  //=== Debug Overrides ===---------------------------------------------------//

  //! Force an IBL regeneration on the next frame.
  /*!
   This is intended for interactive debugging.
  */
  OXGN_RNDR_API auto RequestIblRegeneration() noexcept -> void;

  //! Set debug override flags for atmosphere rendering.
  /*!
   When set, these flags augment the automatically computed atmosphere flags.
   Use `AtmosphereFlags::kForceAnalytic` to disable LUT sampling.
   Use `AtmosphereFlags::kVisualizeLut` for debug visualization.

   @param flags Bitfield of AtmosphereFlags to apply.
  */
  OXGN_RNDR_API auto SetAtmosphereDebugFlags(uint32_t flags) -> void;

  //! Get current debug override flags for atmosphere rendering.
  OXGN_RNDR_NDAPI auto GetAtmosphereDebugFlags() const noexcept -> uint32_t;

  //! Override a material's UV transform used by the shader.
  /*!
   This is intended for editor and runtime authoring workflows. It updates the
   shader-visible constants for an already-registered material without
   rebuilding geometry.

    TODO: Replace this per-material override with a MaterialInstance-based API
    so per-object overrides do not require cloning MaterialAsset instances and
    do not affect other objects sharing the same material.

   @param material The material instance to update.
   @param uv_scale UV scale (tiling). Components must be finite and > 0.
   @param uv_offset UV offset. Components must be finite.
   @return true if the material was found and updated; false otherwise.
  */
  OXGN_RNDR_API auto OverrideMaterialUvTransform(
    const data::MaterialAsset& material, glm::vec2 uv_scale,
    glm::vec2 uv_offset) -> bool;

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

  // Helper extractions for OnRender to keep the main coroutine body concise.
  auto AcquireRecorderForView(ViewId view_id, Graphics& gfx)
    -> std::shared_ptr<oxygen::graphics::CommandRecorder>;

  auto SetupFramebufferForView(const FrameContext& frame_context,
    ViewId view_id, graphics::CommandRecorder& recorder,
    RenderContext& render_context) -> bool;

  auto PrepareAndWireSceneConstantsForView(ViewId view_id,
    const FrameContext& frame_context, RenderContext& render_context) -> bool;

  //! Resolves exposure for the view (manual and auto).
  auto UpdateViewExposure(ViewId view_id, const scene::Scene& scene,
    const SunState& sun_state) -> float;

  // Execute the view's render graph factory (awaits the coroutine). Returns
  // true on successful completion, false on exception.
  auto ExecuteRenderGraphForView(ViewId view_id,
    const RenderGraphFactory& factory, RenderContext& render_context,
    graphics::CommandRecorder& recorder) -> co::Co<bool>;

  std::weak_ptr<Graphics> gfx_weak_; // New AsyncEngine path

  // Managed draw item container removed (AoS path deprecated).

  // Scene constants management - uses dedicated slot-aware manager for root CBV
  // binding
  SceneConstants scene_const_cpu_;
  std::unique_ptr<internal::SceneConstantsManager> scene_const_manager_;

  // Environment dynamic data manager for root CBV at b3 (cluster slots, etc.)
  std::unique_ptr<internal::EnvironmentDynamicDataManager> env_dynamic_manager_;

  // Manages pre-integrated BRDF lookup tables for IBL.
  std::unique_ptr<internal::BrdfLutManager> brdf_lut_manager_;

  // Manages sky atmosphere LUT textures (transmittance, sky-view).
  std::unique_ptr<internal::SkyAtmosphereLutManager> sky_atmo_lut_manager_;

  // Manages Image Based Lighting (Irradiance/Prefilter)
  std::unique_ptr<internal::IblManager> ibl_manager_;

  std::unique_ptr<SkyCapturePass> sky_capture_pass_;
  std::shared_ptr<SkyCapturePassConfig> sky_capture_pass_config_;

  std::unique_ptr<IblComputePass> ibl_compute_pass_;

  // Environment static data single-owner manager (bindless SRV).
  std::unique_ptr<internal::EnvironmentStaticDataManager> env_static_manager_;

  // Persistent ScenePrep state (caches transforms/materials/geometry across
  // frames). ResetFrameData() is invoked each RunScenePrep while retaining
  // deduplicated caches inside contained managers.
  std::unique_ptr<sceneprep::ScenePrepState> scene_prep_state_;
  std::unique_ptr<sceneprep::ScenePrepPipeline> scene_prep_;

  // Frame sequence number from FrameContext
  frame::SequenceNumber frame_seq_num { 0ULL };

  float last_frame_dt_seconds_ { 1.0F / 60.0F };

  std::unordered_map<ViewId, float> auto_exposure_ev100_ {};

  // Frame slot from FrameContext (stored during OnFrameStart for RenderContext)
  frame::Slot frame_slot_ { frame::kInvalidSlot };

  // Upload coordinator: manages buffer/texture uploads and completion.
  std::unique_ptr<upload::UploadCoordinator> uploader_;
  std::shared_ptr<upload::StagingProvider> upload_staging_provider_;
  std::unique_ptr<upload::InlineTransfersCoordinator> inline_transfers_;
  std::shared_ptr<upload::StagingProvider> inline_staging_provider_;

  // Texture binding coordinator: manages texture SRV allocation and loading
  std::unique_ptr<renderer::resources::TextureBinder> texture_binder_;
  observer_ptr<content::IAssetLoader> asset_loader_;

  // View resolver and render graph registration (per-view API). Access is
  // coordinated through a shared mutex so registration can occur from UI or
  // background threads while render phases take consistent snapshots.
  mutable std::shared_mutex view_registration_mutex_;
  std::unordered_map<ViewId, ViewResolver> view_resolvers_;
  std::unordered_map<ViewId, RenderGraphFactory> render_graphs_;

  mutable std::shared_mutex view_state_mutex_;
  std::unordered_map<ViewId, bool> view_ready_states_;

  // Cache of resolved views from OnPreRender, used in OnRender to ensure
  // scene preparation and rendering use the same view state
  std::unordered_map<ViewId, ResolvedView> resolved_views_;

  std::unique_ptr<RenderContextPool> render_context_pool_;
  observer_ptr<RenderContext> render_context_ {};

  // Render Passes
  // NOTE: IBL compute generation is currently not wired in this build.
  std::shared_ptr<CompositingPass> compositing_pass_ {};
  std::shared_ptr<CompositingPassConfig> compositing_pass_config_ {};

  // Cache of prepared frames from OnPreRender, used in OnRender to ensure
  // each view renders with its own draw list (not the last view's data)
  std::unordered_map<ViewId, PreparedSceneFrame> prepared_frames_;

  std::mutex composition_mutex_ {};
  std::optional<CompositionSubmission> composition_submission_ {};
  std::shared_ptr<graphics::Surface> composition_surface_ {};

  // Pending cleanup set guarded by a mutex so arbitrary threads may
  // enqueue view ids for deferred cleanup while OnFrameEnd drains the set.
  std::mutex pending_cleanup_mutex_;
  std::unordered_set<ViewId> pending_cleanup_;

  auto DrainPendingViewCleanup(std::string_view reason) -> void;

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

  // Debug override state for sun light.
  uint32_t atmosphere_debug_flags_ { 0u };
  // Internal debug override only; no public API.
  SunState sun_override_ { kNoSun };

  std::uint64_t last_atmo_generation_ { 0 };
  bool sky_capture_requested_ { false };
};

} // namespace oxygen::engine
