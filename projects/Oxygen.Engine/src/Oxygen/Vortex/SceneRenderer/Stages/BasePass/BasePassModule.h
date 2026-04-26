//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <Oxygen/Vortex/RenderMode.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::graphics {
class Buffer;
class Framebuffer;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct RenderContext;
struct SceneTexturesConfig;
class Renderer;
class SceneTextures;
class BasePassMeshProcessor;

struct BasePassConfig {
  bool write_velocity { true };
  bool early_z_pass_done { true };
  ShadingMode shading_mode { ShadingMode::kDeferred };
  RenderMode render_mode { RenderMode::kSolid };
};

struct BasePassExecutionResult {
  bool published_base_pass_products { false };
  bool completed_velocity_for_dynamic_geometry { false };
  bool wrote_velocity_target { false };
  std::uint32_t draw_count { 0U };
};

class BasePassModule {
public:
  OXGN_VRTX_API explicit BasePassModule(
    Renderer& renderer, const SceneTexturesConfig& scene_textures_config);
  OXGN_VRTX_API ~BasePassModule();

  BasePassModule(const BasePassModule&) = delete;
  auto operator=(const BasePassModule&) -> BasePassModule& = delete;
  BasePassModule(BasePassModule&&) = delete;
  auto operator=(BasePassModule&&) -> BasePassModule& = delete;

  OXGN_VRTX_API auto Execute(RenderContext& ctx, SceneTextures& scene_textures)
    -> BasePassExecutionResult;
  OXGN_VRTX_API auto ExecuteWireframeOverlay(
    RenderContext& ctx, SceneTextures& scene_textures) -> std::uint32_t;
  OXGN_VRTX_API void SetConfig(const BasePassConfig& config);
  [[nodiscard]] OXGN_VRTX_API auto HasPublishedBasePassProducts() const -> bool;
  [[nodiscard]] OXGN_VRTX_API auto
  HasCompletedVelocityForDynamicGeometry() const -> bool;
  [[nodiscard]] OXGN_VRTX_API auto GetLastExecutionResult() const
    -> const BasePassExecutionResult&;

private:
  auto EnsureWireframeConstantsBuffer(Graphics& gfx) -> std::uint32_t;
  auto WriteWireframeConstants(Graphics& gfx, const RenderContext& ctx,
    bool compensate_exposure) -> std::uint32_t;
  auto ReleaseWireframeConstantsBuffer() -> void;

  Renderer& renderer_;
  BasePassConfig config_ {};
  BasePassExecutionResult last_execution_result_ {};
  std::unique_ptr<BasePassMeshProcessor> mesh_processor_;
  std::shared_ptr<oxygen::graphics::Buffer> wireframe_constants_buffer_ {};
  std::byte* wireframe_constants_mapped_ptr_ { nullptr };
  std::shared_ptr<oxygen::graphics::Framebuffer> framebuffer_ {};
  std::shared_ptr<oxygen::graphics::Framebuffer> color_clear_framebuffer_ {};
  std::shared_ptr<oxygen::graphics::Framebuffer> wireframe_framebuffer_ {};
  std::shared_ptr<oxygen::graphics::Texture> velocity_base_copy_ {};
  std::shared_ptr<oxygen::graphics::Texture>
    velocity_motion_vector_world_offset_ {};
  std::shared_ptr<oxygen::graphics::Framebuffer> velocity_aux_framebuffer_ {};
  std::shared_ptr<oxygen::graphics::Framebuffer>
    velocity_aux_color_clear_framebuffer_ {};
};

} // namespace oxygen::vortex
