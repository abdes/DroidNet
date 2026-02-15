//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <map>
#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Passes/ToneMapPass.h>
#include <Oxygen/Renderer/Pipeline/Internal/FrameViewPacket.h>
#include <Oxygen/Renderer/Pipeline/Internal/PipelineSettings.h>
#include <Oxygen/Renderer/Pipeline/RenderingPipeline.h>

namespace oxygen::renderer::internal {

class CompositionViewImpl;

class FramePlanBuilder {
public:
  struct Inputs {
    PipelineSettings frame_settings;
    std::optional<float> pending_auto_exposure_reset;
    observer_ptr<const engine::ToneMapPassConfig> tone_map_pass_config;
    observer_ptr<const engine::ShaderPassConfig> shader_pass_config;
  };

  FramePlanBuilder() = default;
  ~FramePlanBuilder();
  OXYGEN_MAKE_NON_COPYABLE(FramePlanBuilder)
  OXYGEN_MAKE_NON_MOVABLE(FramePlanBuilder)

  void BuildFrameViewPackets(observer_ptr<scene::Scene> scene,
    std::span<CompositionViewImpl* const> ordered_active_views,
    const Inputs& inputs);

  [[nodiscard]] auto GetFrameViewPackets() const
    -> const std::vector<FrameViewPacket>&
  {
    return frame_view_packets_;
  }

  [[nodiscard]] auto AutoExposureReset() const -> const std::optional<float>&
  {
    return frame_auto_exposure_reset_;
  }
  [[nodiscard]] auto GpuDebugPassEnabled() const -> bool
  {
    return frame_gpu_debug_pass_enabled_;
  }
  [[nodiscard]] auto WantAutoExposure() const -> bool
  {
    return frame_want_auto_exposure_;
  }
  [[nodiscard]] auto GetRenderMode() const -> RenderMode
  {
    return frame_render_mode_;
  }
  [[nodiscard]] auto ShaderDebugMode() const -> engine::ShaderDebugMode
  {
    return frame_shader_debug_mode_;
  }
  [[nodiscard]] auto WireColor() const -> const graphics::Color&
  {
    return frame_wire_color_;
  }
  [[nodiscard]] auto GpuDebugMouseDownPosition() const
    -> const std::optional<SubPixelPosition>&
  {
    return frame_gpu_debug_mouse_down_position_;
  }

  [[nodiscard]] auto FindFrameViewPacket(ViewId id) const
    -> const FrameViewPacket*;

private:
  struct SkyState {
    bool sky_atmo_enabled { false };
    bool sky_sphere_enabled { false };
  };

  [[nodiscard]] auto EvaluateSkyState(observer_ptr<scene::Scene> scene) const
    -> SkyState;

  [[nodiscard]] auto EvaluateViewRenderPlan(const CompositionViewImpl& view,
    const SkyState& sky_state, const Inputs& inputs) const -> ViewRenderPlan;

  std::vector<FrameViewPacket> frame_view_packets_;
  std::map<ViewId, size_t> frame_view_packet_index_;
  std::optional<float> frame_auto_exposure_reset_;
  bool frame_gpu_debug_pass_enabled_ { true };
  bool frame_want_auto_exposure_ { false };
  RenderMode frame_render_mode_ { RenderMode::kSolid };
  engine::ShaderDebugMode frame_shader_debug_mode_ {
    engine::ShaderDebugMode::kDisabled
  };
  graphics::Color frame_wire_color_ { 1.0F, 1.0F, 1.0F, 1.0F };
  std::optional<SubPixelPosition> frame_gpu_debug_mouse_down_position_;
};

} // namespace oxygen::renderer::internal
