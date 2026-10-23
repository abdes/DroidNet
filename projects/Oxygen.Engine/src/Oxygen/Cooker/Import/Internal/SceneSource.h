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
#include <variant>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/SceneBuild.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>

namespace oxygen::content::import::internal {

//! Version of the authored scene descriptor (`oxygen.scene-descriptor` schema).
/*!
 Independent of the cooked `data::pak::world::kSceneAssetVersion`: a cooked
 layout change recooks content without changing authored descriptors.
*/
inline constexpr uint32_t kSceneDescriptorVersion = 14;

//! A scene's source values and symbolic dependencies, before cooked linking.
struct SceneSource final {
  //! The virtual path of an asset an override names, before linking.
  struct AssetReference final {
    std::string virtual_path;
  };

  //! An authored override value, by the value type of its property.
  using OverrideValue
    = std::variant<bool, AssetReference, data::pak::geometry::LodPolicyRecord>;

  //! One authored override of a geometry property, before linking.
  /*!
   `property` is the schema entry the authored key names, never null. A part
   property names its surface in `part_id`; a property of the component itself
   has a nil `part_id`. The authored layout revision is not kept: linking
   resolves every surface against the geometry being cooked.
  */
  struct Override final {
    const data::schema::OverrideProperty* property = nullptr;
    data::SurfaceId part_id;
    OverrideValue value;
    std::string object_path;
  };

  struct Renderable final {
    uint32_t node_index = 0;
    std::string geometry;
    std::vector<Override> overrides;
  };

  struct Reference final {
    std::string virtual_path;
    std::optional<data::AssetType> type;
    std::string object_path;
  };

  //! A node that instances a scene, before linking.
  struct Instance final {
    uint32_t node_index = 0;
    std::string scene;
    std::string object_path;
  };

  //! An input mapping context the scene starts with, before linking.
  struct StartingInputContext final {
    std::string context;
    int32_t priority = 0;
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

  struct SkySphere final {
    data::pak::world::SkySphereEnvironmentRecord record {};
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
  //! In node order.
  std::vector<Instance> instances;
  //! In activation order.
  std::vector<StartingInputContext> starting_input_contexts;
  std::optional<data::pak::world::SkyAtmosphereEnvironmentRecord> atmosphere;
  std::optional<data::pak::world::BackgroundEnvironmentRecord> background;
  std::optional<Fog> fog;
  std::optional<SkyLight> sky_light;
  std::optional<SkySphere> sky_sphere;
  std::optional<PostProcess> post_process;

  OXGN_COOK_NDAPI static auto FromDescriptor(std::string_view bytes,
    const std::filesystem::path& source_path,
    std::vector<ImportDiagnostic>& diagnostics) -> std::optional<SceneSource>;
};

} // namespace oxygen::content::import::internal
