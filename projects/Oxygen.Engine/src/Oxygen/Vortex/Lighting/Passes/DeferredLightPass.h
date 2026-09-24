//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Lighting/Internal/DeferredLightPacketBuilder.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowFrameData.h>

namespace oxygen::graphics {
class Buffer;
class Framebuffer;
} // namespace oxygen::graphics

namespace oxygen::graphics {
class CommandRecorder;
}

namespace oxygen::vortex {

struct RenderContext;
class SceneTextures;
class Renderer;
struct DeferredLightConstants;

namespace lighting {

  namespace internal {
    class DeferredLightConstantsPublisher;
  }

  class DeferredLightPass {
  public:
    struct ExecutionState {
      bool recording_succeeded { true };
      bool consumed_packets { false };
      bool accumulated_into_scene_color { false };
      bool used_service_owned_geometry { false };
      bool used_outside_volume_local_lights { false };
      bool used_camera_inside_local_lights { false };
      bool used_non_perspective_local_lights { false };
      bool consumed_static_sky_light_product { false };
      std::uint32_t directional_draw_count { 0U };
      std::uint32_t static_sky_light_draw_count { 0U };
      std::uint32_t point_light_count { 0U };
      std::uint32_t spot_light_count { 0U };
      std::uint32_t local_light_count { 0U };
      std::uint32_t outside_volume_local_light_count { 0U };
      std::uint32_t camera_inside_local_light_count { 0U };
      std::uint32_t local_light_draw_count { 0U };
      std::uint32_t punctual_point_light_draw_count { 0U };
      std::uint32_t pipeline_bind_count { 0U };
      std::uint32_t non_perspective_local_light_count { 0U };
      bool consumed_directional_shadow_product { false };
      bool directional_shadow_vsm_active { false };
      std::uint32_t directional_shadow_cascade_count { 0U };
      std::vector<ShaderVisibleIndex> directional_shadow_surface_srvs;
      bool consumed_spot_shadow_product { false };
      std::uint32_t spot_shadow_count { 0U };
      ShaderVisibleIndex spot_shadow_surface_srv { kInvalidShaderVisibleIndex };
      bool consumed_point_shadow_product { false };
      std::uint32_t point_shadow_count { 0U };
      ShaderVisibleIndex point_shadow_surface_srv {
        kInvalidShaderVisibleIndex
      };
    };

    explicit DeferredLightPass(Renderer& renderer);
    ~DeferredLightPass();
    auto OnFrameStart(frame::SequenceNumber sequence, frame::Slot slot) -> void;

    [[nodiscard]] auto Record(RenderContext& ctx,
      graphics::CommandRecorder& recorder, const SceneTextures& scene_textures,
      const internal::DeferredLightPacketSet& packets,
      const ShadowFrameData* shadow_data,
      std::span<const std::shared_ptr<graphics::Texture>>
        directional_shadow_surfaces,
      std::span<const std::shared_ptr<graphics::Texture>> spot_shadow_surfaces,
      std::span<const std::shared_ptr<graphics::Texture>> point_shadow_surfaces,
      bool static_sky_light_available) -> ExecutionState;

  private:
    Renderer& renderer_;
    std::unique_ptr<internal::DeferredLightConstantsPublisher>
      constants_publisher_;
    std::shared_ptr<graphics::Framebuffer> directional_framebuffer_;
    std::shared_ptr<graphics::Framebuffer> local_framebuffer_;
    std::shared_ptr<graphics::Buffer> point_geometry_buffer_;
    std::shared_ptr<graphics::Buffer> spot_geometry_buffer_;
    ShaderVisibleIndex point_geometry_srv_ { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex spot_geometry_srv_ { kInvalidShaderVisibleIndex };
    std::uint32_t point_geometry_vertex_count_ { 0U };
    std::uint32_t spot_geometry_vertex_count_ { 0U };
    // CPU scratch is consumed synchronously during Record; staged GPU data
    // retains the existing frame-slot ownership contract.
    std::vector<DeferredLightConstants> constants_scratch_;
    std::vector<std::size_t> draw_order_scratch_;
  };

} // namespace lighting

} // namespace oxygen::vortex
