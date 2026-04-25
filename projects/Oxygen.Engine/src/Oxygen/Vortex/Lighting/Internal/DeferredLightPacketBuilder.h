//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Vortex/Lighting/Types/DirectionalLightForwardData.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>

namespace oxygen::vortex::lighting::internal {

struct DeferredLightPacket {
  LocalLightKind kind { LocalLightKind::kPoint };
  glm::vec4 light_position_and_radius { 0.0F };
  glm::vec4 light_color_and_intensity { 0.0F };
  glm::vec4 light_direction_and_falloff { 0.0F };
  glm::vec4 spot_angles { 0.0F };
  glm::mat4 light_world_matrix { 1.0F };
};

struct DeferredLightPacketSet {
  std::optional<DirectionalLightForwardData> directional {};
  std::vector<DeferredLightPacket> local_lights {};
  std::uint64_t selection_epoch { 0U };
};

class DeferredLightPacketBuilder {
public:
  [[nodiscard]] auto Build(const FrameLightSelection& selection) const
    -> DeferredLightPacketSet;
};

} // namespace oxygen::vortex::lighting::internal
