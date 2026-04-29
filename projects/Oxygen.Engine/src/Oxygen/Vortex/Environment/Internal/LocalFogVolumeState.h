//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

class Renderer;
struct RenderContext;

namespace environment::internal {

struct LocalFogVolumeGpuInstance {
  std::array<std::uint32_t, 4> data0 {};
  std::array<std::uint32_t, 4> data1 {};
  std::array<std::uint32_t, 4> data2 {};

  [[nodiscard]] auto GetUniformScale() const noexcept -> float;
};

struct LocalFogVolumeCullingInstance {
  std::array<float, 4> sphere_world {};
};

class LocalFogVolumeState {
public:
  struct ViewProducts {
    ViewId view_id { kInvalidViewId };
    bool prepared { false };
    bool buffer_ready { false };
    bool tile_data_ready { false };
    std::uint32_t instance_count { 0U };
    std::uint32_t kept_instance_offset { 0U };
    ShaderVisibleIndex instance_buffer_slot { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex instance_culling_buffer_slot { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex tile_data_texture_slot { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex occupied_tile_buffer_slot { kInvalidShaderVisibleIndex };
    std::uint32_t tile_resolution_x { 0U };
    std::uint32_t tile_resolution_y { 0U };
    std::uint32_t max_instances_per_tile { 0U };
    std::uint32_t tile_capacity { 0U };
    observer_ptr<const graphics::Buffer> occupied_tile_draw_args_buffer {
      nullptr
    };
  };

  OXGN_VRTX_API explicit LocalFogVolumeState(Renderer& renderer);
  OXGN_VRTX_API ~LocalFogVolumeState();

  LocalFogVolumeState(const LocalFogVolumeState&) = delete;
  auto operator=(const LocalFogVolumeState&) -> LocalFogVolumeState& = delete;
  LocalFogVolumeState(LocalFogVolumeState&&) = delete;
  auto operator=(LocalFogVolumeState&&) -> LocalFogVolumeState& = delete;

  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  OXGN_VRTX_API auto Prepare(RenderContext& ctx) -> ViewProducts&;

  [[nodiscard]] OXGN_VRTX_NDAPI auto GetCurrentProducts() const noexcept
    -> const ViewProducts&
  {
    return current_products_;
  }

private:
  Renderer& renderer_;
  frame::SequenceNumber current_sequence_ { 0U };
  frame::Slot current_slot_ { frame::kInvalidSlot };
  upload::TransientStructuredBuffer instance_buffer_;
  upload::TransientStructuredBuffer instance_culling_buffer_;
  std::vector<LocalFogVolumeGpuInstance> cpu_instances_ {};
  std::vector<LocalFogVolumeCullingInstance> cpu_culling_instances_ {};
  ViewProducts current_products_ {};
};

} // namespace environment::internal
} // namespace oxygen::vortex
