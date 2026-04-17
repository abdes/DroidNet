//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Vortex/Lighting/Internal/DeferredLightPacketBuilder.h>

namespace oxygen::vortex::lighting::internal {

namespace {

auto MakeScaleMatrix(const glm::vec3 scale) -> glm::mat4
{
  return glm::scale(glm::mat4 { 1.0F }, scale);
}

auto BuildLightWorldMatrix(const FrameLocalLightSelection& selection) -> glm::mat4
{
  const auto translation
    = glm::translate(glm::mat4 { 1.0F }, selection.position);
  if (selection.kind == LocalLightKind::kPoint) {
    return translation * MakeScaleMatrix(glm::vec3 { selection.range });
  }

  const auto base_radius = (std::max)(
    selection.range * std::sqrt((std::max)(1.0F - selection.outer_cone_cos
          * selection.outer_cone_cos,
        0.0F)),
    0.001F);
  return translation
    * MakeScaleMatrix(glm::vec3 { base_radius, selection.range, base_radius });
}

} // namespace

auto DeferredLightPacketBuilder::Build(const FrameLightSelection& selection) const
  -> DeferredLightPacketSet
{
  auto packets = DeferredLightPacketSet { .selection_epoch = selection.selection_epoch };
  if (selection.directional_light.has_value()) {
    packets.directional = DirectionalLightForwardData::FromSelection(
      *selection.directional_light);
  }

  packets.local_lights.reserve(selection.local_lights.size());
  for (const auto& light : selection.local_lights) {
    packets.local_lights.push_back(DeferredLightPacket {
      .kind = light.kind,
      .light_position_and_radius = glm::vec4(light.position, light.range),
      .light_color_and_intensity = glm::vec4(light.color, light.intensity),
      .light_direction_and_falloff
      = glm::vec4(light.direction, light.decay_exponent),
      .spot_angles
      = glm::vec4(light.inner_cone_cos, light.outer_cone_cos, 0.0F, 0.0F),
      .light_world_matrix = BuildLightWorldMatrix(light),
    });
  }
  return packets;
}

} // namespace oxygen::vortex::lighting::internal
