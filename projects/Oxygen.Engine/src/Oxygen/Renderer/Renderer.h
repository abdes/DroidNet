//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
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
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Renderer/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Renderer/Pipeline/CompositionView.h>
#include <Oxygen/Renderer/Pipeline/RendererCapability.h>
#include <Oxygen/Renderer/Pipeline/RenderingPipeline.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemData.h>
#include <Oxygen/Renderer/Types/CompositingTask.h>
#include <Oxygen/Renderer/Types/DebugFrameBindings.h>
#include <Oxygen/Renderer/Types/DrawFrameBindings.h>
#include <Oxygen/Renderer/Types/EnvironmentFrameBindings.h>
#include <Oxygen/Renderer/Types/EnvironmentViewData.h>
#include <Oxygen/Renderer/Types/LightCullingConfig.h>
#include <Oxygen/Renderer/Types/LightingFrameBindings.h>
#include <Oxygen/Renderer/Types/ShadowFrameBindings.h>
#include <Oxygen/Renderer/Types/SyntheticSunData.h>
#include <Oxygen/Renderer/Types/ViewColorData.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/Types/ViewFrameBindings.h>
#include <Oxygen/Renderer/Types/VsmFrameBindings.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
class IAsyncEngine;
}

namespace oxygen::scene {
class Scene;
}

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
class Surface;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::content {
class IAssetLoader;
}

namespace oxygen::data {
class Mesh;
class MaterialAsset;
} // namespace oxygen::data

namespace oxygen::engine::upload {
class TransientStructuredBuffer;
}

namespace oxygen::engine {
class RenderContextPool;
class EnvironmentLightingService;
namespace imgui {
  class GpuTimelinePanel;
  class ImGuiModule;
}
class IblComputePass;
class SkyCapturePass;
struct SkyCapturePassConfig;
class SkyAtmosphereLutComputePass;
struct SkyAtmosphereLutComputePassConfig;
class CompositingPass;
struct CompositingPassConfig;
namespace internal {
  class EnvironmentStaticDataManager;
  class ViewConstantsManager;
  class BrdfLutManager;
  class SkyAtmosphereLutManager;
  class GpuDebugManager;
  class GpuTimelineProfiler;
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
class ShadowManager;
class RenderingPipeline;
} // namespace oxygen::renderer

namespace oxygen::renderer::resources {
class GeometryUploader;
class TransformUploader;
class TextureBinder;
} // namespace oxygen::renderer::resources

namespace oxygen::engine {
namespace internal {
  class IblManager;
  class RenderContextMaterializer;
} // namesapce internal

struct MaterialShadingConstants;

//! Renderer: backend-agnostic, manages mesh-to-GPU resource mapping and
//! eviction.
class Renderer : public EngineModule {
  OXYGEN_TYPED(Renderer)

public:
  struct ValidationIssue {
    std::string code;
    std::string message;
  };

  struct ValidationReport {
    std::vector<ValidationIssue> issues;

    [[nodiscard]] auto Ok() const noexcept -> bool { return issues.empty(); }
  };

  struct FrameSessionInput {
    frame::Slot frame_slot { frame::kInvalidSlot };
    frame::SequenceNumber frame_sequence { 1U };
    float delta_time_seconds { 1.0F / 60.0F };
    observer_ptr<scene::Scene> scene { nullptr };
  };

  struct OutputTargetInput {
    observer_ptr<const graphics::Framebuffer> framebuffer { nullptr };
  };

  struct ResolvedViewInput {
    ViewId view_id {};
    ResolvedView value;
  };

  struct PreparedFrameInput {
    PreparedSceneFrame value {};
  };

  struct CoreShaderInputsInput {
    ViewId view_id {};
    ViewConstants value {};
  };

  class ValidatedSinglePassHarnessContext {
  public:
    OXGN_RNDR_API ~ValidatedSinglePassHarnessContext();

    OXYGEN_MAKE_NON_COPYABLE(ValidatedSinglePassHarnessContext)
    OXGN_RNDR_API ValidatedSinglePassHarnessContext(
      ValidatedSinglePassHarnessContext&& other) noexcept;
    OXGN_RNDR_API auto operator=(
      ValidatedSinglePassHarnessContext&& other) noexcept
      -> ValidatedSinglePassHarnessContext&;

    [[nodiscard]] auto GetRenderContext() noexcept -> RenderContext&
    {
      return render_context_;
    }

    [[nodiscard]] auto GetRenderContext() const noexcept -> const RenderContext&
    {
      return render_context_;
    }

  private:
    friend class internal::RenderContextMaterializer;

    OXGN_RNDR_API ValidatedSinglePassHarnessContext(Renderer& renderer,
      FrameSessionInput session, ViewId view_id,
      observer_ptr<const graphics::Framebuffer> pass_target,
      const ResolvedView& resolved_view,
      const PreparedSceneFrame& prepared_frame,
      std::optional<ViewConstants> core_shader_inputs);

    auto RebindCurrentViewPointers() noexcept -> void;
    auto Release() noexcept -> void;

    observer_ptr<Renderer> renderer_ { nullptr };
    RenderContext render_context_ {};
    std::optional<ResolvedView> current_resolved_view_ {};
    std::optional<PreparedSceneFrame> current_prepared_frame_ {};
    bool active_ { false };
  };

  class SinglePassHarnessFacade {
  public:
    OXGN_RNDR_API explicit SinglePassHarnessFacade(Renderer& renderer) noexcept;

    OXYGEN_MAKE_NON_COPYABLE(SinglePassHarnessFacade)
    OXYGEN_DEFAULT_MOVABLE(SinglePassHarnessFacade)

    OXGN_RNDR_API auto SetFrameSession(FrameSessionInput session)
      -> SinglePassHarnessFacade&;
    OXGN_RNDR_API auto SetOutputTarget(OutputTargetInput target)
      -> SinglePassHarnessFacade&;
    OXGN_RNDR_API auto SetResolvedView(ResolvedViewInput view)
      -> SinglePassHarnessFacade&;
    OXGN_RNDR_API auto SetPreparedFrame(PreparedFrameInput frame)
      -> SinglePassHarnessFacade&;
    OXGN_RNDR_API auto SetCoreShaderInputs(CoreShaderInputsInput inputs)
      -> SinglePassHarnessFacade&;

    [[nodiscard]] OXGN_RNDR_API auto CanFinalize() const -> bool;
    OXGN_RNDR_API auto Validate() const -> ValidationReport;
    OXGN_RNDR_API auto Finalize()
      -> std::expected<ValidatedSinglePassHarnessContext, ValidationReport>;

  private:
    observer_ptr<Renderer> renderer_ { nullptr };
    std::optional<FrameSessionInput> frame_session_ {};
    std::optional<OutputTargetInput> output_target_ {};
    std::optional<ResolvedViewInput> resolved_view_ {};
    std::optional<PreparedFrameInput> prepared_frame_ {};
    std::optional<CoreShaderInputsInput> core_shader_inputs_ {};
  };

  using RenderGraphHarnessInput = std::function<co::Co<void>(
    ViewId view_id, const RenderContext&, graphics::CommandRecorder&)>;

  class ValidatedRenderGraphHarness {
  public:
    ValidatedRenderGraphHarness(ValidatedSinglePassHarnessContext context,
      RenderGraphHarnessInput graph, ViewId view_id)
      : context_(std::move(context))
      , graph_(std::move(graph))
      , view_id_(view_id)
    {
    }

    OXYGEN_MAKE_NON_COPYABLE(ValidatedRenderGraphHarness)
    OXYGEN_DEFAULT_MOVABLE(ValidatedRenderGraphHarness)

    [[nodiscard]] auto GetRenderContext() noexcept -> RenderContext&
    {
      return context_.GetRenderContext();
    }

    [[nodiscard]] auto GetRenderContext() const noexcept -> const RenderContext&
    {
      return context_.GetRenderContext();
    }

    [[nodiscard]] auto GetViewId() const noexcept -> ViewId { return view_id_; }

    auto Execute(graphics::CommandRecorder& recorder) const -> co::Co<void>
    {
      auto& render_context = context_.GetRenderContext();
      render_context.ClearRegisteredPasses();
      co_await graph_(view_id_, render_context, recorder);
    }

  private:
    ValidatedSinglePassHarnessContext context_;
    RenderGraphHarnessInput graph_;
    ViewId view_id_ {};
  };

  class RenderGraphHarnessFacade {
  public:
    OXGN_RNDR_API explicit RenderGraphHarnessFacade(
      Renderer& renderer) noexcept;

    OXYGEN_MAKE_NON_COPYABLE(RenderGraphHarnessFacade)
    OXYGEN_DEFAULT_MOVABLE(RenderGraphHarnessFacade)

    OXGN_RNDR_API auto SetFrameSession(FrameSessionInput session)
      -> RenderGraphHarnessFacade&;
    OXGN_RNDR_API auto SetOutputTarget(OutputTargetInput target)
      -> RenderGraphHarnessFacade&;
    OXGN_RNDR_API auto SetResolvedView(ResolvedViewInput view)
      -> RenderGraphHarnessFacade&;
    OXGN_RNDR_API auto SetPreparedFrame(PreparedFrameInput frame)
      -> RenderGraphHarnessFacade&;
    OXGN_RNDR_API auto SetCoreShaderInputs(CoreShaderInputsInput inputs)
      -> RenderGraphHarnessFacade&;
    OXGN_RNDR_API auto SetRenderGraph(RenderGraphHarnessInput graph)
      -> RenderGraphHarnessFacade&;

    [[nodiscard]] OXGN_RNDR_API auto CanFinalize() const -> bool;
    OXGN_RNDR_API auto Validate() const -> ValidationReport;
    OXGN_RNDR_API auto Finalize()
      -> std::expected<ValidatedRenderGraphHarness, ValidationReport>;

  private:
    observer_ptr<Renderer> renderer_ { nullptr };
    std::optional<FrameSessionInput> frame_session_ {};
    std::optional<OutputTargetInput> output_target_ {};
    std::optional<ResolvedViewInput> resolved_view_ {};
    std::optional<PreparedFrameInput> prepared_frame_ {};
    std::optional<CoreShaderInputsInput> core_shader_inputs_ {};
    std::optional<RenderGraphHarnessInput> render_graph_ {};
  };

  struct SceneSourceInput {
    observer_ptr<scene::Scene> scene { nullptr };
  };

  class OffscreenSceneViewInput {
  public:
    OXGN_RNDR_API OffscreenSceneViewInput();

    [[nodiscard]] OXGN_RNDR_API static auto FromCamera(std::string name,
      ViewId view_id, const View& view, const scene::SceneNode& camera)
      -> OffscreenSceneViewInput;

    OXGN_RNDR_API auto SetWithAtmosphere(bool enabled)
      -> OffscreenSceneViewInput&;
    OXGN_RNDR_API auto SetClearColor(const graphics::Color& clear_color)
      -> OffscreenSceneViewInput&;
    OXGN_RNDR_API auto SetForceWireframe(bool enabled)
      -> OffscreenSceneViewInput&;
    OXGN_RNDR_API auto SetExposureSourceViewId(ViewId view_id)
      -> OffscreenSceneViewInput&;

    [[nodiscard]] auto ViewIntent() const noexcept
      -> const renderer::CompositionView&
    {
      return composition_view_;
    }

  private:
    auto SyncName() noexcept -> void;

    std::string name_storage_ { "OffscreenScene" };
    renderer::CompositionView composition_view_ {};
  };

  struct OffscreenPipelineInput {
    [[nodiscard]] static auto Borrowed(
      observer_ptr<renderer::RenderingPipeline> pipeline)
      -> OffscreenPipelineInput
    {
      return OffscreenPipelineInput { .borrowed_pipeline = pipeline };
    }

    [[nodiscard]] static auto Owned(
      std::unique_ptr<renderer::RenderingPipeline> pipeline)
      -> OffscreenPipelineInput
    {
      return OffscreenPipelineInput { .owned_pipeline = std::move(pipeline) };
    }

    observer_ptr<renderer::RenderingPipeline> borrowed_pipeline { nullptr };
    std::unique_ptr<renderer::RenderingPipeline> owned_pipeline {};
  };

  class ValidatedOffscreenSceneSession {
  public:
    ValidatedOffscreenSceneSession(Renderer& renderer,
      FrameSessionInput frame_session, SceneSourceInput scene_source,
      OffscreenSceneViewInput view_intent, OutputTargetInput output_target,
      OffscreenPipelineInput pipeline);

    OXYGEN_MAKE_NON_COPYABLE(ValidatedOffscreenSceneSession)
    OXYGEN_DEFAULT_MOVABLE(ValidatedOffscreenSceneSession)

    [[nodiscard]] auto GetViewId() const noexcept -> ViewId
    {
      return view_intent_.ViewIntent().id;
    }

    [[nodiscard]] auto GetPipeline() const noexcept
      -> observer_ptr<renderer::RenderingPipeline>
    {
      return pipeline_;
    }

    OXGN_RNDR_API auto Execute() -> co::Co<void>;

  private:
    [[nodiscard]] auto MakeCompositionView() const -> renderer::CompositionView;

    observer_ptr<Renderer> renderer_ { nullptr };
    FrameSessionInput frame_session_ {};
    SceneSourceInput scene_source_ {};
    OffscreenSceneViewInput view_intent_ {};
    OutputTargetInput output_target_ {};
    observer_ptr<renderer::RenderingPipeline> pipeline_ { nullptr };
    std::unique_ptr<renderer::RenderingPipeline> owned_pipeline_ {};
  };

  class OffscreenSceneFacade {
  public:
    OXGN_RNDR_API explicit OffscreenSceneFacade(Renderer& renderer) noexcept;

    OXYGEN_MAKE_NON_COPYABLE(OffscreenSceneFacade)
    OXYGEN_DEFAULT_MOVABLE(OffscreenSceneFacade)

    OXGN_RNDR_API auto SetFrameSession(FrameSessionInput session)
      -> OffscreenSceneFacade&;
    OXGN_RNDR_API auto SetSceneSource(SceneSourceInput scene)
      -> OffscreenSceneFacade&;
    OXGN_RNDR_API auto SetViewIntent(OffscreenSceneViewInput view)
      -> OffscreenSceneFacade&;
    OXGN_RNDR_API auto SetOutputTarget(OutputTargetInput target)
      -> OffscreenSceneFacade&;
    OXGN_RNDR_API auto SetPipeline(OffscreenPipelineInput pipeline)
      -> OffscreenSceneFacade&;

    [[nodiscard]] OXGN_RNDR_API auto CanFinalize() const -> bool;
    OXGN_RNDR_API auto Validate() const -> ValidationReport;
    OXGN_RNDR_API auto Finalize()
      -> std::expected<ValidatedOffscreenSceneSession, ValidationReport>;

  private:
    observer_ptr<Renderer> renderer_ { nullptr };
    std::optional<FrameSessionInput> frame_session_ {};
    std::optional<SceneSourceInput> scene_source_ {};
    std::optional<OffscreenSceneViewInput> view_intent_ {};
    std::optional<OutputTargetInput> output_target_ {};
    std::optional<OffscreenPipelineInput> pipeline_ {};
  };

  struct LastFrameStats {
    double sceneprep_ms { 0.0 };
    double view_render_ms { 0.0 };
    double render_graph_ms { 0.0 };
    double env_update_ms { 0.0 };
    double compositing_ms { 0.0 };
    std::uint32_t views { 0 };
    std::uint32_t scene_views { 0 };
  };

  struct Stats {
    double sceneprep_avg_ms { 0.0 };
    double avg_view_render_ms { 0.0 };
    double avg_render_graph_ms { 0.0 };
    double avg_env_update_ms { 0.0 };
    double avg_compositing_ms { 0.0 };
    LastFrameStats last_frame {};
  };

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
  OXGN_RNDR_API explicit Renderer(std::weak_ptr<Graphics> graphics,
    RendererConfig config, renderer::CapabilitySet capability_families);

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
    return kRendererModulePriority;
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

  OXGN_RNDR_NDAPI auto OnAttached(observer_ptr<IAsyncEngine> engine) noexcept
    -> bool override;
  OXGN_RNDR_API auto RegisterConsoleBindings(
    observer_ptr<console::Console> console) noexcept -> void override;
  OXGN_RNDR_API auto ApplyConsoleCVars(
    observer_ptr<const console::Console> console) noexcept -> void override;

  OXGN_RNDR_API auto OnShutdown() noexcept -> void override;

  OXGN_RNDR_API auto OnFrameStart(observer_ptr<FrameContext> context)
    -> void override;

  OXGN_RNDR_NDAPI auto OnTransformPropagation(
    observer_ptr<FrameContext> context) -> co::Co<> override;

  OXGN_RNDR_NDAPI auto OnPreRender(observer_ptr<FrameContext> context)
    -> co::Co<> override;

  // Submit deferred uploads and retire completed ones during render phase.
  OXGN_RNDR_NDAPI auto OnRender(observer_ptr<FrameContext> context)
    -> co::Co<> override;

  // Execute compositor tasks submitted during kCompositing.
  OXGN_RNDR_NDAPI auto OnCompositing(observer_ptr<FrameContext> context)
    -> co::Co<> override;

  // Perform deferred per-frame cleanup for views that were unregistered
  // during the frame. This runs after rendering completes.
  OXGN_RNDR_API auto OnFrameEnd(observer_ptr<FrameContext> context)
    -> void override;

  //! Register a view render graph for this frame.
  /*!
    Publishes both the render graph factory and resolved view snapshot.
    Call this during frame preparation for each view that should render.

    @param view_id The unique identifier for the view
    @param factory Callable that performs rendering work for this view
    @param view Resolved per-frame camera/view snapshot
  */
  OXGN_RNDR_API auto RegisterViewRenderGraph(
    ViewId view_id, RenderGraphFactory factory, ResolvedView view) -> void;

  //! Upsert the published runtime view for a pipeline-authored intent view id.
  OXGN_RNDR_API auto UpsertPublishedRuntimeView(FrameContext& frame_context,
    ViewId intent_view_id, ViewContext view) -> ViewId;
  [[nodiscard]] OXGN_RNDR_NDAPI auto ResolvePublishedRuntimeViewId(
    ViewId intent_view_id) const noexcept -> ViewId;
  OXGN_RNDR_API auto RemovePublishedRuntimeView(
    FrameContext& frame_context, ViewId intent_view_id) -> void;
  OXGN_RNDR_API auto PruneStalePublishedRuntimeViews(
    FrameContext& frame_context) -> std::vector<ViewId>;

  //! Unregister a previously published view render graph.
  /*!
    Removes render graph entries and clears any cached per-view prepared
    state
    in the renderer. Safe to call even when the view is not
    registered.

    @param view_id The unique identifier for the view to remove
  */
  OXGN_RNDR_API auto UnregisterViewRenderGraph(ViewId view_id) -> void;

  //! Submit compositing tasks for the current frame.
  /*!
    Phase 1 accepts multiple submissions per frame, but they must all
   * target
    the same composite framebuffer/surface pair. `target_surface` is
   * renderer
    bookkeeping for presentability integration;
   * `submission.composite_target`
    remains the execution framebuffer
   * handoff.
  */
  OXGN_RNDR_API auto RegisterComposition(CompositionSubmission submission,
    std::shared_ptr<graphics::Surface> target_surface) -> void;
  OXGN_RNDR_API auto ForSinglePassHarness() -> SinglePassHarnessFacade;
  OXGN_RNDR_API auto ForRenderGraphHarness() -> RenderGraphHarnessFacade;
  OXGN_RNDR_API auto ForOffscreenScene() -> OffscreenSceneFacade;

  //! Returns a point-in-time renderer stats snapshot.
  OXGN_RNDR_NDAPI auto GetStats() const noexcept -> Stats;
  //! Resets renderer stats accumulators.
  OXGN_RNDR_API auto ResetStats() noexcept -> void;

  [[nodiscard]] auto GetCapabilityFamilies() const noexcept
    -> renderer::CapabilitySet
  {
    return capability_families_;
  }

  [[nodiscard]] auto HasCapability(
    const renderer::RendererCapabilityFamily family) const noexcept -> bool
  {
    return renderer::HasAllCapabilities(capability_families_, family);
  }

  [[nodiscard]] auto ValidateCapabilityRequirements(
    const renderer::PipelineCapabilityRequirements requirements) const noexcept
    -> renderer::PipelineCapabilityValidation
  {
    return renderer::ValidateCapabilityRequirements(
      capability_families_, requirements);
  }

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

  OXGN_RNDR_NDAPI auto GetShadowManager() const noexcept
    -> observer_ptr<renderer::ShadowManager>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto
  GetConventionalShadowDepthTexture() const noexcept
    -> std::shared_ptr<graphics::Texture>;
  OXGN_RNDR_API auto ExecuteCurrentViewVirtualShadowShell(
    const RenderContext& render_context, graphics::CommandRecorder& recorder,
    observer_ptr<const graphics::Texture> scene_depth_texture) -> co::Co<>;

  OXGN_RNDR_NDAPI auto GetSkyAtmosphereLutManagerForView(
    ViewId view_id) const noexcept
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

  struct CurrentViewDynamicBindingsUpdate {
    std::optional<LightCullingConfig> light_culling {};
    std::optional<VsmFrameBindings> virtual_shadow {};
  };

  //! Update renderer-owned dynamic bindings for the active view and republish
  //! only the current view's dynamic system bindings.
  OXGN_RNDR_API auto UpdateCurrentViewDynamicBindings(
    const RenderContext& render_context,
    const CurrentViewDynamicBindingsUpdate& update) -> void;

  //=== Debug Overrides ===---------------------------------------------------//

  //! Force an IBL regeneration on the next frame.
  /*!
   This is intended for interactive debugging.
  */
  OXGN_RNDR_API auto RequestIblRegeneration() noexcept -> void;

  //! Force a sky capture refresh on the next frame.
  OXGN_RNDR_API auto RequestSkyCapture() noexcept -> void;

  //! Enables/disables blue-noise jitter for atmosphere LUT generation.
  OXGN_RNDR_API auto SetAtmosphereBlueNoiseEnabled(bool enabled) noexcept
    -> void;

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
  enum class ViewBindingRepublishMode : std::uint8_t {
    kFull,
    kDynamicSystemBindings,
  };

  struct PerViewRuntimeState {
    LightCullingConfig light_culling {};
    SyntheticSunData sun { kNoSun };
    EnvironmentViewData environment_view {};
    VsmFrameBindings virtual_shadow {};
    ViewFrameBindings published_view_bindings {};
    ShaderVisibleIndex scene_depth_srv { kInvalidShaderVisibleIndex };
    const graphics::Texture* scene_depth_texture_owner { nullptr };
    bool owns_scene_depth_srv { false };
    bool has_published_view_bindings { false };
  };

  //! Build frame data for a specific view (scene prep, culling, draw list).
  /*!\n    Internal method called by OnPreRender for each registered view.
    @param view The resolved view containing camera and projection data
    @param frame_context The current frame context
    @param run_frame_phase Whether to run the frame-phase collection
    @param single_view_mode Whether this frame is operating with exactly one
           active view and can use the fused single-view collect path.
    @return Number of draws prepared for this view
  */
  OXGN_RNDR_API auto RunScenePrep(ViewId view_id, const ResolvedView& view,
    const FrameContext& frame_context, bool run_frame_phase = true,
    bool single_view_mode = false) -> std::size_t;

  //! Update `ViewConstants` from resolved view matrices and camera state.
  auto UpdateViewConstantsFromView(const ResolvedView& view) -> void;

  //! Publish spans into PreparedSceneFrame using TransformUploader and
  //! DrawMetadataEmitter data. The spans are non-owning and must refer to
  //! stable renderer-owned per-view backing storage. The caller must provide
  //! the view id so we can put backing bytes into per-view storage.
  auto PublishPreparedFrameSpans(
    ViewId view_id, PreparedSceneFrame& prepared_frame) -> void;
  auto PublishConventionalShadowDrawRecords(
    ViewId view_id, PreparedSceneFrame& prepared_frame) -> void;

  //! Wires updated buffers into the provided render context for the frame.
  auto WireContext(RenderContext& context,
    const std::shared_ptr<graphics::Buffer>& view_constants) -> void;
  auto EnsureOffscreenFrameServicesInitialized() -> void;
  auto EnsureShadowServicesInitialized(observer_ptr<Graphics> gfx) -> void;
  auto EnsureConventionalShadowDrawRecordBufferInitialized(
    observer_ptr<Graphics> gfx) -> void;
  auto BeginStandaloneFrameExecution(const FrameSessionInput& session) -> void;
  auto InitializeStandaloneCurrentView(RenderContext& render_context,
    std::optional<ResolvedView>& current_resolved_view,
    std::optional<PreparedSceneFrame>& current_prepared_frame, ViewId view_id,
    const ResolvedView& resolved_view, const PreparedSceneFrame& prepared_frame,
    const std::optional<ViewConstants>& view_constants_override) -> void;
  auto BeginFrameServices(
    frame::Slot frame_slot, frame::SequenceNumber frame_sequence) -> void;
  auto EndOffscreenFrame() noexcept -> void;

  // Helper extractions for OnRender to keep the main coroutine body concise.
  auto AcquireRecorderForView(ViewId view_id, Graphics& gfx)
    -> std::shared_ptr<oxygen::graphics::CommandRecorder>;

  auto SetupFramebufferForView(const FrameContext& frame_context,
    ViewId view_id, graphics::CommandRecorder& recorder,
    RenderContext& render_context) -> bool;

  auto PrepareAndWireViewConstantsForView(ViewId view_id,
    const FrameContext& frame_context, RenderContext& render_context) -> bool;
  auto RepublishCurrentViewBindings(const RenderContext& render_context,
    ViewBindingRepublishMode mode = ViewBindingRepublishMode::kFull) -> bool;
  auto PublishBaselineViewBindings(ViewId view_id,
    const RenderContext& render_context, const PreparedSceneFrame& prepared,
    PerViewRuntimeState& runtime_state, bool can_reuse_cached_view_bindings,
    ViewFrameBindings& view_bindings, ViewConstants& view_constants) -> bool;
  auto PublishOptionalFamilyViewBindings(ViewId view_id,
    const RenderContext& render_context, const ResolvedView& resolved,
    const PreparedSceneFrame& prepared,
    ShaderVisibleIndex environment_static_slot,
    PerViewRuntimeState& runtime_state, bool can_reuse_cached_view_bindings,
    ViewFrameBindings& view_bindings, ViewConstants& view_constants) -> void;
  auto EnsureSceneDepthTextureSrv(PerViewRuntimeState& runtime_state,
    const graphics::Texture& depth_texture) -> ShaderVisibleIndex;

  //! Resolves exposure for the view (manual and auto).
  auto UpdateViewExposure(ViewId view_id, const scene::Scene& scene,
    const SyntheticSunData& sun_state) -> float;

  // Execute the view's render graph factory (awaits the coroutine). Returns
  // true on successful completion, false on exception.
  auto ExecuteRenderGraphForView(ViewId view_id,
    const RenderGraphFactory& factory, RenderContext& render_context,
    graphics::CommandRecorder& recorder) -> co::Co<bool>;
  auto EvictPerViewCachedProducts(ViewId view_id) -> void;
  auto EvictInactivePerViewState(frame::SequenceNumber current_seq,
    const std::unordered_set<ViewId>& active_views) -> void;

  std::weak_ptr<Graphics> gfx_weak_; // New AsyncEngine path
  observer_ptr<IAsyncEngine> engine_ { nullptr };
  RendererConfig config_ {};

  // Managed draw item container removed (AoS path deprecated).

  // ViewConstants management - uses a dedicated slot-aware manager for root CBV
  // binding.
  ViewConstants view_const_cpu_;
  std::unique_ptr<internal::ViewConstantsManager> view_const_manager_;
  std::unique_ptr<internal::PerViewStructuredPublisher<ViewFrameBindings>>
    view_frame_bindings_publisher_;
  std::unique_ptr<internal::PerViewStructuredPublisher<DrawFrameBindings>>
    draw_frame_bindings_publisher_;
  std::unique_ptr<internal::PerViewStructuredPublisher<ViewColorData>>
    view_color_data_publisher_;
  std::unique_ptr<internal::PerViewStructuredPublisher<DebugFrameBindings>>
    debug_frame_bindings_publisher_;
  std::unique_ptr<upload::TransientStructuredBuffer>
    conventional_shadow_draw_record_buffer_;
  std::unique_ptr<internal::PerViewStructuredPublisher<LightingFrameBindings>>
    lighting_frame_bindings_publisher_;
  std::unique_ptr<renderer::ShadowManager> shadow_manager_;
  std::unique_ptr<internal::PerViewStructuredPublisher<ShadowFrameBindings>>
    shadow_frame_bindings_publisher_;
  std::unique_ptr<internal::PerViewStructuredPublisher<VsmFrameBindings>>
    vsm_frame_bindings_publisher_;
  std::unique_ptr<EnvironmentLightingService> env_lighting_service_;
  std::unordered_map<ViewId, PerViewRuntimeState> per_view_runtime_state_;

  // Persistent ScenePrep state (caches transforms/materials/geometry across
  // frames). ResetFrameData() is invoked each RunScenePrep while retaining
  // deduplicated caches inside contained managers.
  std::unique_ptr<sceneprep::ScenePrepState> scene_prep_state_;
  std::unique_ptr<sceneprep::ScenePrepPipeline> scene_prep_;

  // Manages GPU debug resources (line buffer and counters).
  std::unique_ptr<internal::GpuDebugManager> gpu_debug_manager_;
  std::unique_ptr<internal::GpuTimelineProfiler> gpu_timeline_profiler_;
  std::unique_ptr<imgui::GpuTimelinePanel> gpu_timeline_panel_;
  IAsyncEngine::ModuleSubscription imgui_module_subscription_ {};
  std::uint64_t gpu_timeline_panel_drawer_token_ { 0U };

  // Frame sequence number from FrameContext
  frame::SequenceNumber frame_seq_num { 0ULL };

  float last_frame_dt_seconds_ { 1.0F / 60.0F };

  // Frame slot from FrameContext (stored during OnFrameStart for RenderContext)
  frame::Slot frame_slot_ { frame::kInvalidSlot };
  FrameContext::BudgetStats frame_budget_stats_ {};

  // Upload coordinator: manages buffer/texture uploads and completion.
  std::unique_ptr<upload::UploadCoordinator> uploader_;
  std::shared_ptr<upload::StagingProvider> upload_staging_provider_;
  std::unique_ptr<upload::InlineTransfersCoordinator> inline_transfers_;
  std::shared_ptr<upload::StagingProvider> inline_staging_provider_;

  // Texture binding coordinator: manages texture SRV allocation and loading
  std::unique_ptr<renderer::resources::TextureBinder> texture_binder_;
  observer_ptr<content::IAssetLoader> asset_loader_;

  // Render graph registration (per-view API). Access is
  // coordinated through a shared mutex so registration can occur from UI or
  // background threads while render phases take consistent snapshots.
  mutable std::shared_mutex view_registration_mutex_;
  std::unordered_map<ViewId, RenderGraphFactory> render_graphs_;

  mutable std::shared_mutex view_state_mutex_;
  struct PublishedRuntimeViewState {
    ViewId published_view_id { kInvalidViewId };
    frame::SequenceNumber last_seen_frame { 0U };
  };
  static constexpr frame::SequenceNumber kPublishedRuntimeViewMaxIdleFrames {
    60
  };
  std::unordered_map<ViewId, bool> view_ready_states_;
  std::unordered_map<ViewId, PublishedRuntimeViewState>
    published_runtime_views_by_intent_;

  // Cache of resolved views published for the current frame before OnPreRender.
  // Used by OnPreRender/OnRender to ensure scene prep and rendering use the
  // same per-view snapshot.
  std::unordered_map<ViewId, ResolvedView> resolved_views_;

  std::unique_ptr<RenderContextPool> render_context_pool_;
  observer_ptr<RenderContext> render_context_;

  // Render Passes
  // NOTE: IBL compute generation is currently not wired in this build.
  std::shared_ptr<CompositingPass> compositing_pass_;
  std::shared_ptr<CompositingPassConfig> compositing_pass_config_;

  // Cache of prepared frames from OnPreRender, used in OnRender to ensure
  // each view renders with its own draw list (not the last view's data)
  std::unordered_map<ViewId, PreparedSceneFrame> prepared_frames_;

  struct PendingComposition {
    CompositionSubmission submission {};
    std::shared_ptr<graphics::Surface> target_surface {};
    std::uint64_t sequence_in_frame { 0 };
  };

  std::mutex composition_mutex_;
  std::vector<PendingComposition> pending_compositions_;
  std::uint64_t next_composition_sequence_in_frame_ { 0 };
  renderer::CapabilitySet capability_families_ {
    renderer::kPhase1DefaultRuntimeCapabilityFamilies
  };

  // Pending cleanup set guarded by a mutex so arbitrary threads may
  // enqueue view ids for deferred cleanup while OnFrameEnd drains the set.
  std::mutex pending_cleanup_mutex_;
  std::unordered_set<ViewId> pending_cleanup_;
  observer_ptr<console::Console> console_ { nullptr };

  auto DrainPendingViewCleanup(std::string_view reason) -> void;
  auto AttachGpuTimelinePanelDrawer(imgui::ImGuiModule& imgui_module) -> void;
  auto DetachGpuTimelinePanelDrawer() -> void;

  // Per-view stable backing storage for the non-owning spans exposed by
  // PreparedSceneFrame. Each view gets a collection of vectors that hold the
  // raw bytes, partitions and matrix arrays so PreparedSceneFrame spans can
  // point into them safely without risk of being invalidated by the
  // DrawMetadataEmitter or TransformUploader when those mutate.
  struct PerViewStorage {
    std::vector<std::byte> draw_metadata_storage;
    std::vector<PreparedSceneFrame::PartitionRange> partition_storage;
    std::vector<glm::vec4> draw_bounding_sphere_storage;
    std::vector<renderer::ConventionalShadowDrawRecord>
      conventional_shadow_draw_record_storage;
    std::vector<sceneprep::RenderItemData> render_item_storage;
    std::vector<glm::vec4> shadow_caster_bounds_storage;
    std::vector<glm::vec4> visible_receiver_bounds_storage;
    std::vector<float> world_matrix_storage;
    std::vector<float> normal_matrix_storage;
  };

  std::unordered_map<ViewId, PerViewStorage> per_view_storage_;

  const scene::Scene* last_scene_identity_ { nullptr };
  // ScenePrep instrumentation.
  std::uint64_t sceneprep_profile_frames_ { 0 };
  std::uint64_t sceneprep_profile_total_ns_ { 0 };
  std::uint64_t sceneprep_last_frame_ns_ { 0 };
  std::uint32_t sceneprep_last_frame_view_count_ { 0 };
  std::uint32_t sceneprep_last_frame_scene_view_count_ { 0 };

  std::uint64_t render_profile_frames_ { 0 };
  std::uint64_t render_profile_view_render_total_ns_ { 0 };
  std::uint64_t render_profile_render_graph_total_ns_ { 0 };
  std::uint64_t render_profile_env_update_total_ns_ { 0 };
  std::uint64_t render_last_frame_view_render_ns_ { 0 };
  std::uint64_t render_last_frame_render_graph_ns_ { 0 };
  std::uint64_t render_last_frame_env_update_ns_ { 0 };

  std::uint64_t compositing_profile_frames_ { 0 };
  std::uint64_t compositing_profile_total_ns_ { 0 };
  std::uint64_t compositing_last_frame_ns_ { 0 };
  bool offscreen_frame_used_ { false };
  bool offscreen_frame_active_ { false };
};

} // namespace oxygen::engine
