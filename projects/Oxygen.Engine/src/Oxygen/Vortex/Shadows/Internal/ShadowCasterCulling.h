//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <vector>

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

  OXGN_VRTX_API auto BuildDrawCommands(
    const PreparedSceneFrame& prepared_scene) -> void;
  [[nodiscard]] OXGN_VRTX_API auto GetDrawCommands() const
    -> std::span<const DrawCommand>;

private:
  std::vector<DrawCommand> draw_commands_ {};
};

} // namespace oxygen::vortex::shadows::internal
