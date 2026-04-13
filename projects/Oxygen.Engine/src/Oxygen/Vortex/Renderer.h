//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
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
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/GpuEventScope.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Internal/RenderContextPool.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Types/CompositingTask.h>
#include <Oxygen/Vortex/Types/ViewConstants.h>
#include <Oxygen/Vortex/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Vortex/Upload/RingBufferStaging.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
class Framebuffer;
class Surface;
} // namespace oxygen::graphics

namespace oxygen::scene {
class Scene;
class SceneNode;
} // namespace oxygen::scene

namespace oxygen {
class Graphics;
class IAsyncEngine;
namespace console {
  class Console;
} // namespace console
} // namespace oxygen

namespace oxygen::vortex::internal {
class ViewConstantsManager;
template <typename RendererT> class BasicRenderContextMaterializer;
} // namespace oxygen::vortex::internal

namespace oxygen::vortex {

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
    OXGN_VRTX_API ~ValidatedSinglePassHarnessContext();

    OXYGEN_MAKE_NON_COPYABLE(ValidatedSinglePassHarnessContext)
    OXGN_VRTX_API ValidatedSinglePassHarnessContext(
      ValidatedSinglePassHarnessContext&& other) noexcept;
    OXGN_VRTX_API auto operator=(
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
    RenderContext render_context_ {};
    std::optional<ResolvedView> current_resolved_view_ {};
    std::optional<PreparedSceneFrame> current_prepared_frame_ {};
    bool active_ { false };
  };

  class SinglePassHarnessFacade {
  public:
    OXGN_VRTX_API explicit SinglePassHarnessFacade(Renderer& renderer) noexcept;

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

    [[nodiscard]] OXGN_VRTX_API auto CanFinalize() const -> bool;
    OXGN_VRTX_API auto Validate() const -> ValidationReport;
    OXGN_VRTX_API auto Finalize()
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
    OXGN_VRTX_API explicit RenderGraphHarnessFacade(
      Renderer& renderer) noexcept;

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

    [[nodiscard]] OXGN_VRTX_API auto CanFinalize() const -> bool;
    OXGN_VRTX_API auto Validate() const -> ValidationReport;
    OXGN_VRTX_API auto Finalize()
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
    OXGN_VRTX_API OffscreenSceneViewInput();

    [[nodiscard]] OXGN_VRTX_API static auto FromCamera(std::string name,
      ViewId view_id, const View& view, const scene::SceneNode& camera)
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
    OffscreenSceneViewInput view_intent_ {};
    OutputTargetInput output_target_ {};
    OffscreenPipelineInput pipeline_ {};
  };

  class OffscreenSceneFacade {
  public:
    OXGN_VRTX_API explicit OffscreenSceneFacade(Renderer& renderer) noexcept;

    OXYGEN_MAKE_NON_COPYABLE(OffscreenSceneFacade)
    OXYGEN_DEFAULT_MOVABLE(OffscreenSceneFacade)

    OXGN_VRTX_API auto SetFrameSession(FrameSessionInput session)
      -> OffscreenSceneFacade&;
    OXGN_VRTX_API auto SetSceneSource(SceneSourceInput scene)
      -> OffscreenSceneFacade&;
    OXGN_VRTX_API auto SetViewIntent(OffscreenSceneViewInput view)
      -> OffscreenSceneFacade&;
    OXGN_VRTX_API auto SetOutputTarget(OutputTargetInput target)
      -> OffscreenSceneFacade&;
    OXGN_VRTX_API auto SetPipeline(OffscreenPipelineInput pipeline)
      -> OffscreenSceneFacade&;

    [[nodiscard]] OXGN_VRTX_API auto CanFinalize() const -> bool;
    OXGN_VRTX_API auto Validate() const -> ValidationReport;
    OXGN_VRTX_API auto Finalize()
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
  OXGN_VRTX_API auto UpsertPublishedRuntimeView(
    engine::FrameContext& frame_context, ViewId intent_view_id,
    engine::ViewContext view) -> ViewId;
  [[nodiscard]] OXGN_VRTX_NDAPI auto ResolvePublishedRuntimeViewId(
    ViewId intent_view_id) const noexcept -> ViewId;
  OXGN_VRTX_API auto RemovePublishedRuntimeView(
    engine::FrameContext& frame_context, ViewId intent_view_id) -> void;
  OXGN_VRTX_API auto PruneStalePublishedRuntimeViews(
    engine::FrameContext& frame_context) -> std::vector<ViewId>;
  OXGN_VRTX_API auto UnregisterViewRenderGraph(ViewId view_id) -> void;
  OXGN_VRTX_API auto RegisterComposition(CompositionSubmission submission,
    std::shared_ptr<graphics::Surface> target_surface) -> void;

  OXGN_VRTX_API auto ForSinglePassHarness() -> SinglePassHarnessFacade;
  OXGN_VRTX_API auto ForRenderGraphHarness() -> RenderGraphHarnessFacade;
  OXGN_VRTX_API auto ForOffscreenScene() -> OffscreenSceneFacade;

  [[nodiscard]] OXGN_VRTX_NDAPI auto GetStats() const noexcept -> Stats;
  OXGN_VRTX_API auto ResetStats() noexcept -> void;

  [[nodiscard]] auto GetCapabilityFamilies() const noexcept -> CapabilitySet
  {
    return capability_families_;
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

  OXGN_VRTX_API auto IsViewReady(ViewId view_id) const -> bool;
  OXGN_VRTX_API auto GetGraphics() -> std::shared_ptr<Graphics>;
  [[nodiscard]] OXGN_VRTX_API auto MakeGpuEventScopeOptions() const
    -> graphics::GpuEventScopeOptions;
  OXGN_VRTX_NDAPI auto GetStagingProvider() -> upload::StagingProvider&;
  OXGN_VRTX_NDAPI auto GetInlineTransfersCoordinator()
    -> upload::InlineTransfersCoordinator&;

private:
  struct PublishedRuntimeViewState {
    ViewId published_view_id { kInvalidViewId };
    frame::SequenceNumber last_seen_frame { 0U };
  };

  static constexpr frame::SequenceNumber kPublishedRuntimeViewMaxIdleFrames {
    60
  };

  auto EnsureViewConstantsManager(Graphics& gfx) -> void;
  auto UpdateViewConstantsFromView(const ResolvedView& view) -> void;
  auto WireContext(RenderContext& context,
    const std::shared_ptr<graphics::Buffer>& view_constants) -> void;
  auto BeginStandaloneFrameExecution(const FrameSessionInput& session) -> void;
  auto InitializeStandaloneCurrentView(RenderContext& render_context,
    std::optional<ResolvedView>& current_resolved_view,
    std::optional<PreparedSceneFrame>& current_prepared_frame, ViewId view_id,
    const ResolvedView& resolved_view, const PreparedSceneFrame& prepared_frame,
    const std::optional<ViewConstants>& view_constants_override) -> void;
  auto EndOffscreenFrame() noexcept -> void;
  auto DispatchSceneRendererPreRender(engine::FrameContext& context)
    -> co::Co<>;
  auto DispatchSceneRendererRender(engine::FrameContext& context) -> co::Co<>;
  auto DispatchSceneRendererCompositing(engine::FrameContext& context)
    -> co::Co<>;

  std::weak_ptr<Graphics> gfx_weak_;
  observer_ptr<IAsyncEngine> engine_ { nullptr };
  RendererConfig config_ {};
  CapabilitySet capability_families_ {
    kPhase1DefaultRuntimeCapabilityFamilies
  };

  ViewConstants view_const_cpu_ {};
  std::unique_ptr<internal::ViewConstantsManager> view_const_manager_;
  std::unique_ptr<upload::UploadCoordinator> uploader_;
  std::shared_ptr<upload::StagingProvider> upload_staging_provider_;
  std::unique_ptr<upload::InlineTransfersCoordinator> inline_transfers_;
  std::shared_ptr<upload::StagingProvider> inline_staging_provider_;
  std::unique_ptr<internal::RenderContextPool> render_context_pool_;

  mutable std::shared_mutex view_registration_mutex_;
  std::unordered_map<ViewId, RenderGraphFactory> render_graphs_;
  std::unordered_map<ViewId, ResolvedView> resolved_views_;

  mutable std::shared_mutex view_state_mutex_;
  std::unordered_map<ViewId, bool> view_ready_states_;
  std::unordered_map<ViewId, PublishedRuntimeViewState>
    published_runtime_views_by_intent_;

  struct PendingComposition {
    CompositionSubmission submission {};
    std::shared_ptr<graphics::Surface> target_surface {};
    std::uint64_t sequence_in_frame { 0 };
  };

  std::mutex composition_mutex_;
  std::vector<PendingComposition> pending_compositions_;
  std::uint64_t next_composition_sequence_in_frame_ { 0 };
  std::uint64_t frame_seq_num_ { 0 };
  frame::Slot frame_slot_ { frame::kInvalidSlot };
  float last_frame_dt_seconds_ { 1.0F / 60.0F };
  observer_ptr<console::Console> console_ { nullptr };
};

} // namespace oxygen::vortex
