//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Vortex/Internal/RenderContextMaterializer.h>
#include <Oxygen/Vortex/Internal/ViewConstantsManager.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererTag.h>

namespace oxygen::vortex::internal {

auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}

} // namespace oxygen::vortex::internal

namespace oxygen::vortex {

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
  upload_staging_provider_
    = uploader_->CreateRingBufferStaging(frame::kFramesInFlight, 16,
      upload::kDefaultRingBufferStagingSlack, "Vortex.UploadStaging");

  inline_transfers_ = std::make_unique<upload::InlineTransfersCoordinator>(
    observer_ptr { gfx.get() });
  inline_staging_provider_ = std::make_shared<upload::RingBufferStaging>(
    upload::internal::UploaderTagFactory::Get(), observer_ptr { gfx.get() },
    frame::kFramesInFlight, 16, upload::kDefaultRingBufferStagingSlack,
    "Vortex.InlineStaging");
  inline_transfers_->RegisterProvider(inline_staging_provider_);

  render_context_pool_ = std::make_unique<internal::RenderContextPool>();
}

Renderer::Renderer(std::weak_ptr<Graphics> graphics, RendererConfig config)
  : Renderer(std::move(graphics), std::move(config),
      kPhase1DefaultRuntimeCapabilityFamilies)
{
}

Renderer::~Renderer() { OnShutdown(); }

auto Renderer::OnAttached(observer_ptr<IAsyncEngine> engine) noexcept -> bool
{
  engine_ = engine;
  return true;
}

auto Renderer::RegisterConsoleBindings(
  observer_ptr<console::Console> console) noexcept -> void
{
  console_ = console;
}

auto Renderer::ApplyConsoleCVars(
  observer_ptr<const console::Console> /*console*/) noexcept -> void
{
}

auto Renderer::OnShutdown() noexcept -> void
{
  if (uploader_) {
    [[maybe_unused]] const auto result = uploader_->Shutdown();
  }
  render_graphs_.clear();
  resolved_views_.clear();
  view_ready_states_.clear();
  published_runtime_views_by_intent_.clear();
}

auto Renderer::OnFrameStart(observer_ptr<engine::FrameContext> context) -> void
{
  resolved_views_.clear();
  {
    std::lock_guard lock(composition_mutex_);
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

  frame_slot_ = context->GetFrameSlot();
  frame_seq_num_ = context->GetFrameSequenceNumber().get();
  const auto dt = context->GetModuleTimingData().game_delta_time;
  const auto dt_seconds
    = std::chrono::duration_cast<std::chrono::duration<float>>(dt.get())
        .count();
  last_frame_dt_seconds_ = dt_seconds > 0.0F ? dt_seconds : 1.0F / 60.0F;

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
    co_await DispatchSceneRendererPreRender(*context);
  }
  co_return;
}

auto Renderer::OnRender(observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  if (context != nullptr) {
    co_await DispatchSceneRendererRender(*context);
  }
  co_return;
}

auto Renderer::OnCompositing(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  if (context != nullptr) {
    co_await DispatchSceneRendererCompositing(*context);
  }
  {
    std::lock_guard lock(composition_mutex_);
    pending_compositions_.clear();
  }
  co_return;
}

auto Renderer::OnFrameEnd(observer_ptr<engine::FrameContext> /*context*/)
  -> void
{
  resolved_views_.clear();
}

auto Renderer::RegisterViewRenderGraph(
  ViewId view_id, RenderGraphFactory factory, ResolvedView view) -> void
{
  std::unique_lock lock(view_registration_mutex_);
  render_graphs_.insert_or_assign(view_id, std::move(factory));
  resolved_views_.insert_or_assign(view_id, std::move(view));
}

auto Renderer::UpsertPublishedRuntimeView(engine::FrameContext& frame_context,
  const ViewId intent_view_id, engine::ViewContext view) -> ViewId
{
  CHECK_F(intent_view_id != kInvalidViewId,
    "Renderer::UpsertPublishedRuntimeView requires a valid intent view id");

  std::unique_lock state_lock(view_state_mutex_);
  if (const auto it = published_runtime_views_by_intent_.find(intent_view_id);
    it != published_runtime_views_by_intent_.end()) {
    frame_context.UpdateView(it->second.published_view_id, std::move(view));
    it->second.last_seen_frame = frame_context.GetFrameSequenceNumber();
    return it->second.published_view_id;
  }

  const auto published_view_id = frame_context.RegisterView(std::move(view));
  published_runtime_views_by_intent_[intent_view_id]
    = PublishedRuntimeViewState {
        .published_view_id = published_view_id,
        .last_seen_frame = frame_context.GetFrameSequenceNumber(),
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

auto Renderer::RemovePublishedRuntimeView(
  engine::FrameContext& frame_context, const ViewId intent_view_id) -> void
{
  if (intent_view_id == kInvalidViewId) {
    return;
  }

  ViewId published_view_id { kInvalidViewId };
  {
    std::unique_lock state_lock(view_state_mutex_);
    if (const auto it = published_runtime_views_by_intent_.find(intent_view_id);
      it != published_runtime_views_by_intent_.end()) {
      published_view_id = it->second.published_view_id;
      published_runtime_views_by_intent_.erase(it);
    }
  }

  if (published_view_id == kInvalidViewId) {
    return;
  }

  frame_context.RemoveView(published_view_id);
  UnregisterViewRenderGraph(published_view_id);
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
  }

  return stale_intent_ids;
}

auto Renderer::UnregisterViewRenderGraph(ViewId view_id) -> void
{
  std::unique_lock lock(view_registration_mutex_);
  render_graphs_.erase(view_id);
  resolved_views_.erase(view_id);
}

auto Renderer::RegisterComposition(CompositionSubmission submission,
  std::shared_ptr<graphics::Surface> target_surface) -> void
{
  std::lock_guard lock(composition_mutex_);
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

auto Renderer::GetGraphics() -> std::shared_ptr<Graphics>
{
  return gfx_weak_.lock();
}

auto Renderer::MakeGpuEventScopeOptions() const
  -> graphics::GpuEventScopeOptions
{
  return {};
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

auto Renderer::EnsureViewConstantsManager(Graphics& gfx) -> void
{
  if (!view_const_manager_) {
    view_const_manager_ = std::make_unique<internal::ViewConstantsManager>(
      observer_ptr { &gfx }, sizeof(ViewConstants::GpuData));
  }
}

auto Renderer::UpdateViewConstantsFromView(const ResolvedView& view) -> void
{
  view_const_cpu_.SetViewMatrix(view.ViewMatrix())
    .SetProjectionMatrix(view.ProjectionMatrix())
    .SetStableProjectionMatrix(view.StableProjectionMatrix())
    .SetCameraPosition(view.CameraPosition());
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
}

auto Renderer::BeginStandaloneFrameExecution(const FrameSessionInput& session)
  -> void
{
  frame_slot_ = session.frame_slot;
  frame_seq_num_ = session.frame_sequence.get();
  last_frame_dt_seconds_ = session.delta_time_seconds > 0.0F
    ? session.delta_time_seconds
    : 1.0F / 60.0F;

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

auto Renderer::EndOffscreenFrame() noexcept -> void { }

auto Renderer::DispatchSceneRendererPreRender(engine::FrameContext& /*context*/)
  -> co::Co<>
{
  co_return;
}

auto Renderer::DispatchSceneRendererRender(engine::FrameContext& /*context*/)
  -> co::Co<>
{
  co_return;
}

auto Renderer::DispatchSceneRendererCompositing(
  engine::FrameContext& /*context*/) -> co::Co<>
{
  co_return;
}

Renderer::ValidatedSinglePassHarnessContext::ValidatedSinglePassHarnessContext(
  Renderer& renderer, FrameSessionInput session, const ViewId view_id,
  const observer_ptr<const graphics::Framebuffer> pass_target,
  const ResolvedView& resolved_view, const PreparedSceneFrame& prepared_frame,
  std::optional<ViewConstants> core_shader_inputs)
  : renderer_(observer_ptr { &renderer })
{
  renderer.BeginStandaloneFrameExecution(session);
  active_ = true;
  try {
    renderer.WireContext(render_context_, {});
    render_context_.scene = session.scene;
    render_context_.pass_target = pass_target;
    renderer.InitializeStandaloneCurrentView(render_context_,
      current_resolved_view_, current_prepared_frame_, view_id, resolved_view,
      prepared_frame, core_shader_inputs);
  } catch (...) {
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
  , current_resolved_view_(std::move(other.current_resolved_view_))
  , current_prepared_frame_(std::move(other.current_prepared_frame_))
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
  current_resolved_view_ = std::move(other.current_resolved_view_);
  current_prepared_frame_ = std::move(other.current_prepared_frame_);
  active_ = std::exchange(other.active_, false);
  RebindCurrentViewPointers();
  return *this;
}

auto Renderer::ValidatedSinglePassHarnessContext::
  RebindCurrentViewPointers() noexcept -> void
{
  render_context_.current_view.resolved_view
    = current_resolved_view_.has_value()
    ? observer_ptr<const ResolvedView> { &*current_resolved_view_ }
    : observer_ptr<const ResolvedView> {};
  render_context_.current_view.prepared_frame
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
  render_context_.current_view = {};
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

  const auto view_id = resolved_view_.has_value()
    ? resolved_view_->view_id
    : core_shader_inputs_->view_id;
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
  , view_intent_(view_intent)
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

auto Renderer::OffscreenSceneFacade::SetViewIntent(OffscreenSceneViewInput view)
  -> OffscreenSceneFacade&
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
