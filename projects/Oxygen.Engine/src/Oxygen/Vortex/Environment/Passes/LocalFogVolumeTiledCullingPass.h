//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/Environment/Internal/LocalFogVolumeState.h>
#include <Oxygen/Vortex/Environment/Passes/Generated.FogContracts.h>
#include <Oxygen/Vortex/Internal/ChangedLogLine.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
}

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

    OXGN_VRTX_API auto OnFrameStart(
      frame::SequenceNumber sequence, frame::Slot slot) -> void;

    LocalFogVolumeTiledCullingPass(const LocalFogVolumeTiledCullingPass&)
      = delete;
    auto operator=(const LocalFogVolumeTiledCullingPass&)
      -> LocalFogVolumeTiledCullingPass& = delete;
    LocalFogVolumeTiledCullingPass(LocalFogVolumeTiledCullingPass&&) = delete;
    auto operator=(LocalFogVolumeTiledCullingPass&&)
      -> LocalFogVolumeTiledCullingPass& = delete;

    [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
      graphics::CommandRecorder& recorder, const SceneTextures& scene_textures,
      internal::LocalFogVolumeState::ViewProducts& products) -> RecordState;

  private:
    using PassConstants = LocalFogTiledCullingPassConstants;
    static_assert(std::is_standard_layout_v<PassConstants>);
    //! Nine 16-byte rows, mirrored by the HLSL pass constants.
    static constexpr std::size_t kPassConstantsBytes = 144U;
    static_assert(sizeof(PassConstants) == kPassConstantsBytes);

    auto EnsurePassConstantsBuffer() -> upload::TransientStructuredBuffer*;
    auto EnsureTileDataTexture(std::uint32_t tile_resolution_x,
      std::uint32_t tile_resolution_y, std::uint32_t slice_count) -> bool;
    auto EnsureOccupiedTileDrawBuffers(std::uint32_t tile_count) -> bool;

    Renderer& renderer_;
    std::optional<upload::TransientStructuredBuffer> pass_constants_buffer_;
    std::shared_ptr<graphics::Texture> tile_data_texture_;
    std::shared_ptr<graphics::Buffer> occupied_tile_buffer_;
    std::shared_ptr<graphics::Buffer> indirect_args_buffer_;
    std::shared_ptr<graphics::Buffer> indirect_count_clear_buffer_;
    ShaderVisibleIndex tile_data_texture_uav_ { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex tile_data_texture_srv_ { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex occupied_tile_buffer_uav_ { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex occupied_tile_buffer_srv_ { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex indirect_args_buffer_uav_ { kInvalidShaderVisibleIndex };
    // The per-view HZB report is logged only when it changes.
    vortex::internal::ChangedLogLine hzb_log_;
    std::uint32_t tile_data_resolution_x_ { 0U };
    std::uint32_t tile_data_resolution_y_ { 0U };
    std::uint32_t tile_data_slice_count_ { 0U };
    std::uint32_t occupied_tile_capacity_ { 0U };
  };

} // namespace environment
} // namespace oxygen::vortex
