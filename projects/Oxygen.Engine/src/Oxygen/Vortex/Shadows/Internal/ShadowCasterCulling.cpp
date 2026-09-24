//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

#include <Oxygen/Core/Types/Frustum.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowCasterCulling.h>
#include <Oxygen/Vortex/Types/PassMask.h>

namespace oxygen::vortex::shadows::internal {

auto ShadowCasterCulling::BuildDrawCommands(
  const PreparedSceneFrame& prepared_scene,
  const glm::mat4& light_view_projection,
  const glm::vec4& light_position_and_inv_range) -> void
{
  draw_commands_.clear();
  candidate_count_ = 0U;
  const auto frustum = Frustum::FromViewProj(light_view_projection, true);

  const auto metadata = prepared_scene.GetDrawMetadata();
  draw_commands_.reserve(metadata.size());
  for (std::uint32_t draw_index = 0U; draw_index < metadata.size();
    ++draw_index) {
    const auto& draw = metadata[draw_index];
    if (!draw.flags.IsSet(PassMaskBit::kShadowCaster)) {
      continue;
    }
    ++candidate_count_;

    if (draw_index < prepared_scene.draw_bounding_spheres.size()) {
      const auto bounds = prepared_scene.draw_bounding_spheres[draw_index];
      // Missing/invalid bounds cannot prove exclusion. Keep those draws until
      // their producer supplies bounds, including all instances of a batch.
      if (std::isfinite(bounds.x) && std::isfinite(bounds.y)
        && std::isfinite(bounds.z) && std::isfinite(bounds.w)
        && bounds.w > 0.0F) {
        const auto center = glm::vec3(bounds);
        const auto radius = bounds.w * 1.01F + 1.0e-4F;
        if (!frustum.IntersectsSphere(center, radius)) {
          continue;
        }
        if (light_position_and_inv_range.w > 0.0F) {
          const auto delta = center - glm::vec3(light_position_and_inv_range);
          const auto combined_radius
            = radius + 1.0F / light_position_and_inv_range.w;
          if (glm::dot(delta, delta) > combined_radius * combined_radius) {
            continue;
          }
        }
      }
    }

    draw_commands_.push_back(DrawCommand {
      .draw_index = draw_index,
      .index_count
      = draw.is_indexed != 0U ? draw.index_count : draw.vertex_count,
      .instance_count = (std::max)(draw.instance_count, 1U),
      .start_index = draw.first_index,
      .base_vertex = draw.base_vertex,
      .start_instance = 0U,
      .is_indexed = draw.is_indexed != 0U,
    });
  }
}

auto ShadowCasterCulling::GetDrawCommands() const
  -> std::span<const DrawCommand>
{
  return draw_commands_;
}

} // namespace oxygen::vortex::shadows::internal
