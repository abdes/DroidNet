//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import {

//! One environment system record for the trailing scene block.
struct SceneEnvironmentSystem {
  uint32_t system_type = 0;
  std::vector<std::byte> record_bytes;
};

//! Intermediate scene build data produced by adapters.
struct SceneBuild final {
  std::vector<data::pak::world::NodeRecord> nodes;
  std::vector<std::byte> strings;

  std::vector<data::pak::world::RenderableRecord> renderables;
  std::vector<data::pak::world::MaterialOverrideRecord> material_overrides;
  std::vector<data::pak::world::LocalFogVolumeRecord> local_fog_volumes;
  std::vector<data::pak::world::PerspectiveCameraRecord> perspective_cameras;
  std::vector<data::pak::world::OrthographicCameraRecord> orthographic_cameras;
  std::vector<data::pak::world::DirectionalLightRecord> directional_lights;
  std::vector<data::pak::world::PointLightRecord> point_lights;
  std::vector<data::pak::world::SpotLightRecord> spot_lights;
};

} // namespace oxygen::content::import
