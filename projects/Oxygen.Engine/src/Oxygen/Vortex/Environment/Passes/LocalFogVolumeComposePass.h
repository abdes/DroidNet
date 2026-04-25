//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>

#include <Oxygen/Vortex/Environment/Internal/LocalFogVolumeState.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

namespace environment {

class LocalFogVolumeComposePass {
public:
  struct RecordState {
    bool requested { false };
    bool executed { false };
    std::uint32_t draw_count { 0U };
    bool bound_scene_color { false };
    bool sampled_scene_depth { false };
    bool consumed_instance_buffer { false };
  };

  OXGN_VRTX_API explicit LocalFogVolumeComposePass(Renderer& renderer);
  OXGN_VRTX_API ~LocalFogVolumeComposePass();

  LocalFogVolumeComposePass(const LocalFogVolumeComposePass&) = delete;
  auto operator=(const LocalFogVolumeComposePass&)
    -> LocalFogVolumeComposePass& = delete;
  LocalFogVolumeComposePass(LocalFogVolumeComposePass&&) = delete;
  auto operator=(LocalFogVolumeComposePass&&)
    -> LocalFogVolumeComposePass& = delete;

  [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
    const SceneTextures& scene_textures,
    const internal::LocalFogVolumeState::ViewProducts& products) -> RecordState;

private:
  struct PassConstants {
    std::uint32_t instance_buffer_slot { kInvalidShaderVisibleIndex.get() };
    std::uint32_t tile_data_texture_slot { kInvalidShaderVisibleIndex.get() };
    std::uint32_t occupied_tile_buffer_slot { kInvalidShaderVisibleIndex.get() };
    std::uint32_t tile_resolution_x { 0U };
    std::uint32_t tile_resolution_y { 0U };
    std::uint32_t max_instances_per_tile { 0U };
    std::uint32_t instance_count { 0U };
    std::uint32_t tile_pixel_size { 0U };
    std::uint32_t view_width { 0U };
    std::uint32_t view_height { 0U };
    float global_start_distance_meters { 0.0F };
    float start_depth_z { 0.0F };
    std::uint32_t _pad0 { 0U };
    std::uint32_t _pad1 { 0U };
  };

  auto EnsurePassConstantsBuffer() -> bool;

  Renderer& renderer_;
  std::optional<upload::TransientStructuredBuffer> pass_constants_buffer_ {};
};

} // namespace environment
} // namespace oxygen::vortex
