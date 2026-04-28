//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Time/SimulationClock.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Types.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Internal/DeformationHistoryCache.h>
#include <Oxygen/Vortex/Internal/PreviousViewHistoryCache.h>
#include <Oxygen/Vortex/Internal/RigidTransformHistoryCache.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>
#include <Oxygen/Vortex/Types/CompositingTask.h>
#include <Oxygen/Vortex/Types/GroundGridConfig.h>
#include <Oxygen/Vortex/Types/ViewConstants.h>
#include <Oxygen/Vortex/Types/ViewHistoryFrameBindings.h>
#include <Oxygen/Vortex/ViewExtension.h>
#include <Oxygen/Vortex/api_export.h>

struct ImGuiContext;

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
class Framebuffer;
class Surface;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::content {
class IAssetLoader;
} // namespace oxygen::content

namespace oxygen::scene {
class Scene;
class SceneNode;
} // namespace oxygen::scene

namespace oxygen::scenesync {
class RuntimeMotionProducerModule;
} // namespace oxygen::scenesync

namespace oxygen {
class Graphics;
class IAsyncEngine;
namespace console {
  class Console;
} // namespace console
} // namespace oxygen

namespace oxygen::vortex::internal {
template <typename RenderContextT> class BasicRenderContextPool;
class CompositingPass;
struct CompositingPassConfig;
class GpuTimelineProfiler;
class ImGuiRuntime;
class ViewConstantsManager;
template <typename RendererT> class BasicRenderContextMaterializer;
} // namespace oxygen::vortex::internal

namespace oxygen::vortex::upload {
class InlineTransfersCoordinator;
class StagingProvider;
class UploadCoordinator;
} // namespace oxygen::vortex::upload

namespace oxygen::vortex {

namespace testing {
  struct RendererPublicationProbe;
}

class SceneRenderer;
struct RendererPublicationState;

class Renderer : public engine::EngineModule {
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
    float delta_time_seconds { time::SimulationClock::kMinDeltaTimeSeconds };
    observer_ptr<scene::Scene> scene { nullptr };
  };

  struct OutputTargetInput {
    observer_ptr<graphics::Framebuffer> framebuffer { nullptr };
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
    ViewConstants value;
  };

  struct RuntimeViewPublishInput {
    CompositionView composition_view {};
    observer_ptr<graphics::Framebuffer> render_target { nullptr };
    observer_ptr<graphics::Framebuffer> composite_source { nullptr };
  };

  struct RuntimeCompositionLayer {
    ViewId intent_view_id { kInvalidViewId };
    ViewPort viewport {};
    float opacity { 1.0F };
  };

  struct RuntimeCompositionInput {
    std::vector<RuntimeCompositionLayer> layers {};
    std::shared_ptr<graphics::Framebuffer> composite_target {};
    std::shared_ptr<graphics::Surface> target_surface {};
  };

  class ValidatedSinglePassHarnessContext {
  public:
    OXGN_VRTX_API ~ValidatedSinglePassHarnessContext();

    OXYGEN_MAKE_NON_COPYABLE(ValidatedSinglePassHarnessContext)
    OXGN_VRTX_API ValidatedSinglePassHarnessContext(
      ValidatedSinglePassHarnessContext&& other) noexcept;
    OXGN_VRTX_API auto operator=(
      ValidatedSinglePassHarnessContext&& other) noexcept
      -> ValidatedSinglePassHarnessContext&;

    [[nodiscard]] auto GetRenderContext() noexcept -> RenderContext&
    {
      return *render_context_;
    }

    [[nodiscard]] auto GetRenderContext() const noexcept -> const RenderContext&
    {
      return *render_context_;
    }

  private:
    template <typename RendererT>
    friend class internal::BasicRenderContextMaterializer;

    OXGN_VRTX_API ValidatedSinglePassHarnessContext(Renderer& renderer,
      FrameSessionInput session, ViewId view_id,
      observer_ptr<const graphics::Framebuffer> pass_target,
      const ResolvedView& resolved_view,
      const PreparedSceneFrame& prepared_frame,
      std::optional<ViewConstants> core_shader_inputs);

    auto RebindCurrentViewPointers() noexcept -> void;
    auto Release() noexcept -> void;

    observer_ptr<Renderer> renderer_ { nullptr };
    std::unique_ptr<RenderContext> render_context_ {
      std::make_unique<RenderContext>()
    };
    std::optional<ResolvedView> current_resolved_view_;
    std::optional<PreparedSceneFrame> current_prepared_frame_;
    bool active_ { false };
  };

  class SinglePassHarnessFacade {
  public:
    OXGN_VRTX_API explicit SinglePassHarnessFacade(Renderer& renderer) noexcept;
    ~SinglePassHarnessFacade() = default;

    OXYGEN_MAKE_NON_COPYABLE(SinglePassHarnessFacade)
    OXYGEN_DEFAULT_MOVABLE(SinglePassHarnessFacade)

    OXGN_VRTX_API auto SetFrameSession(FrameSessionInput session)
      -> SinglePassHarnessFacade&;
    OXGN_VRTX_API auto SetOutputTarget(OutputTargetInput target)
      -> SinglePassHarnessFacade&;
    OXGN_VRTX_API auto SetResolvedView(ResolvedViewInput view)
      -> SinglePassHarnessFacade&;
    OXGN_VRTX_API auto SetPreparedFrame(PreparedFrameInput frame)
      -> SinglePassHarnessFacade&;
    OXGN_VRTX_API auto SetCoreShaderInputs(CoreShaderInputsInput inputs)
      -> SinglePassHarnessFacade&;

    OXGN_VRTX_NDAPI auto CanFinalize() const -> bool;
    OXGN_VRTX_API auto Validate() const -> ValidationReport;
    OXGN_VRTX_API auto Finalize()
      -> oxygen::Result<ValidatedSinglePassHarnessContext, ValidationReport>;

  private:
    observer_ptr<Renderer> renderer_ { nullptr };
    std::optional<FrameSessionInput> frame_session_;
    std::optional<OutputTargetInput> output_target_;
    std::optional<ResolvedViewInput> resolved_view_;
    std::optional<PreparedFrameInput> prepared_frame_;
    std::optional<CoreShaderInputsInput> core_shader_inputs_;
  };

  using RenderGraphHarnessInput = std::function<co::Co<void>(
    ViewId view_id, const RenderContext&, graphics::CommandRecorder&)>;

  class ValidatedRenderGraphHarness {
  public:
    ~ValidatedRenderGraphHarness() = default;

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
      const auto& render_context = context_.GetRenderContext();
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
    OXGN_VRTX_API explicit RenderGraphHarnessFacade(
      Renderer& renderer) noexcept;
    ~RenderGraphHarnessFacade() = default;

    OXYGEN_MAKE_NON_COPYABLE(RenderGraphHarnessFacade)
    OXYGEN_DEFAULT_MOVABLE(RenderGraphHarnessFacade)

    OXGN_VRTX_API auto SetFrameSession(FrameSessionInput session)
      -> RenderGraphHarnessFacade&;
    OXGN_VRTX_API auto SetOutputTarget(OutputTargetInput target)
      -> RenderGraphHarnessFacade&;
    OXGN_VRTX_API auto SetResolvedView(ResolvedViewInput view)
      -> RenderGraphHarnessFacade&;
    OXGN_VRTX_API auto SetPreparedFrame(PreparedFrameInput frame)
      -> RenderGraphHarnessFacade&;
    OXGN_VRTX_API auto SetCoreShaderInputs(CoreShaderInputsInput inputs)
      -> RenderGraphHarnessFacade&;
    OXGN_VRTX_API auto SetRenderGraph(RenderGraphHarnessInput graph)
      -> RenderGraphHarnessFacade&;

    OXGN_VRTX_NDAPI auto CanFinalize() const -> bool;
    OXGN_VRTX_API auto Validate() const -> ValidationReport;
    OXGN_VRTX_API auto Finalize()
      -> oxygen::Result<ValidatedRenderGraphHarness, ValidationReport>;

  private:
    observer_ptr<Renderer> renderer_ { nullptr };
    std::optional<FrameSessionInput> frame_session_;
    std::optional<OutputTargetInput> output_target_;
    std::optional<ResolvedViewInput> resolved_view_;
    std::optional<PreparedFrameInput> prepared_frame_;
    std::optional<CoreShaderInputsInput> core_shader_inputs_;
    std::optional<RenderGraphHarnessInput> render_graph_;
  };

  struct SceneSourceInput {
    observer_ptr<scene::Scene> scene { nullptr };
  };

  class OffscreenSceneViewInput {
  public:
    OXGN_VRTX_API OffscreenSceneViewInput();

    OXGN_VRTX_NDAPI static auto FromCamera(std::string name, ViewId view_id,
      const View& view, const scene::SceneNode& camera)
      -> OffscreenSceneViewInput;

    OXGN_VRTX_API auto SetWithAtmosphere(bool enabled)
      -> OffscreenSceneViewInput&;
    OXGN_VRTX_API auto SetClearColor(const graphics::Color& clear_color)
      -> OffscreenSceneViewInput&;
    OXGN_VRTX_API auto SetForceWireframe(bool enabled)
      -> OffscreenSceneViewInput&;
    OXGN_VRTX_API auto SetExposureSourceViewId(ViewId view_id)
      -> OffscreenSceneViewInput&;

    [[nodiscard]] auto ViewIntent() const noexcept -> const CompositionView&
    {
      return composition_view_;
    }

  private:
    auto SyncName() noexcept -> void;

    std::string name_storage_ { "OffscreenScene" };
    CompositionView composition_view_ {};
  };

  struct OffscreenPipelineInput { };

  class ValidatedOffscreenSceneSession {
  public:
    ValidatedOffscreenSceneSession(Renderer& renderer,
      FrameSessionInput frame_session, SceneSourceInput scene_source,
      OffscreenSceneViewInput view_intent, OutputTargetInput output_target,
      OffscreenPipelineInput pipeline);
    ~ValidatedOffscreenSceneSession() = default;

    OXYGEN_MAKE_NON_COPYABLE(ValidatedOffscreenSceneSession)
    OXYGEN_DEFAULT_MOVABLE(ValidatedOffscreenSceneSession)

    [[nodiscard]] auto GetViewId() const noexcept -> ViewId
    {
      return view_intent_.ViewIntent().id;
    }

    OXGN_VRTX_API auto Execute() -> co::Co<void>;

  private:
    observer_ptr<Renderer> renderer_ { nullptr };
    FrameSessionInput frame_session_ {};
    SceneSourceInput scene_source_ {};
    OffscreenSceneViewInput view_intent_;
    OutputTargetInput output_target_ {};
    OffscreenPipelineInput pipeline_ {};
  };

  class OffscreenSceneFacade {
  public:
    OXGN_VRTX_API explicit OffscreenSceneFacade(Renderer& renderer) noexcept;
    ~OffscreenSceneFacade() = default;

    OXYGEN_MAKE_NON_COPYABLE(OffscreenSceneFacade)
    OXYGEN_DEFAULT_MOVABLE(OffscreenSceneFacade)

    OXGN_VRTX_API auto SetFrameSession(FrameSessionInput session)
      -> OffscreenSceneFacade&;
    OXGN_VRTX_API auto SetSceneSource(SceneSourceInput scene)
      -> OffscreenSceneFacade&;
    OXGN_VRTX_API auto SetViewIntent(const OffscreenSceneViewInput& view)
      -> OffscreenSceneFacade&;
    OXGN_VRTX_API auto SetOutputTarget(OutputTargetInput target)
      -> OffscreenSceneFacade&;
    OXGN_VRTX_API auto SetPipeline(OffscreenPipelineInput pipeline)
      -> OffscreenSceneFacade&;

    OXGN_VRTX_NDAPI auto CanFinalize() const -> bool;
    OXGN_VRTX_API auto Validate() const -> ValidationReport;
    OXGN_VRTX_API auto Finalize()
      -> oxygen::Result<ValidatedOffscreenSceneSession, ValidationReport>;

  private:
    observer_ptr<Renderer> renderer_ { nullptr };
    std::optional<FrameSessionInput> frame_session_;
    std::optional<SceneSourceInput> scene_source_;
    std::optional<OffscreenSceneViewInput> view_intent_;
    std::optional<OutputTargetInput> output_target_;
    std::optional<OffscreenPipelineInput> pipeline_;
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

  using RenderGraphFactory = RenderGraphHarnessInput;

  OXGN_VRTX_API explicit Renderer(
    std::weak_ptr<Graphics> graphics, RendererConfig config);
  OXGN_VRTX_API explicit Renderer(std::weak_ptr<Graphics> graphics,
    RendererConfig config, CapabilitySet capability_families);

  OXYGEN_MAKE_NON_COPYABLE(Renderer)
  OXYGEN_DEFAULT_MOVABLE(Renderer)

  OXGN_VRTX_API ~Renderer() override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "VortexRendererModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    return engine::kRendererModulePriority;
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override
  {
    return engine::MakeModuleMask<core::PhaseId::kFrameStart,
      core::PhaseId::kTransformPropagation, core::PhaseId::kPreRender,
      core::PhaseId::kRender, core::PhaseId::kCompositing,
      core::PhaseId::kFrameEnd>();
  }

  OXGN_VRTX_NDAPI auto OnAttached(observer_ptr<IAsyncEngine> engine) noexcept
    -> bool override;
  OXGN_VRTX_API auto RegisterConsoleBindings(
    observer_ptr<console::Console> console) noexcept -> void override;
  OXGN_VRTX_API auto ApplyConsoleCVars(
    observer_ptr<const console::Console> console) noexcept -> void override;
  OXGN_VRTX_API auto OnShutdown() noexcept -> void override;
  OXGN_VRTX_API auto OnFrameStart(observer_ptr<engine::FrameContext> context)
    -> void override;
  OXGN_VRTX_NDAPI auto OnTransformPropagation(
    observer_ptr<engine::FrameContext> context) -> co::Co<> override;
  OXGN_VRTX_NDAPI auto OnPreRender(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  OXGN_VRTX_NDAPI auto OnRender(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  OXGN_VRTX_NDAPI auto OnCompositing(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  OXGN_VRTX_API auto OnFrameEnd(observer_ptr<engine::FrameContext> context)
    -> void override;

  OXGN_VRTX_API auto RegisterViewRenderGraph(
    ViewId view_id, RenderGraphFactory factory, ResolvedView view) -> void;
  OXGN_VRTX_API auto RegisterResolvedView(ViewId view_id, ResolvedView view)
    -> void;
  OXGN_VRTX_API auto PublishRuntimeCompositionView(
    engine::FrameContext& frame_context, const RuntimeViewPublishInput& input,
    std::optional<ShadingMode> shading_mode_override = std::nullopt) -> ViewId;
  OXGN_VRTX_API auto UpsertPublishedRuntimeView(
    engine::FrameContext& frame_context, ViewId intent_view_id,
    engine::ViewContext view,
    std::optional<ShadingMode> shading_mode_override = std::nullopt,
    std::optional<RenderMode> render_mode_override = std::nullopt,
    CompositionView::ViewStateHandle view_state_handle
    = CompositionView::kInvalidViewStateHandle,
    CompositionView::ViewKind view_kind = CompositionView::ViewKind::kPrimary,
    std::vector<CompositionView::AuxOutputDesc> produced_aux_outputs = {},
    std::vector<CompositionView::AuxInputDesc> consumed_aux_outputs = {},
    std::string debug_name = {}) -> ViewId;
  OXGN_VRTX_NDAPI auto ResolvePublishedRuntimeViewId(
    ViewId intent_view_id) const noexcept -> ViewId;
  auto GetRigidTransformHistoryCache() noexcept
    -> internal::RigidTransformHistoryCache&
  {
    return rigid_transform_history_cache_;
  }
  auto GetDeformationHistoryCache() noexcept
    -> internal::DeformationHistoryCache&
  {
    return deformation_history_cache_;
  }
  OXGN_VRTX_NDAPI auto GetRuntimeMotionProducerModule() const noexcept
    -> observer_ptr<scenesync::RuntimeMotionProducerModule>;
  OXGN_VRTX_API auto RemovePublishedRuntimeView(ViewId intent_view_id) -> void;
  OXGN_VRTX_API auto RemovePublishedRuntimeView(
    engine::FrameContext& frame_context, ViewId intent_view_id) -> void;
  OXGN_VRTX_API auto PruneStalePublishedRuntimeViews(
    engine::FrameContext& frame_context) -> std::vector<ViewId>;
  OXGN_VRTX_API auto UnregisterViewRenderGraph(ViewId view_id) -> void;
  OXGN_VRTX_API auto RegisterRuntimeComposition(
    const RuntimeCompositionInput& input) -> void;
  OXGN_VRTX_API auto RegisterComposition(CompositionSubmission submission,
    std::shared_ptr<graphics::Surface> target_surface) -> void;
  OXGN_VRTX_API auto RegisterViewExtension(ViewExtensionPtr extension) -> void;

  OXGN_VRTX_API auto ForSinglePassHarness() -> SinglePassHarnessFacade;
  OXGN_VRTX_API auto ForRenderGraphHarness() -> RenderGraphHarnessFacade;
  OXGN_VRTX_API auto ForOffscreenScene() -> OffscreenSceneFacade;

  OXGN_VRTX_NDAPI auto GetStats() const noexcept -> Stats;
  OXGN_VRTX_API auto ResetStats() noexcept -> void;

  [[nodiscard]] auto GetCapabilityFamilies() const noexcept -> CapabilitySet
  {
    return capability_families_;
  }

  [[nodiscard]] auto GetShadowQualityTier() const noexcept -> ShadowQualityTier
  {
    return config_.shadow_quality_tier;
  }

  [[nodiscard]] auto HasCapability(
    const RendererCapabilityFamily family) const noexcept -> bool
  {
    return HasAllCapabilities(capability_families_, family);
  }

  [[nodiscard]] auto ValidateCapabilityRequirements(
    const PipelineCapabilityRequirements requirements) const noexcept
    -> PipelineCapabilityValidation
  {
    return vortex::ValidateCapabilityRequirements(
      capability_families_, requirements);
  }

  OXGN_VRTX_API auto SetShaderDebugMode(ShaderDebugMode mode) noexcept -> void;
  OXGN_VRTX_NDAPI auto GetShaderDebugMode() const noexcept -> ShaderDebugMode;
  OXGN_VRTX_API auto SetRenderMode(RenderMode mode) noexcept -> void;
  OXGN_VRTX_NDAPI auto GetRenderMode() const noexcept -> RenderMode;
  OXGN_VRTX_API auto SetWireframeColor(const graphics::Color& color) noexcept
    -> void;
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetWireframeColor() const noexcept
    -> const graphics::Color&;
  OXGN_VRTX_NDAPI auto GetDiagnosticsService() noexcept -> DiagnosticsService&;
  OXGN_VRTX_NDAPI auto GetDiagnosticsService() const noexcept
    -> const DiagnosticsService&;
  OXGN_VRTX_API auto SetGroundGridConfig(
    const GroundGridConfig& config) noexcept -> void;
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetGroundGridConfig() const noexcept
    -> const GroundGridConfig&
  {
    return ground_grid_config_;
  }
  [[nodiscard]] OXGN_VRTX_API auto
  GetLastEnvironmentLightingState() const noexcept
    -> SceneRenderer::EnvironmentLightingState;

  OXGN_VRTX_API auto IsViewReady(ViewId view_id) const -> bool;
  OXGN_VRTX_API auto SetImGuiWindowId(platform::WindowIdType window_id) -> void;
  [[nodiscard]] OXGN_VRTX_API auto GetImGuiContext() noexcept -> ImGuiContext*;
  [[nodiscard]] OXGN_VRTX_NDAPI auto IsImGuiFrameActive() const noexcept
    -> bool;
  OXGN_VRTX_API auto GetGraphics() -> std::shared_ptr<Graphics>;
  [[nodiscard]] OXGN_VRTX_API auto GetLocalFogEnabled() const noexcept -> bool;
  [[nodiscard]] OXGN_VRTX_API auto
  GetLocalFogGlobalStartDistanceMeters() const noexcept -> float;
  [[nodiscard]] OXGN_VRTX_API auto
  GetLocalFogRenderIntoVolumetricFog() const noexcept -> bool;
  [[nodiscard]] OXGN_VRTX_API auto
  GetLocalFogMaxDensityIntoVolumetricFog() const noexcept -> float;
  [[nodiscard]] OXGN_VRTX_API auto
  GetVolumetricFogDirectionalShadowsEnabled() const noexcept -> bool;
  [[nodiscard]] OXGN_VRTX_API auto
  GetVolumetricFogTemporalReprojectionEnabled() const noexcept -> bool;
  [[nodiscard]] OXGN_VRTX_API auto
  GetVolumetricFogJitterEnabled() const noexcept -> bool;
  [[nodiscard]] OXGN_VRTX_API auto
  GetVolumetricFogHistoryMissSupersampleCount() const noexcept -> std::uint32_t;
  [[nodiscard]] OXGN_VRTX_API auto GetLocalFogTilePixelSize() const noexcept
    -> std::uint32_t;
  [[nodiscard]] OXGN_VRTX_API auto
  GetLocalFogTileMaxInstanceCount() const noexcept -> std::uint32_t;
  [[nodiscard]] OXGN_VRTX_API auto GetLocalFogUseHzb() const noexcept -> bool;
  [[nodiscard]] OXGN_VRTX_API auto GetAerialPerspectiveLutWidth() const noexcept
    -> std::uint32_t;
  [[nodiscard]] OXGN_VRTX_API auto
  GetAerialPerspectiveLutDepthResolution() const noexcept -> std::uint32_t;
  [[nodiscard]] OXGN_VRTX_API auto
  GetAerialPerspectiveLutDepthKm() const noexcept -> float;
  [[nodiscard]] OXGN_VRTX_API auto
  GetAerialPerspectiveLutSampleCountMaxPerSlice() const noexcept -> float;
  [[nodiscard]] OXGN_VRTX_API auto GetOcclusionEnabled() const noexcept
    -> bool;
  [[nodiscard]] OXGN_VRTX_API auto GetOcclusionMaxCandidateCount()
    const noexcept -> std::uint32_t;
  OXGN_VRTX_NDAPI auto GetStagingProvider() -> upload::StagingProvider&;
  OXGN_VRTX_NDAPI auto GetInlineTransfersCoordinator()
    -> upload::InlineTransfersCoordinator&;
  OXGN_VRTX_NDAPI auto GetUploadCoordinator() -> upload::UploadCoordinator&;
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetAssetLoader() const noexcept
    -> observer_ptr<content::IAssetLoader>;

private:
  friend class SceneRenderer;
  friend struct testing::RendererPublicationProbe;

  struct PublishedRuntimeViewState {
    ViewId published_view_id { kInvalidViewId };
    frame::SequenceNumber last_seen_frame { 0U };
    std::optional<ShadingMode> shading_mode_override;
    std::optional<RenderMode> render_mode_override;
    CompositionView::ViewStateHandle view_state_handle {
      CompositionView::kInvalidViewStateHandle
    };
    CompositionView::ViewKind view_kind { CompositionView::ViewKind::kPrimary };
    std::vector<CompositionView::AuxOutputDesc> produced_aux_outputs {};
    std::vector<CompositionView::AuxInputDesc> consumed_aux_outputs {};
    std::string debug_name {};
  };

  struct DetachedPublishedRuntimeViewState {
    ViewId published_view_id { kInvalidViewId };
    CompositionView::ViewStateHandle view_state_handle {
      CompositionView::kInvalidViewStateHandle
    };
  };

  static constexpr frame::SequenceNumber kPublishedRuntimeViewMaxIdleFrames {
    60
  };

  auto EnsureViewConstantsManager(Graphics& gfx) -> void;
  auto EnsureImGuiRuntime() -> internal::ImGuiRuntime*;
  OXGN_VRTX_API auto PopulateRenderContextViewState(
    RenderContext& render_context, engine::FrameContext& context,
    bool prefer_composite_source) const -> void;
  [[nodiscard]] auto ResolvePublishedRuntimeShadingMode(
    ViewId published_view_id) const noexcept -> std::optional<ShadingMode>;
  [[nodiscard]] auto ResolvePublishedRuntimeRenderMode(
    ViewId published_view_id) const noexcept -> std::optional<RenderMode>;
  [[nodiscard]] auto ResolvePublishedRuntimeViewStateHandle(
    ViewId published_view_id) const noexcept -> CompositionView::ViewStateHandle;
  auto UpdateViewConstantsFromView(const ResolvedView& view) -> void;
  [[nodiscard]] auto BuildViewHistoryFrameBindings(
    CompositionView::ViewStateHandle view_state_handle,
    const ResolvedView& view, observer_ptr<const scene::Scene> scene)
    -> ViewHistoryFrameBindings;
  auto EnsureSceneRenderer(const CompositionView* composition_view = nullptr)
    -> SceneRenderer&;
  auto EnsureSceneRendererFrameStarted(engine::FrameContext& context) -> void;
  auto EnsurePublicationState(Graphics& gfx) -> RendererPublicationState&;
  auto BeginPublicationFrame(
    Graphics& gfx, frame::SequenceNumber sequence, frame::Slot slot) -> void;
  [[nodiscard]] auto PublishCurrentViewHistoryFrameBindings(
    RenderContext& render_context, RendererPublicationState& publication_state)
    -> ShaderVisibleIndex;
  auto WriteCurrentViewConstants(RenderContext& render_context, Graphics& gfx,
    ShaderVisibleIndex view_frame_bindings_slot) -> void;
  auto RefreshCurrentViewFrameBindings(
    RenderContext& render_context, SceneRenderer& scene_renderer) -> void;
  auto PublishCurrentViewPreSceneFrameBindings(
    RenderContext& render_context, SceneRenderer& scene_renderer) -> void;
  auto PublishCurrentViewPostSceneFrameBindings(
    RenderContext& render_context, SceneRenderer& scene_renderer) -> void;
  auto ResetPublicationState() -> void;
  auto DetachPublishedRuntimeViewState(ViewId intent_view_id)
    -> DetachedPublishedRuntimeViewState;
  auto ReleasePooledRenderContext(frame::Slot slot) noexcept -> void;
  auto WireContext(RenderContext& context,
    const std::shared_ptr<graphics::Buffer>& view_constants) -> void;
  auto BeginStandaloneFrameExecution(const FrameSessionInput& session) -> void;
  auto InitializeStandaloneCurrentView(RenderContext& render_context,
    std::optional<ResolvedView>& current_resolved_view,
    std::optional<PreparedSceneFrame>& current_prepared_frame, ViewId view_id,
    const ResolvedView& resolved_view, const PreparedSceneFrame& prepared_frame,
    const std::optional<ViewConstants>& view_constants_override) -> void;
  auto EndOffscreenFrame() noexcept -> void;
  auto DispatchSceneRendererPreRender(
    observer_ptr<engine::FrameContext> context) -> co::Co<>;
  auto DispatchSceneRendererRender(observer_ptr<engine::FrameContext> context)
    -> co::Co<>;
  auto DispatchSceneRendererCompositing(
    observer_ptr<engine::FrameContext> context) -> co::Co<>;
  [[nodiscard]] auto SnapshotViewExtensions() const
    -> std::vector<ViewExtensionPtr>;
  auto DispatchViewExtensionsOnFamilyAssembled(
    engine::FrameContext& frame_context, RenderContext& render_context)
    -> void;
  auto DispatchViewExtensionsOnViewSetup(RenderContext& render_context) -> void;
  auto DispatchViewExtensionsOnPreRenderViewGpu(RenderContext& render_context)
    -> void;
  auto DispatchViewExtensionsOnPostRenderViewGpu(RenderContext& render_context)
    -> void;
  auto DispatchViewExtensionsOnPostComposition(
    engine::FrameContext& frame_context,
    CompositionView::SurfaceRouteId surface_id, graphics::Framebuffer& target,
    graphics::CommandRecorder& recorder) -> void;

  std::weak_ptr<Graphics> gfx_weak_;
  observer_ptr<IAsyncEngine> engine_ { nullptr };
  RendererConfig config_ {};
  CapabilitySet capability_families_ {
    kPhase1DefaultRuntimeCapabilityFamilies
  };

  ViewConstants view_const_cpu_;
  std::unique_ptr<internal::ViewConstantsManager> view_const_manager_;
  std::unique_ptr<upload::UploadCoordinator> uploader_;
  std::shared_ptr<upload::StagingProvider> upload_staging_provider_;
  std::unique_ptr<upload::InlineTransfersCoordinator> inline_transfers_;
  std::shared_ptr<upload::StagingProvider> inline_staging_provider_;
  std::shared_ptr<internal::CompositingPass> compositing_pass_;
  std::shared_ptr<internal::CompositingPassConfig> compositing_pass_config_;
  std::unique_ptr<DiagnosticsService> diagnostics_service_;
  std::unique_ptr<internal::GpuTimelineProfiler> gpu_timeline_profiler_;
  std::unique_ptr<internal::ImGuiRuntime> imgui_runtime_ {};
  GroundGridConfig ground_grid_config_ {};
  RenderMode render_mode_ { RenderMode::kSolid };
  graphics::Color wireframe_color_ { 1.0F, 1.0F, 1.0F, 1.0F };
  std::unique_ptr<internal::BasicRenderContextPool<RenderContext>>
    render_context_pool_;
  std::unique_ptr<SceneRenderer> scene_renderer_;
  internal::RigidTransformHistoryCache rigid_transform_history_cache_ {};
  internal::DeformationHistoryCache deformation_history_cache_ {};
  internal::PreviousViewHistoryCache previous_view_history_cache_ {};
  frame::SequenceNumber scene_renderer_started_frame_ { 0U };

  mutable std::shared_mutex view_registration_mutex_;
  std::unordered_map<ViewId, RenderGraphFactory> render_graphs_;
  std::unordered_map<ViewId, ResolvedView> resolved_views_;

  mutable std::shared_mutex view_state_mutex_;
  std::unordered_map<ViewId, bool> view_ready_states_;
  std::unordered_map<ViewId, PublishedRuntimeViewState>
    published_runtime_views_by_intent_;
  std::unique_ptr<RendererPublicationState> publication_state_;

  struct PendingComposition {
    CompositionSubmission submission {};
    std::shared_ptr<graphics::Surface> target_surface;
    std::uint64_t sequence_in_frame { 0 };
  };

  std::mutex composition_mutex_;
  std::vector<PendingComposition> pending_compositions_;
  std::uint64_t next_composition_sequence_in_frame_ { 0 };

  mutable std::mutex view_extension_mutex_;
  std::vector<ViewExtensionPtr> view_extensions_;

  std::uint64_t frame_seq_num_ { 0 };
  frame::Slot frame_slot_ { frame::kInvalidSlot };
  float last_frame_dt_seconds_ { time::SimulationClock::kMinDeltaTimeSeconds };
  bool shutdown_called_ { false };
  observer_ptr<console::Console> console_ { nullptr };
};

} // namespace oxygen::vortex
