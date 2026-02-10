//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Renderer/Passes/ComputeRenderPass.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
class Texture;
class Buffer;
class ComputePipelineDesc;
} // namespace oxygen::graphics

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::engine {

struct RenderContext;

[[nodiscard]] OXGN_RNDR_API auto to_string(MeteringMode mode)
  -> std::string_view;

//! Configuration for the auto exposure compute pass.
struct AutoExposurePassConfig {
  static constexpr float kDefaultMinLogLuminance = -8.0F;
  static constexpr float kDefaultLogLuminanceRange = 20.0F;
  static constexpr float kDefaultLowPercentile = 0.4F;
  static constexpr float kDefaultHighPercentile = 0.95F;
  static constexpr float kDefaultAdaptationSpeedUp = 3.0F;
  static constexpr float kDefaultAdaptationSpeedDown = 3.0F;
  static constexpr float kDefaultTargetLuminance = 0.18F;
  static constexpr MeteringMode kDefaultMeteringMode = MeteringMode::kAverage;

  //! HDR source texture to analyze.
  std::shared_ptr<graphics::Texture> source_texture;

  //! Global histogram buffer (transient per-frame).
  std::shared_ptr<graphics::Buffer> histogram_buffer;

  //! Minimum log2 luminance for histogram mapping.
  float min_log_luminance { kDefaultMinLogLuminance };

  //! Log2 luminance range for histogram mapping.
  float log_luminance_range { kDefaultLogLuminanceRange };

  //! Low percentile for histogram outlier filtering (0.0 to 1.0).
  float low_percentile { kDefaultLowPercentile };

  //! High percentile for histogram outlier filtering (0.0 to 1.0).
  float high_percentile { kDefaultHighPercentile };

  //! Adaptation speed when scene gets brighter.
  float adaptation_speed_up { kDefaultAdaptationSpeedUp };

  //! Adaptation speed when scene gets darker.
  float adaptation_speed_down { kDefaultAdaptationSpeedDown };

  //! Target luminance (middle gray).
  float target_luminance { kDefaultTargetLuminance };

  //! Metering mode.
  MeteringMode metering_mode { kDefaultMeteringMode };

  //! Debug label for diagnostics.
  std::string debug_name { "AutoExposurePass" };
};

//! Compute pass that generates luminance histogram and calculates exposure.
/*!
 This pass implements histogram-based auto exposure with temporal smoothing.
 It consists of two stages:
 1. Histogram construction CS: Analyzes the HDR scene texture.
 2. Average and Smoothing CS: Calculates smoothed exposure for the current
 frame.
*/
class AutoExposurePass final : public ComputeRenderPass {
public:
  using Config = AutoExposurePassConfig;

  struct ExposureOutput {
    //! Shader-visible SRV index for the exposure state buffer.
    //! Layout:
    //!  - offset 0 = avg_lum (float)
    //!  - offset 4 = exposure_multiplier (float)
    //!  - offset 8 = ev100 (float)
    bindless::ShaderVisibleIndex exposure_state_srv_index {
      kInvalidShaderVisibleIndex
    };
  };

  OXGN_RNDR_API explicit AutoExposurePass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);

  OXGN_RNDR_API ~AutoExposurePass() override;

  //! Returns the exported exposure output for a given view.
  /*!\return Invalid indices if the pass has not produced output for that view.
   */
  [[nodiscard]] OXGN_RNDR_API auto GetExposureOutput(
    oxygen::ViewId view_id) const -> ExposureOutput;

  OXYGEN_MAKE_NON_COPYABLE(AutoExposurePass)
  OXYGEN_DEFAULT_MOVABLE(AutoExposurePass)

  //! Resets the exposure history for a specific view to a given average
  //! luminance.
  /*!
   This is useful when switching environments to prevent glossing/flashing due
   to the adaptation history being far from the new scene's luminance.
   */
  OXGN_RNDR_API auto ResetExposure(graphics::CommandRecorder& recorder,
    oxygen::ViewId view_id, float initial_avg_luminance) -> void;

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;

  //! Handled manually in DoExecute due to dual-stage dispatches
  auto CreatePipelineStateDesc() -> graphics::ComputePipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  auto EnsureHistogramBuffer() -> void;
  auto EnsureExposureStateForView(
    graphics::CommandRecorder& recorder, oxygen::ViewId view_id) -> void;
  auto EnsureExposureInitUploadBuffer(graphics::CommandRecorder& recorder)
    -> void;
  auto ReleasePassConstantsBuffer() noexcept -> void;
  auto UpdateHistogramConstants(graphics::CommandRecorder& recorder) -> void;
  auto UpdateAverageConstants(graphics::CommandRecorder& recorder) -> void;

  std::shared_ptr<Config> config_;
  observer_ptr<Graphics> graphics_;

  struct {
    std::optional<graphics::ComputePipelineDesc> histogram;
    std::optional<graphics::ComputePipelineDesc> average;
    std::optional<graphics::ComputePipelineDesc> clear;
  } pso_stages_;

  // Mapping state
  std::shared_ptr<graphics::Buffer> pass_constants_buffer_;
  void* pass_constants_mapped_ptr_ { nullptr };

  static constexpr uint32_t kPassConstantsStride = 256U;
  static constexpr size_t kPassConstantsSlots = 4U;

  std::array<bindless::ShaderVisibleIndex, kPassConstantsSlots>
    pass_constants_indices_;
  size_t pass_constants_slot_ { 0U };

  bindless::ShaderVisibleIndex histogram_uav_index_ {
    kInvalidShaderVisibleIndex
  };
  std::shared_ptr<graphics::Buffer> last_histogram_buffer_;

  struct PerViewExposureState {
    std::shared_ptr<graphics::Buffer> buffer;
    bindless::ShaderVisibleIndex uav_index { kInvalidShaderVisibleIndex };
    bindless::ShaderVisibleIndex srv_index { kInvalidShaderVisibleIndex };
  };

  std::unordered_map<oxygen::ViewId, PerViewExposureState> exposure_states_;
  PerViewExposureState* active_exposure_state_ { nullptr };

  // Small upload buffer used to initialize newly-created exposure buffers.
  std::shared_ptr<graphics::Buffer> init_upload_buffer_;
  void* exposure_init_upload_mapped_ptr_ { nullptr };

  std::shared_ptr<graphics::Texture> last_source_texture_;
  bindless::ShaderVisibleIndex source_texture_srv_index_ {
    kInvalidShaderVisibleIndex
  };
};

} // namespace oxygen::engine
