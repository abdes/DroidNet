//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/NodePathTable.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Data/DescriptorStrings.h>
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
  //! The scene descriptor's string table; node names are offsets into it.
  data::DescriptorStringsBuilder strings;

  std::vector<data::pak::world::RenderableRecord> renderables;
  //! The overrides of the renderables and the changes of instanced nodes; a
  //! LOD policy value is an index into `override_lod_policies`.
  std::vector<data::pak::world::OverrideRecord> overrides;
  std::vector<data::pak::geometry::LodPolicyRecord> override_lod_policies;
  std::vector<data::pak::world::LocalFogVolumeRecord> local_fog_volumes;
  std::vector<data::pak::world::PerspectiveCameraRecord> perspective_cameras;
  std::vector<data::pak::world::OrthographicCameraRecord> orthographic_cameras;
  std::vector<data::pak::world::DirectionalLightRecord> directional_lights;
  std::vector<data::pak::world::PointLightRecord> point_lights;
  std::vector<data::pak::world::SpotLightRecord> spot_lights;

  data::pak::world::SceneAssetFlags flags
    = data::pak::world::SceneAssetFlags::kNone;
  data::pak::world::NodeRef starting_camera {
    .node_index = data::pak::world::kNoStartingCamera,
  };
  //! The paths every node reference of the scene names.
  internal::NodePathTable node_paths;
  //! Sorted by node; one instance per node.
  std::vector<data::pak::world::SceneInstanceRecord> instances;
  //! In activation order.
  std::vector<data::pak::world::StartingInputContextRecord>
    starting_input_contexts;
  //! For a model scene: the anchor and ID of each node emitted.
  std::vector<ModelPublishedNode> model_nodes;
};

} // namespace oxygen::content::import
