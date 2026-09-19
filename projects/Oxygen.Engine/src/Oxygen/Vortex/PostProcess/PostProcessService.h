//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessFrameBindings.h>
#include <Oxygen/Vortex/Resources/TextureBinder.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Framebuffer;
class Texture;
class GpuBufferReadback;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;
namespace testing {
  struct RendererPublicationProbe;
}

namespace internal {
template <typename Payload> class PerViewStructuredPublisher;
} // namespace internal

namespace postprocess {
class BloomPass;
class ExposurePass;
class TonemapPass;
} // namespace postprocess

class PostProcessService {
public:
  struct Inputs {
    const graphics::Texture* scene_signal { nullptr };
    const graphics::Texture* scene_depth { nullptr };
    const graphics::Texture* scene_velocity { nullptr };
    observer_ptr<const graphics::Framebuffer> post_target;
    ShaderVisibleIndex scene_signal_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex scene_depth_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex scene_velocity_srv { kInvalidShaderVisibleIndex };
    //! Optional external bloom, matching scene extent and frame P. The caller
    //! retains its texture/SRV and establishes shader-read state through use.
    ShaderVisibleIndex bloom_texture_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex eye_adaptation_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex eye_adaptation_uav { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex post_history_srv { kInvalidShaderVisibleIndex };
    //! Keep this accumulation immutable through every checked-color consumer.
    const graphics::Texture* scene_fallback { nullptr };
    ShaderVisibleIndex scene_fallback_srv { kInvalidShaderVisibleIndex };
    postprocess::ExposurePass::FrameLease checked_resolution;
  };

  struct ExecutionState {
    bool published_bindings { false };
    bool tonemap_requested { false };
    bool tonemap_executed { false };
    bool wrote_visible_output { false };
    bool bloom_requested { false };
    bool bloom_executed { false };
    bool auto_exposure_requested { false };
    bool auto_exposure_executed { false };
    bool used_fixed_exposure { false };
    ViewId view_id { kInvalidViewId };
    ShaderVisibleIndex post_process_frame_slot { kInvalidShaderVisibleIndex };
    float exposure_value { 1.0F };
  };

  enum class ExposureMaskStatus { kAbsent, kPending, kReady, kFailed };

  struct ExposureSettingsState {
    scene::ResolvedExposureSettings resolved;
    std::optional<float> camera_ev;
    std::uint64_t revision { 0U };
    std::uint64_t lifetime { 0U };
    std::optional<scene::ExposureSettingsError> last_error;
    ExposureMaskStatus mask_status { ExposureMaskStatus::kAbsent };
    content::ResourceKey requested_mask {};
    std::string mask_error;
    std::shared_ptr<const resources::TextureBinder::ReadyTexture> mask;
  };

  OXGN_VRTX_API explicit PostProcessService(
    Renderer& renderer, observer_ptr<content::IAssetLoader> asset_loader = {});
  OXGN_VRTX_API ~PostProcessService();

  PostProcessService(const PostProcessService&) = delete;
  auto operator=(const PostProcessService&) -> PostProcessService& = delete;
  PostProcessService(PostProcessService&&) = delete;
  auto operator=(PostProcessService&&) -> PostProcessService& = delete;

  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  //! Validate and atomically apply authored settings. Missing camera context
  //! reuses the accepted camera EV only when remaining in ManualCamera mode.
  OXGN_VRTX_API auto SetConfig(const PostProcessConfig& config,
    std::optional<float> camera_ev = {}) -> void;
  //! Apply an immutable validated snapshot from a captured view or pass client.
  OXGN_VRTX_API auto SetResolvedConfig(const ResolvedPostProcessConfig& config)
    -> void;
  //! Combine presentation settings with this frame's captured exposure.
  //! CaptureViewExposureSettings must have captured the view before this call.
  //! The supplied config.exposure is superseded by the accepted capture.
  [[nodiscard]] OXGN_VRTX_API auto BuildPassConfig(
    const PostProcessConfig& config, ViewId view_id,
    CompositionView::ViewStateHandle handle) const -> ResolvedPostProcessConfig;
  //! Resolve one complete request, retaining this view's prior valid revision.
  [[nodiscard]] OXGN_VRTX_API auto ResolveViewExposureSettings(
    CompositionView::ViewStateHandle handle,
    const scene::ExposureSettings& requested,
    std::optional<float> camera_ev = {}) -> const ExposureSettingsState&;
  //! Freeze the complete accepted revision and mask for this logical
  //! view/frame.
  [[nodiscard]] OXGN_VRTX_API auto CaptureViewExposureSettings(ViewId view_id,
    CompositionView::ViewStateHandle handle,
    const scene::ExposureSettings& requested,
    std::optional<float> camera_ev = {}, bool suppress_transitions = false,
    observer_ptr<const scene::Scene> world = {})
    -> const ExposureSettingsState&;
  OXGN_VRTX_API auto CaptureSharedExposureSource(const RenderContext& ctx,
    ViewId source_view_id, CompositionView::ViewStateHandle source_handle)
    -> const postprocess::ExposurePass::Source&;
  OXGN_VRTX_API auto CaptureRegisteredExposureControls(const RenderContext& ctx)
    -> void;
  [[nodiscard]] OXGN_VRTX_API auto BuildBindings(
    const Inputs& inputs) const -> PostProcessFrameBindings;
  OXGN_VRTX_API auto PublishBindings(
    ViewId view_id, const PostProcessFrameBindings& bindings) -> ShaderVisibleIndex;
  //! Resolve before writing pre-exposed radiance; Execute consumes this binding.
  [[nodiscard]] OXGN_VRTX_API auto PrepareFrameExposure(RenderContext& ctx,
    bool use_fp32,
    postprocess::ExposurePass::StateLease qualified_candidate = {},
    bool preserve_fp32_candidate_p = false)
    -> postprocess::ExposurePass::FrameLease;
  struct PreparedExposure {
    const PostProcessService* owner;
    postprocess::ExposurePass::Result exposure;
    ResolvedPostProcessConfig config;
    ViewId view_id;
    CompositionView::ViewStateHandle handle;
    std::uint64_t lifetime;
    frame::SequenceNumber sequence;
    std::optional<ExposureTransitionToken> status_transition;
    std::optional<std::uint64_t> precision_epoch;
  };
  //! Observe unattenuated opaque input using the current immutable frame lease.
  [[nodiscard]] OXGN_VRTX_API auto CapturePreEnvironmentRange(
    RenderContext& ctx, const graphics::Texture& source,
    ShaderVisibleIndex source_srv) -> bool;
  [[nodiscard]] OXGN_VRTX_API auto CheckSceneColorRange(RenderContext& ctx,
    const graphics::Texture& source, ShaderVisibleIndex source_srv) -> bool;
  //! Solve from the accumulated scene signal before checked color resolution.
  //! The returned record pins the result/config for this view and logical
  //! frame.
  [[nodiscard]] OXGN_VRTX_API auto PrepareSceneExposure(
    ViewId view_id, RenderContext& ctx, const Inputs& inputs)
    -> std::optional<PreparedExposure>;
  //! Check/narrow using the prepared final gain and accepted metering policy.
  //! A submitted check still requires GPU rejection handling by the consumer.
  [[nodiscard]] OXGN_VRTX_API auto ConvertSceneColor(RenderContext& ctx,
    const PreparedExposure& prepared, const Inputs& inputs,
    graphics::Texture& destination, ShaderVisibleIndex destination_uav) -> bool;
  //! Configure the current view's required products before frame preparation.
  //! Returns only a matching completed GPU candidate; numerical P stays on GPU.
  [[nodiscard]] OXGN_VRTX_API auto SelectPrecisionCandidate(RenderContext& ctx,
    const postprocess::ExposurePass::EligibilityInputs& requirements)
    -> postprocess::ExposurePass::StateLease;
  //! Generate current/candidate certificates before any checked conversion.
  [[nodiscard]] OXGN_VRTX_API auto PrepareScenePrecision(RenderContext& ctx,
    const PreparedExposure& prepared,
    std::span<const postprocess::ExposurePass::HdrProduct> products,
    std::optional<postprocess::ExposurePass::SceneComposition> composition
    = std::nullopt) -> bool;
  //! Finalize after checked conversion, then copy one combined completed status.
  [[nodiscard]] OXGN_VRTX_API auto FinalizeScenePrecision(RenderContext& ctx,
    const PreparedExposure& prepared) -> bool;
  OXGN_VRTX_API auto Execute(ViewId view_id, RenderContext& ctx,
    const SceneTextures& scene_textures, const Inputs& inputs,
    const PreparedExposure* prepared_exposure = nullptr) -> void;
  OXGN_VRTX_API auto RemoveViewState(ViewId view_id,
    CompositionView::ViewStateHandle view_state_handle
    = CompositionView::kInvalidViewStateHandle) -> void;
  OXGN_VRTX_API auto PreserveRemovedExposureSource(
    std::shared_ptr<const ExposureSourceLoss> loss,
    CompositionView::ViewStateHandle only_consumer
    = CompositionView::kInvalidViewStateHandle) -> void;

  [[nodiscard]] OXGN_VRTX_API auto InspectBindings(ViewId view_id) const
    -> const PostProcessFrameBindings*;
  [[nodiscard]] OXGN_VRTX_API auto ResolveBindingSlot(ViewId view_id) const
    -> ShaderVisibleIndex;
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetConfig() const noexcept
    -> const PostProcessConfig&
  {
    return resolved_config_.Settings();
  }
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetLastExecutionState() const noexcept
    -> const ExecutionState&
  {
    return last_execution_state_;
  }

private:
  friend struct testing::RendererPublicationProbe;
  auto ValidatePreparedExposure(ViewId view_id, const RenderContext& ctx,
    const PreparedExposure& prepared) const -> void;
  auto CaptureConfiguredExposure(ViewId view_id, RenderContext& ctx)
    -> const ExposureSettingsState&;
  struct CapturedExposureSettings {
    CompositionView::ViewStateHandle handle;
    ExposureSettingsState settings;
  };
  using ExposureCaptureKey
    = std::pair<ViewId, CompositionView::ViewStateHandle>;
  std::map<ExposureCaptureKey, CapturedExposureSettings>
    captured_exposure_settings_;
  std::unordered_map<ViewId, postprocess::ExposurePass::Source>
    captured_exposure_sources_;
  std::optional<frame::SequenceNumber> captured_control_frame_;
  auto BuildBindings(const Inputs& inputs,
    const ResolvedPostProcessConfig& config) const -> PostProcessFrameBindings;
  struct PublishedView {
    ShaderVisibleIndex slot { kInvalidShaderVisibleIndex };
    // Make the CPU cache's existing gap explicit; bindings retain GPU
    // alignment.
    std::array<std::byte,
      alignof(PostProcessFrameBindings) - sizeof(ShaderVisibleIndex)>
      slot_padding {};
    PostProcessFrameBindings bindings {};
  };
  static_assert(
    offsetof(PublishedView, bindings) == alignof(PostProcessFrameBindings));
  static_assert(sizeof(PublishedView)
    == sizeof(PostProcessFrameBindings) + alignof(PostProcessFrameBindings));

  struct PrecisionState {
    std::uint64_t lifetime { 0U };
    std::uint64_t settings_revision { 0U };
    std::uint64_t layout_revision { 0U };
    std::uint32_t expected_products { 0U };
    std::uint64_t transition_generation { 0U };
    CompositionView::ViewStateHandle source_handle {
      CompositionView::kInvalidViewStateHandle
    };
    std::uint64_t source_lifetime { 0U };
    std::uint64_t source_revision { 0U };
    std::uint64_t source_generation { 0U };
    bool source_pending { false };
    std::uint64_t epoch { 0U };
    std::uint64_t last_completed_frame { 0U };
    frame::SequenceNumber configured_frame { 0U };
    std::optional<frame::SequenceNumber> prepared_frame;
    std::uint64_t prepared_epoch { 0U };
    std::optional<frame::SequenceNumber> finalized_frame;
    std::uint64_t finalized_epoch { 0U };
    bool diagnostic { false };
    bool restart_streak { true };
    postprocess::ExposurePass::StateLease candidate;
  };
  std::unordered_map<CompositionView::ViewStateHandle, PrecisionState>
    precision_states_;
  struct PrecisionTicket {
    std::uint64_t layout_revision;
    std::uint64_t epoch;
    std::uint64_t transition_generation;
    bool normal_mode;
    bool auto_owner;
  };
  struct PendingExposureStatus {
    postprocess::ExposurePass::StateLease state;
    std::shared_ptr<graphics::GpuBufferReadback> readback;
    std::optional<ExposureTransitionToken> token;
    CompositionView::ViewStateHandle handle;
    std::uint64_t lifetime;
    std::uint64_t frame_sequence;
    std::uint64_t settings_revision;
    std::optional<PrecisionTicket> precision;
  };
  std::unordered_map<CompositionView::ViewStateHandle,
    std::deque<PendingExposureStatus>>
    pending_exposure_status_;
  std::unordered_map<CompositionView::ViewStateHandle, PendingExposureStatus>
    deferred_exposure_status_;
  auto IsExposureStatusNeeded(const PendingExposureStatus& job) const -> bool;
  auto IsPrecisionStatusNeeded(const PendingExposureStatus& job) const -> bool;
  auto QueueExposureStatus(PendingExposureStatus job) -> void;
  auto InvalidatePrecision(CompositionView::ViewStateHandle handle) -> void;
  auto CurrentExposureGeneration(CompositionView::ViewStateHandle handle,
    std::uint64_t lifetime) const -> std::uint64_t;
  auto DeferExposureStatus(PendingExposureStatus job) -> void;
  auto TryEnqueueExposureStatus(PendingExposureStatus job) -> bool;
  auto PollExposureStatus() -> void;
  OXGN_VRTX_API auto EnqueueExposureStatus(const ExposureTransitionToken& token,
    postprocess::ExposurePass::StateLease state, const RenderContext& ctx,
    std::uint64_t settings_revision) -> void;

  auto EnsurePublishResources() -> bool;
  auto EnsureMaskBinder() -> resources::TextureBinder*;

  Renderer& renderer_;
  observer_ptr<content::IAssetLoader> asset_loader_;
  std::unique_ptr<resources::TextureBinder> mask_binder_;
  std::array<
    std::vector<std::shared_ptr<const resources::TextureBinder::ReadyTexture>>,
    frame::kFramesInFlight.get()>
    frame_masks_;

  ResolvedPostProcessConfig resolved_config_;
  std::unordered_map<CompositionView::ViewStateHandle, ExposureSettingsState>
    exposure_settings_;
  ExposureSettingsState transient_exposure_settings_ {};
  frame::SequenceNumber current_sequence_ { 0U };
  frame::Slot current_slot_ { frame::kInvalidSlot };
  std::unique_ptr<internal::PerViewStructuredPublisher<PostProcessFrameBindings>>
    bindings_publisher_ {};
  std::unordered_map<ViewId, PublishedView> published_views_ {};
  ExecutionState last_execution_state_ {};
  std::unique_ptr<postprocess::ExposurePass> exposure_pass_ {};
  std::unique_ptr<postprocess::BloomPass> bloom_pass_ {};
  std::unique_ptr<postprocess::TonemapPass> tonemap_pass_ {};
};

} // namespace oxygen::vortex
