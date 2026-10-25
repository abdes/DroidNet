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

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/SceneBuild.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>

namespace oxygen::content::import::internal {

//! Version of the authored scene descriptor (`oxygen.scene-descriptor` schema).
/*!
 Independent of the cooked `data::pak::world::kSceneAssetVersion`: a cooked
 layout change recooks content without changing authored descriptors.
*/
inline constexpr uint32_t kSceneDescriptorVersion = 15;

//! The code of a descriptor property that is not a finite value inside its
//! generated range, or not a declared member of its enum.
inline constexpr std::string_view kScenePropertyOutOfRangeCode
  = "scene.descriptor.property_out_of_range";

//! A scene's source values and symbolic dependencies, before cooked linking.
struct SceneSource final {
  //! The virtual path of an asset an override names, before linking.
  struct AssetReference final {
    std::string virtual_path;
  };

  //! An authored override value, by the value type of its property: an
  //! asset-sourced property's bool, asset or LOD policy, or the encoded change
  //! of any other property.
  using OverrideValue = std::variant<bool, AssetReference,
    data::pak::geometry::LodPolicyRecord, data::pak::world::OverrideValue>;

  //! One authored override of a renderable, or change of an instanced node,
  //! before linking.
  /*!
   `property` is the schema entry the authored key names, never null. A part
   property names its surface in `part_id`; a property of the component itself
   has a nil `part_id`. A renderable's override resolves its surface against
   the geometry being cooked; a change of an instanced node keeps its authored
   `layout_revision`, since its geometry is the instanced scene's.
  */
  struct Override final {
    const data::schema::OverrideProperty* property = nullptr;
    data::SurfaceId part_id;
    base::Sha256Digest layout_revision {};
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

  //! A change of a node an instance brings in, by its node path.
  struct InstanceChange final {
    std::vector<data::NodeId> path;
    Override change;
  };

  //! A node that instances a scene, with its changes, before linking.
  struct Instance final {
    uint32_t node_index = 0;
    std::string scene;
    std::string object_path;
    std::vector<InstanceChange> changes;
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
  //! The node path below `build.starting_camera.node_index`, empty for one of
  //! the scene's own nodes.
  std::vector<data::NodeId> starting_camera_path;
  //! In activation order.
  std::vector<StartingInputContext> starting_input_contexts;
  std::optional<data::pak::world::SkyAtmosphereEnvironmentRecord> atmosphere;
  std::optional<data::pak::world::BackgroundEnvironmentRecord> background;
  std::optional<Fog> fog;
  std::optional<SkyLight> sky_light;
  std::optional<SkySphere> sky_sphere;
  std::optional<PostProcess> post_process;

  //! Reads a scene descriptor, or reports why it cannot be read.
  /*!
   The descriptor schema states the types of camera, light, local fog volume
   and node properties only; the scene schema owns their defaults, ranges and
   members. A property the descriptor omits takes the scene schema's default.
   One whose value is not finite, lies outside the property's generated range
   or is not a declared enum member fails with `kScenePropertyOutOfRangeCode`
   at the property's path; every such property of an object is reported. A
   descriptor is scene data: unlike a model import, it is never clamped.
  */
  OXGN_COOK_NDAPI static auto FromDescriptor(std::string_view bytes,
    const std::filesystem::path& source_path,
    std::vector<ImportDiagnostic>& diagnostics) -> std::optional<SceneSource>;
};

} // namespace oxygen::content::import::internal
