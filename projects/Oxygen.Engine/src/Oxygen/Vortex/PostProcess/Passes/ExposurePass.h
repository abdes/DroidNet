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

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct RenderContext;
class Renderer;

namespace postprocess {

class ExposurePass {
public:
  struct Inputs {
    const graphics::Texture* scene_signal { nullptr };
    ShaderVisibleIndex scene_signal_srv { kInvalidShaderVisibleIndex };
  };

  struct Result {
    bool requested { false };
    bool executed { false };
    bool used_fixed_exposure { false };
    float exposure_value { 1.0F };
    const graphics::Buffer* exposure_buffer { nullptr };
    ShaderVisibleIndex exposure_buffer_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex exposure_buffer_uav { kInvalidShaderVisibleIndex };
  };

  OXGN_VRTX_API explicit ExposurePass(Renderer& renderer);
  OXGN_VRTX_API ~ExposurePass();

  ExposurePass(const ExposurePass&) = delete;
  auto operator=(const ExposurePass&) -> ExposurePass& = delete;
  ExposurePass(ExposurePass&&) = delete;
  auto operator=(ExposurePass&&) -> ExposurePass& = delete;

  [[nodiscard]] OXGN_VRTX_API auto Execute(
    RenderContext& ctx, const PostProcessConfig& config, const Inputs& inputs)
    -> Result;
  OXGN_VRTX_API auto RemoveViewState(ViewId view_id) -> void;

private:
  struct PerViewExposureState {
    std::shared_ptr<graphics::Buffer> buffer {};
    std::shared_ptr<graphics::Buffer> histogram_buffer {};
    ShaderVisibleIndex uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex histogram_uav_index { kInvalidShaderVisibleIndex };
    frame::SequenceNumber last_seen_sequence { 0U };
  };

  auto EnsurePipelines() -> void;
  auto EnsureHistogramBuffer(PerViewExposureState& state) -> void;
  auto EnsurePassConstantsBuffer() -> void;
  auto EnsureExposureInitUploadBuffer(
    graphics::CommandRecorder& recorder, const PostProcessConfig& config)
    -> void;
  auto EnsureExposureStateForView(
    RenderContext& ctx, graphics::CommandRecorder& recorder, ViewId view_id,
    const PostProcessConfig& config) -> PerViewExposureState&;
  auto UpdateHistogramConstants(graphics::CommandRecorder& recorder,
    const Inputs& inputs, const PostProcessConfig& config,
    const PerViewExposureState& state) -> void;
  auto UpdateAverageConstants(RenderContext& ctx,
    graphics::CommandRecorder& recorder, const PostProcessConfig& config,
    const PerViewExposureState& state) -> void;
  auto ReleaseExposureState(PerViewExposureState& state) -> void;
  auto ReleasePassConstantsBuffer() -> void;
  auto ReleaseExposureResources() -> void;

  Renderer& renderer_;
  std::optional<graphics::ComputePipelineDesc> clear_pipeline_ {};
  std::optional<graphics::ComputePipelineDesc> histogram_pipeline_ {};
  std::optional<graphics::ComputePipelineDesc> average_pipeline_ {};
  std::shared_ptr<graphics::Buffer> pass_constants_buffer_ {};
  void* pass_constants_mapped_ptr_ { nullptr };
  std::array<ShaderVisibleIndex, 8U> pass_constants_indices_ {};
  std::size_t pass_constants_slot_ { 0U };
  std::shared_ptr<graphics::Buffer> init_upload_buffer_ {};
  void* exposure_init_upload_mapped_ptr_ { nullptr };
  std::unordered_map<ViewId, PerViewExposureState> exposure_states_ {};
};

} // namespace postprocess

} // namespace oxygen::vortex
