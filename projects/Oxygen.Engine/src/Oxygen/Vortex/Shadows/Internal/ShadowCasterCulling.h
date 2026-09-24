//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <vector>

#include <glm/mat4x4.hpp>

#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::shadows::internal {

class ShadowCasterCulling {
public:
  OXGN_VRTX_API ShadowCasterCulling() = default;
  OXGN_VRTX_API ~ShadowCasterCulling() = default;

  ShadowCasterCulling(const ShadowCasterCulling&) = delete;
  auto operator=(const ShadowCasterCulling&) -> ShadowCasterCulling& = delete;
  ShadowCasterCulling(ShadowCasterCulling&&) = delete;
  auto operator=(ShadowCasterCulling&&) -> ShadowCasterCulling& = delete;

  //! Cull world-space draw bounds against one shadow projection and light
  //! range. A zero inverse range identifies a directional projection.
  OXGN_VRTX_API auto BuildDrawCommands(const PreparedSceneFrame& prepared_scene,
    const glm::mat4& light_view_projection,
    const glm::vec4& light_position_and_inv_range = glm::vec4 { 0.0F }) -> void;
  [[nodiscard]] OXGN_VRTX_API auto GetDrawCommands() const
    -> std::span<const DrawCommand>;
  [[nodiscard]] auto GetCandidateCount() const noexcept -> std::uint32_t
  {
    return candidate_count_;
  }

private:
  std::vector<DrawCommand> draw_commands_;
  std::uint32_t candidate_count_ { 0U };
};

} // namespace oxygen::vortex::shadows::internal
