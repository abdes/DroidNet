//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <unordered_map>
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

class ExposurePass {
public:
  //! Frame-retained resources; the pass owns their registry/descriptor
  //! lifetime.
  struct StateResources {
    std::uint64_t owner_lifetime { 0U };
    std::shared_ptr<graphics::Buffer> buffer;
    std::shared_ptr<graphics::Buffer> histogram_buffer;
    std::shared_ptr<graphics::Buffer> status_buffer;
    ShaderVisibleIndex status_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex histogram_uav_index { kInvalidShaderVisibleIndex };
  };
  using StateLease = std::shared_ptr<const StateResources>;

  struct Source {
    CompositionView::ViewStateHandle handle;
    PostProcessConfig config;
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
    bool metering_available { true };
    std::optional<ExposureTransitionToken> transition;
    const Source* source { nullptr };
    std::optional<ExposureTransitionError> rejection;
    std::uint64_t lifetime { 0U };
  };

  struct Result {
    StateLease state;
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

  OXGN_VRTX_API explicit ExposurePass(Renderer& renderer);
  OXGN_VRTX_API ~ExposurePass();

  ExposurePass(const ExposurePass&) = delete;
  auto operator=(const ExposurePass&) -> ExposurePass& = delete;
  ExposurePass(ExposurePass&&) = delete;
  auto operator=(ExposurePass&&) -> ExposurePass& = delete;

  [[nodiscard]] OXGN_VRTX_API auto Execute(RenderContext& ctx,
    const PostProcessConfig& config, const Inputs& inputs) -> Result;
  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  OXGN_VRTX_API auto RemoveViewState(
    CompositionView::ViewStateHandle view_state_handle) -> void;

private:
  friend struct ::oxygen::vortex::testing::RendererPublicationProbe;
  struct PerViewExposureState {
    StateLease latest;
    std::optional<frame::SequenceNumber> submitted_frame;
  };

  auto EnsurePipelines() -> void;
  auto RecordState(RenderContext& ctx, const PostProcessConfig& config,
    const Inputs& inputs, StateLease previous, StateLease borrowed,
    bool bootstrap = false) -> StateLease;
  auto AcquireState() -> std::shared_ptr<StateResources>;
  auto EnsureHistogramBuffer(StateResources& state) -> void;
  auto UpdateHistogramConstants(RenderContext& ctx,
    graphics::CommandRecorder& recorder, const Inputs& inputs,
    const PostProcessConfig& config, const StateResources& state) -> void;
  auto UpdateAverageConstants(RenderContext& ctx,
    graphics::CommandRecorder& recorder, const PostProcessConfig& config,
    const StateResources& state, ShaderVisibleIndex targets_srv,
    ShaderVisibleIndex previous_srv, const Inputs& inputs,
    ShaderVisibleIndex borrowed_srv, bool bootstrap, bool metering) -> void;
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
