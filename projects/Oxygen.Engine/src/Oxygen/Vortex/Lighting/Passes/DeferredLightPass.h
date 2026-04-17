//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Vortex/Lighting/Internal/DeferredLightPacketBuilder.h>
#include <Oxygen/Vortex/Types/ShadowFrameBindings.h>

namespace oxygen::graphics {
class Buffer;
class Framebuffer;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct RenderContext;
class SceneTextures;
class Renderer;

namespace lighting {

class DeferredLightPass {
public:
  struct ExecutionState {
    bool consumed_packets { false };
    bool accumulated_into_scene_color { false };
    bool used_service_owned_geometry { false };
    bool used_outside_volume_local_lights { false };
    bool used_camera_inside_local_lights { false };
    bool used_non_perspective_local_lights { false };
    std::uint32_t directional_draw_count { 0U };
    std::uint32_t point_light_count { 0U };
    std::uint32_t spot_light_count { 0U };
    std::uint32_t local_light_count { 0U };
    std::uint32_t outside_volume_local_light_count { 0U };
    std::uint32_t camera_inside_local_light_count { 0U };
    std::uint32_t local_light_draw_count { 0U };
    std::uint32_t non_perspective_local_light_count { 0U };
    bool consumed_directional_shadow_product { false };
    bool directional_shadow_vsm_active { false };
    std::uint32_t directional_shadow_cascade_count { 0U };
    ShaderVisibleIndex directional_shadow_surface_srv {
      kInvalidShaderVisibleIndex
    };
  };

  explicit DeferredLightPass(Renderer& renderer);
  ~DeferredLightPass();

  [[nodiscard]] auto Record(RenderContext& ctx,
    const SceneTextures& scene_textures,
    const internal::DeferredLightPacketSet& packets,
    const ShadowFrameBindings* directional_shadow_bindings,
    const graphics::Texture* directional_shadow_surface) -> ExecutionState;

private:
  Renderer& renderer_;
  std::shared_ptr<graphics::Buffer> deferred_light_constants_buffer_ {};
  void* deferred_light_constants_mapped_ptr_ { nullptr };
  std::vector<ShaderVisibleIndex> deferred_light_constants_indices_ {};
  std::uint32_t deferred_light_constants_slot_count_ { 0U };
  std::shared_ptr<graphics::Framebuffer> directional_framebuffer_ {};
  std::shared_ptr<graphics::Framebuffer> local_framebuffer_ {};
  std::shared_ptr<graphics::Buffer> point_geometry_buffer_ {};
  std::shared_ptr<graphics::Buffer> spot_geometry_buffer_ {};
  ShaderVisibleIndex point_geometry_srv_ { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex spot_geometry_srv_ { kInvalidShaderVisibleIndex };
  std::uint32_t point_geometry_vertex_count_ { 0U };
  std::uint32_t spot_geometry_vertex_count_ { 0U };
};

} // namespace lighting

} // namespace oxygen::vortex
