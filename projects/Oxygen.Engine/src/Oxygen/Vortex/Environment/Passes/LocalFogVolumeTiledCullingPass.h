//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/Environment/Internal/LocalFogVolumeState.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

namespace environment {

class LocalFogVolumeTiledCullingPass {
public:
  struct RecordState {
    bool requested { false };
    bool executed { false };
    bool sampled_scene_depth { false };
    bool consumed_instance_buffer { false };
    bool consumed_published_screen_hzb { false };
    std::uint32_t dispatch_count_x { 0U };
    std::uint32_t dispatch_count_y { 0U };
    std::uint32_t dispatch_count_z { 0U };
  };

  OXGN_VRTX_API explicit LocalFogVolumeTiledCullingPass(Renderer& renderer);
  OXGN_VRTX_API ~LocalFogVolumeTiledCullingPass();

  LocalFogVolumeTiledCullingPass(const LocalFogVolumeTiledCullingPass&) = delete;
  auto operator=(const LocalFogVolumeTiledCullingPass&)
    -> LocalFogVolumeTiledCullingPass& = delete;
  LocalFogVolumeTiledCullingPass(LocalFogVolumeTiledCullingPass&&) = delete;
  auto operator=(LocalFogVolumeTiledCullingPass&&)
    -> LocalFogVolumeTiledCullingPass& = delete;

  [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
    const SceneTextures& scene_textures,
    internal::LocalFogVolumeState::ViewProducts& products) -> RecordState;

private:
  struct alignas(16) PassConstants {
    std::uint32_t instance_buffer_slot { kInvalidShaderVisibleIndex.get() };
    std::uint32_t instance_culling_buffer_slot { kInvalidShaderVisibleIndex.get() };
    std::uint32_t tile_data_texture_slot { kInvalidShaderVisibleIndex.get() };
    std::uint32_t occupied_tile_buffer_slot { kInvalidShaderVisibleIndex.get() };
    std::uint32_t indirect_args_buffer_slot { kInvalidShaderVisibleIndex.get() };
    std::uint32_t instance_count { 0U };
    std::uint32_t tile_resolution_x { 0U };
    std::uint32_t tile_resolution_y { 0U };
    std::uint32_t max_instances_per_tile { 0U };
    std::uint32_t use_hzb { 1U };
    std::uint32_t _pad0 { 0U };
    std::uint32_t _pad1 { 0U };
    std::array<float, 4> left_plane { 0.0F, 0.0F, 0.0F, 0.0F };
    std::array<float, 4> right_plane { 0.0F, 0.0F, 0.0F, 0.0F };
    std::array<float, 4> top_plane { 0.0F, 0.0F, 0.0F, 0.0F };
    std::array<float, 4> bottom_plane { 0.0F, 0.0F, 0.0F, 0.0F };
    std::array<float, 4> near_plane { 0.0F, 0.0F, 0.0F, 0.0F };
    std::array<float, 2> view_to_tile_space_ratio { 1.0F, 1.0F };
    std::array<float, 2> _pad2 { 0.0F, 0.0F };
  };
  static_assert(std::is_standard_layout_v<PassConstants>);
  static_assert(sizeof(PassConstants) == 144U);

  auto EnsurePassConstantsBuffer() -> bool;
  auto EnsureTileDataTexture(std::uint32_t tile_resolution_x,
    std::uint32_t tile_resolution_y, std::uint32_t slice_count) -> bool;
  auto EnsureOccupiedTileDrawBuffers(std::uint32_t tile_count) -> bool;

  Renderer& renderer_;
  std::optional<upload::TransientStructuredBuffer> pass_constants_buffer_ {};
  std::shared_ptr<graphics::Texture> tile_data_texture_ {};
  std::shared_ptr<graphics::Buffer> occupied_tile_buffer_ {};
  std::shared_ptr<graphics::Buffer> indirect_args_buffer_ {};
  std::shared_ptr<graphics::Buffer> indirect_count_clear_buffer_ {};
  ShaderVisibleIndex tile_data_texture_uav_ { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex tile_data_texture_srv_ { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex occupied_tile_buffer_uav_ { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex occupied_tile_buffer_srv_ { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex indirect_args_buffer_uav_ { kInvalidShaderVisibleIndex };
  std::uint32_t tile_data_resolution_x_ { 0U };
  std::uint32_t tile_data_resolution_y_ { 0U };
  std::uint32_t tile_data_slice_count_ { 0U };
  std::uint32_t occupied_tile_capacity_ { 0U };
  std::vector<std::shared_ptr<graphics::Texture>> retired_textures_ {};
  std::vector<std::shared_ptr<graphics::Buffer>> retired_buffers_ {};
};

} // namespace environment
} // namespace oxygen::vortex
