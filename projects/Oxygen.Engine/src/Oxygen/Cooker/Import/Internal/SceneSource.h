//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/SceneBuild.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/PakFormat_world.h>

namespace oxygen::content::import::internal {

//! A scene's source values and symbolic dependencies, before cooked linking.
struct SceneSource final {
  struct MaterialAssignment final {
    data::MaterialSlotId slot_id {};
    std::string material;
    std::string layout_revision;
  };

  struct Renderable final {
    uint32_t node_index = 0;
    bool visible = true;
    std::string geometry;
    std::vector<MaterialAssignment> materials;
  };

  struct Reference final {
    std::string virtual_path;
    std::optional<data::AssetType> type;
    std::string object_path;
  };

  struct Fog final {
    data::pak::world::FogEnvironmentRecord record {};
    std::optional<std::string> cubemap;
  };

  struct SkyLight final {
    data::pak::world::SkyLightEnvironmentRecord record {};
    std::optional<std::string> cubemap;
  };

  struct PostProcess final {
    data::pak::world::PostProcessVolumeEnvironmentRecord record {};
    std::vector<data::pak::world::ExposureCompensationKeyRecord> curve;
    std::optional<std::string> metering_mask;
  };

  std::string name;
  std::optional<bool> content_hashing;
  SceneBuild build;
  std::vector<Renderable> renderables;
  std::vector<Reference> references;
  std::optional<data::pak::world::SkyAtmosphereEnvironmentRecord> atmosphere;
  std::optional<data::pak::world::BackgroundEnvironmentRecord> background;
  std::optional<Fog> fog;
  std::optional<SkyLight> sky_light;
  std::optional<PostProcess> post_process;

  OXGN_COOK_NDAPI static auto FromDescriptor(std::string_view bytes,
    const std::filesystem::path& source_path,
    std::vector<ImportDiagnostic>& diagnostics) -> std::optional<SceneSource>;
};

} // namespace oxygen::content::import::internal
