//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>
#include <Oxygen/Vortex/Types/ExposureTransition.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
struct ExposureTargetData;
namespace testing {
  struct RendererPublicationProbe;
}
namespace internal {
  template <typename Payload> class PerViewStructuredPublisher;
}

namespace postprocess {

  struct ExposureStateResources {
    std::uint64_t owner_lifetime { 0U };
    CompositionView::ViewStateHandle borrowed_from {
      CompositionView::kInvalidViewStateHandle
    };
    std::uint64_t borrowed_lifetime { 0U };
    std::shared_ptr<graphics::Buffer> buffer;
    std::shared_ptr<graphics::Buffer> histogram_buffer;
    std::shared_ptr<graphics::Buffer> status_buffer;
    ShaderVisibleIndex status_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex histogram_uav_index { kInvalidShaderVisibleIndex };
  };
  struct FrameExposureResources {
    std::shared_ptr<graphics::Buffer> buffer;
    ShaderVisibleIndex srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex uav_index { kInvalidShaderVisibleIndex };
    std::shared_ptr<ExposureStateResources> current_state;
    std::shared_ptr<const ExposureStateResources> selected_history;
    std::shared_ptr<const ExposureStateResources> precision_history;
    std::shared_ptr<const ExposureStateResources> qualified_candidate;
    std::shared_ptr<graphics::Buffer> suitability_buffer;
    ShaderVisibleIndex suitability_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex suitability_uav { kInvalidShaderVisibleIndex };
    std::shared_ptr<graphics::Buffer> conversion_buffer;
    ShaderVisibleIndex conversion_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex conversion_uav { kInvalidShaderVisibleIndex };
  };
class ExposurePass {
public:
  using StateResources = ExposureStateResources;
  using StateLease = std::shared_ptr<const StateResources>;
  using FrameResources = FrameExposureResources;
  using FrameLease = std::shared_ptr<const FrameResources>;

  struct Source {
    CompositionView::ViewStateHandle handle;
    ResolvedPostProcessConfig config;
    std::optional<ExposureTransitionToken> transition;
    std::optional<ExposureTransitionError> rejection;
    std::uint64_t lifetime { 0U };
  };

  struct Inputs {
    const graphics::Texture* scene_signal { nullptr };
    ShaderVisibleIndex scene_signal_srv { kInvalidShaderVisibleIndex };
    const graphics::Texture* metering_mask { nullptr };
    ShaderVisibleIndex metering_mask_srv { kInvalidShaderVisibleIndex };
    //! Scale of the supplied signal; scene-referred fixtures use one.
    float one_over_pre_exposure { 1.0F };
    const FrameResources* frame_exposure { nullptr };
    bool metering_available { true };
    std::optional<ExposureTransitionToken> transition;
    const Source* source { nullptr };
    std::optional<ExposureTransitionError> rejection;
    std::uint64_t lifetime { 0U };
  };

  struct Result {
    StateLease state;
    FrameLease frame;
    bool requested { false };
    bool executed { false };
    bool used_fixed_exposure { false };
    bool borrowed_exposure { false };
    float exposure_value { 1.0F };
    const graphics::Buffer* exposure_buffer { nullptr };
    const graphics::Buffer* histogram_buffer { nullptr };
    ShaderVisibleIndex exposure_buffer_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex exposure_buffer_uav { kInvalidShaderVisibleIndex };
  };

  struct FrameInputs {
    bool use_fp32 { false };
    StateLease qualified_candidate;
    const Source* source { nullptr };
    std::optional<ExposureTransitionToken> transition;
    std::optional<ExposureTransitionError> rejection;
    std::uint64_t lifetime { 0U };
  };

  struct HdrProduct {
    const graphics::Texture* texture { nullptr };
    ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
    std::uint32_t id { 0U };
    bool metering { false };
    bool coverage { false };
    bool transmittance { false };
    float error_budget_share { 1.0F };
  };

  //! Evaluate FP32 reference products without granting normal-mode admission.
  enum class SuitabilityScale { kCandidate, kCurrentFrame };
  [[nodiscard]] OXGN_VRTX_API auto EvaluateFp16Products(RenderContext& ctx,
    const FrameLease& frame, const ResolvedPostProcessConfig& config,
    std::span<const HdrProduct> products, const Inputs& metering,
    SuitabilityScale scale = SuitabilityScale::kCandidate) -> bool;
  struct EligibilityInputs {
    std::uint64_t product_layout_revision { 0U };
    std::uint32_t expected_products { 0U };
    bool invalidate_previous { false };
  };
  //! Publish bounded GPU eligibility after a submitted complete product check.
  [[nodiscard]] OXGN_VRTX_API auto FinalizeFp16Suitability(RenderContext& ctx,
    const FrameLease& frame, const EligibilityInputs& inputs) -> bool;

  //! Check the current frame's FP32 SceneColor before writing its FP16 resolve.
  //! Call after the exposure solve. True means submitted, not suitable: the GPU
  //! suitability report gates all destination writes. A rejected destination
  //! remains untouched and must not be published as valid or sampled
  //! downstream.
  [[nodiscard]] OXGN_VRTX_API auto ConvertCheckedSceneColor(RenderContext& ctx,
    const FrameLease& frame, const ResolvedPostProcessConfig& config,
    const Inputs& inputs, graphics::Texture& destination,
    ShaderVisibleIndex destination_uav) -> bool;

  OXGN_VRTX_API explicit ExposurePass(Renderer& renderer);
  OXGN_VRTX_API ~ExposurePass();

  ExposurePass(const ExposurePass&) = delete;
  auto operator=(const ExposurePass&) -> ExposurePass& = delete;
  ExposurePass(ExposurePass&&) = delete;
  auto operator=(ExposurePass&&) -> ExposurePass& = delete;

  [[nodiscard]] OXGN_VRTX_API auto Execute(RenderContext& ctx,
    const ResolvedPostProcessConfig& config, const Inputs& inputs) -> Result;
  //! GPU-only numerical resolve; publication must precede dependent HDR work.
  [[nodiscard]] OXGN_VRTX_API auto ResolveFrame(RenderContext& ctx,
    const ResolvedPostProcessConfig& config, const FrameInputs& inputs)
    -> FrameLease;
  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  OXGN_VRTX_API auto RemoveViewState(
    CompositionView::ViewStateHandle view_state_handle) -> void;
  OXGN_VRTX_API auto PreserveRemovedSource(
    std::shared_ptr<const ExposureSourceLoss> loss, const Source& source,
    CompositionView::ViewStateHandle only_consumer
    = CompositionView::kInvalidViewStateHandle) -> void;

private:
  friend struct ::oxygen::vortex::testing::RendererPublicationProbe;
  struct PerViewExposureState {
    StateLease latest;
    std::optional<frame::SequenceNumber> submitted_frame;
    struct BorrowSelection {
      StateLease state;
      Source source;
      std::uint64_t consumer_lifetime;
    };
    std::optional<BorrowSelection> selected_borrow;
    struct PendingSourceLoss {
      std::shared_ptr<const ExposureSourceLoss> event;
      Source source;
      StateLease fallback;
      std::uint64_t consumer_lifetime;
    };
    std::optional<PendingSourceLoss> source_loss;
  };

  auto EnsurePipelines() -> void;
  auto PreparePublishers(RenderContext& ctx) -> void;
  auto AcquireFrame() -> std::shared_ptr<FrameResources>;
  auto RestoreFrameFallback(RenderContext& ctx,
    const ResolvedPostProcessConfig& config, const FrameResources& frame,
    StateLease fallback) -> bool;
  auto RecordState(RenderContext& ctx, const ResolvedPostProcessConfig& config,
    const Inputs& inputs, StateLease previous, StateLease borrowed,
    bool bootstrap = false, bool source_loss = false,
    std::shared_ptr<StateResources> reserved = {}) -> StateLease;
  auto AcquireState() -> std::shared_ptr<StateResources>;
  auto EnsureHistogramBuffer(StateResources& state) -> void;
  auto UpdateHistogramConstants(RenderContext& ctx,
    graphics::CommandRecorder& recorder, const Inputs& inputs,
    const ResolvedPostProcessConfig& config, const StateResources& state)
    -> void;
  auto UpdateAverageConstants(RenderContext& ctx,
    graphics::CommandRecorder& recorder,
    const ResolvedPostProcessConfig& config, const StateResources& state,
    ShaderVisibleIndex targets_srv, ShaderVisibleIndex previous_srv,
    const Inputs& inputs, ShaderVisibleIndex borrowed_srv, bool bootstrap,
    bool metering, bool source_loss) -> void;
  auto ReleaseExposureResources() -> void;

  Renderer& renderer_;
  std::unique_ptr<::oxygen::vortex::internal::PerViewStructuredPublisher<
    ExposureTargetData>>
    target_publisher_;
  std::optional<frame::SequenceNumber> target_frame_;
  std::unique_ptr<::oxygen::vortex::internal::PerViewStructuredPublisher<
    std::array<std::uint32_t, 16U>>>
    constants_publisher_;
  std::unique_ptr<::oxygen::vortex::internal::PerViewStructuredPublisher<
    std::array<std::uint32_t, 28U>>>
    average_constants_publisher_;
  std::optional<graphics::ComputePipelineDesc> clear_pipeline_ {};
  std::optional<graphics::ComputePipelineDesc> histogram_pipeline_ {};
  std::optional<graphics::ComputePipelineDesc> average_pipeline_ {};
  std::optional<graphics::ComputePipelineDesc> frame_pipeline_ {};
  std::optional<graphics::ComputePipelineDesc> fallback_pipeline_ {};
  std::optional<graphics::ComputePipelineDesc> convert_pipeline_ {};
  std::optional<graphics::ComputePipelineDesc> eligibility_pipeline_ {};
  std::array<std::optional<graphics::ComputePipelineDesc>, 4>
    suitability_pipelines_;
  std::unique_ptr<::oxygen::vortex::internal::PerViewStructuredPublisher<
    std::array<std::uint32_t, 20U>>>
    suitability_constants_publisher_;
  std::unique_ptr<::oxygen::vortex::internal::PerViewStructuredPublisher<
    std::array<std::uint32_t, 8U>>>
    conversion_constants_publisher_;
  std::unique_ptr<::oxygen::vortex::internal::PerViewStructuredPublisher<
    std::array<std::uint32_t, 12U>>>
    frame_constants_publisher_;
  std::vector<std::shared_ptr<FrameResources>> frame_pool_;
  std::map<std::pair<ViewId, CompositionView::ViewStateHandle>, FrameLease>
    resolved_frames_;
  std::unordered_set<const FrameResources*> submitted_suitability_;
  std::unordered_set<const FrameResources*> submitted_conversion_;
  std::array<std::vector<FrameLease>, frame::kFramesInFlight.get()>
    frame_bindings_;
  std::vector<std::shared_ptr<StateResources>> state_pool_;
  std::array<std::vector<StateLease>, frame::kFramesInFlight.get()>
    frame_states_;
  std::unordered_map<CompositionView::ViewStateHandle, StateLease>
    prior_states_;
  std::unordered_map<CompositionView::ViewStateHandle, StateLease>
    bootstrap_states_;
  std::unordered_map<CompositionView::ViewStateHandle, PerViewExposureState>
    exposure_states_ {};
};

} // namespace postprocess

} // namespace oxygen::vortex
