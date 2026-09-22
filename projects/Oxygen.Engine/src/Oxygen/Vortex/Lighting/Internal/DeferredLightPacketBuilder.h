//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Vortex/Lighting/Internal/LightEvaluationRecords.h>
#include <Oxygen/Vortex/Lighting/Types/DirectionalLightForwardData.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>

namespace oxygen::vortex::lighting::internal {

struct DeferredLightPacket {
  LocalLightKind kind { LocalLightKind::kPoint };
  observer_ptr<const ForwardLocalLightRecord> light;
  glm::mat4 light_world_matrix { 1.0F };
  bool spherical_proxy { false };
};

struct DeferredLightPacketSet {
  std::span<const DirectionalLightForwardData> directional;
  std::vector<DeferredLightPacket> local_lights;
  std::uint64_t selection_epoch { 0U };
};

class DeferredLightPacketBuilder {
public:
  [[nodiscard]] auto Build(const FrameLightSelection& selection,
    const LightEvaluationRecords& evaluation) const -> DeferredLightPacketSet;
};

} // namespace oxygen::vortex::lighting::internal
