//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <type_traits>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/api_export.h>

#include <Oxygen/Content/Import/Naming.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Content/Import/TextureSourceAssembly.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Core/Types/Format.h>

namespace oxygen::content::import {

//! Policy for generating asset keys.
/*!
 Import pipelines typically want stable keys so repeated imports preserve
 external references and incremental cooks.

 Asset identities in Oxygen are GUIDs (see oxygen::data::AssetKey). This policy
 controls how those GUID values are produced.
*/
enum class AssetKeyPolicy : uint8_t {
  //! Generate a deterministic GUID derived from virtual paths (recommended).
  kDeterministicFromVirtualPath = 0,

  //! Generate a random GUID for each import.
  kRandom,
};

//! String representation of enum values in `AssetKeyPolicy`.
OXGN_CNTT_API auto to_string(AssetKeyPolicy value) -> std::string;

//! Policy for converting authored units into Oxygen world units.
/*!
 Importers must produce cooked content that is consistent across source formats.
 This includes consistent treatment of linear units.

 Oxygen treats authored linear distances as meters.

 ### Definitions

 - `source_unit_meters`: meters represented by one source-space unit.
   - For glTF 2.0: `source_unit_meters` is 1.0 by specification.
   - For FBX: `source_unit_meters` should come from the file settings (for
     example, ufbx exposes it as `scene->settings.unit_meters`). FBX commonly
     uses centimeters (0.01).

 ### Implementation Requirements

 - Apply unit scaling to *all* linear distances:
   - vertex positions and morph target position deltas
   - node translations
   - translation animation tracks
   - any transform matrix translation terms that are baked into geometry
 - Do not scale dimensionless attributes:
   - normals, tangents (unit vectors)
   - quaternions / rotations
 - If `kPreserveSource` is selected, the importer MUST leave numeric distances
   unchanged even when `source_unit_meters != 1`. This is a debugging / pipeline
   escape hatch; it will generally produce assets that look incorrectly scaled
   relative to engine physics and other meter-based content.

 @warning Changing unit normalization changes both geometry and animation.
  Assets imported with different unit policies are not guaranteed to compose
  correctly in a single scene.
*/
enum class UnitNormalizationPolicy : uint8_t {
  //! Convert source units to meters.
  /*! The importer scales linear distances by `source_unit_meters`. */
  kNormalizeToMeters = 0,

  //! Preserve source units (do not apply any unit scaling).
  kPreserveSource,

  //! Normalize to meters then apply a custom multiplier.
  /*! The importer scales linear distances by `source_unit_meters * factor`. */
  kApplyCustomFactor,
};

//! String representation of enum values in `UnitNormalizationPolicy`.
OXGN_CNTT_API auto to_string(UnitNormalizationPolicy value) -> std::string;

//! Flags describing which kinds of cooked content the importer should emit.
/*!
 These flags control which asset types are emitted, but do not permit emitting
 invalid asset structures.

 In particular, Oxygen geometry is structured as:

 - `oxygen::data::GeometryAsset` contains one `Mesh` per LOD.
 - Each `Mesh` contains one or more `SubMesh` instances.
 - Each `SubMesh` references exactly one `MaterialAsset`.

 ### Implementation Requirements

 - If `kGeometry` or `kScene` is requested while `kMaterials` is not, the
   importer MUST still bind each SubMesh to a valid MaterialAsset (typically
   the engine default material), but MUST NOT emit authored MaterialAssets.
 - If no material can be assigned to a SubMesh, the importer MUST fail with a
   diagnostic rather than emitting an invalid mesh.
*/
enum class ImportContentFlags : uint32_t { // NOLINT(performance-enum-size)
  kNone = 0,

  //! Emit texture resources.
  kTextures = OXYGEN_FLAG(0),

  //! Emit material assets.
  kMaterials = OXYGEN_FLAG(1),

  //! Emit geometry assets.
  kGeometry = OXYGEN_FLAG(2),

  //! Emit scene assets.
  kScene = OXYGEN_FLAG(3),

  kAll = kTextures | kMaterials | kGeometry | kScene,
};
OXYGEN_DEFINE_FLAGS_OPERATORS(ImportContentFlags)

//! String representation of enum values in `ImportContentFlags`.
OXGN_CNTT_API auto to_string(ImportContentFlags value) -> std::string;

//! Policy describing how the importer should handle computed vertex attributes.
/*!
 Some authored formats may omit derived vertex attributes such as normals and
 tangents. Importers can preserve, generate, or discard these attributes.

 ### Semantics

 - `kNone`: Do not emit the attribute at all.
 - `kPreserveIfPresent`: Emit the authored attribute if it exists; otherwise
   do not emit it.
 - `kGenerateMissing`: Emit the authored attribute if it exists; otherwise
   generate it.
 - `kAlwaysRecalculate`: Always recompute and emit the attribute, ignoring any
   authored values.

 ### Implementation Requirements

 - If tangents are generated or recalculated, the importer MUST ensure that the
   required prerequisites exist (typically positions, UVs, and normals). If the
   prerequisites are missing and generation is requested, the importer MUST
   record a diagnostic and fall back to `kNone` for tangents.
 - If `kNone` is selected for normals, tangents cannot be meaningfully
   generated; the importer MUST treat tangent generation as `kNone`.

 @warning Dropping normals/tangents can significantly affect lighting and
  material appearance.
*/
enum class GeometryAttributePolicy : uint8_t {
  kNone = 0,
  kPreserveIfPresent,
  kGenerateMissing,
  kAlwaysRecalculate,
};

//! String representation of enum values in `GeometryAttributePolicy`.
OXGN_CNTT_API auto to_string(GeometryAttributePolicy value) -> std::string;

//! Policy for pruning nodes that do not contribute geometry.
/*!
 "Empty" here means the node has no imported components after all conversion
 steps (including optional transform baking).

 In particular, nodes that carry (or will carry) non-geometry semantic
 components such as cameras or lights MUST NOT be considered empty, and MUST
 NOT be pruned by this policy.

 @note Cameras and lights are *scene-node components* authored into the scene
  descriptor. They are not emitted as standalone cooked assets.
*/
enum class NodePruningPolicy : uint8_t {
  //! Keep all authored nodes.
  kKeepAll = 0,

  //! Drop nodes that are empty.
  /*!
   The importer may still keep nodes that act as required parents (for
   example, to preserve the hierarchy of nodes that do have geometry).

   Nodes with non-geometry semantic components (for example, cameras or
   lights) are not empty and MUST be kept.
  */
  kDropEmptyNodes,
};

//! String representation of enum values in `NodePruningPolicy`.
OXGN_CNTT_API auto to_string(NodePruningPolicy value) -> std::string;

//! Coordinate conversion policy.
/*!
 This policy configures how source authoring data is converted into Oxygen's
 fixed coordinate-space conventions.

 The importer MUST always produce output that obeys the engine contract defined
 in Oxygen/Core/Constants.h (right-handed, Z-up, forward = -Y). This is not
 optional.

 These options only control how inputs authored in other conventions are mapped
 into Oxygen space.
*/
struct CoordinateConversionPolicy final {
  //! When true, bake node transforms into mesh vertices.
  /*!
   Baking applies only to linear vertex data (positions and compatible deltas)
   and is intended for static geometry.

   @warning Baking transforms can destroy instancing and can invalidate
    transform-driven semantics (for example, animation, skinning, or any node
    used as an attachment).

   If baking is enabled but a node's transform must remain authored to preserve
   fidelity, the importer MUST keep the transform on the node and record a
   diagnostic.
  */
  bool bake_transforms_into_meshes = true;

  //! Unit normalization policy for authored linear distances.
  UnitNormalizationPolicy unit_normalization
    = UnitNormalizationPolicy::kNormalizeToMeters;

  //! Custom unit scale multiplier.
  /*!
   Used only when `unit_normalization` is `kApplyCustomFactor`.

   The importer MUST apply this multiplier after normalizing to meters.
   This replaces the former `additional_uniform_scale` knob.

   @note This factor is applied to linear distances only (see
    `UnitNormalizationPolicy` for the exact requirements).
  */
  float custom_unit_scale = 1.0f;

  //! Importers must always output Oxygen world coordinates.
};

//! Import tuning options.
struct ImportOptions final {
  AssetKeyPolicy asset_key_policy
    = AssetKeyPolicy::kDeterministicFromVirtualPath;

  CoordinateConversionPolicy coordinate = {};

  //! Cooperative cancellation token for long-running imports.
  /*! Importers should periodically check this token and abort promptly. */
  std::stop_token stop_token {};

  //! Optional naming strategy applied to imported nodes and assets.
  /*!
   If set, the importer should call this hook when assigning names to scene
   nodes and emitted assets.

   If the strategy returns `std::nullopt`, the importer MUST keep the authored
   name unchanged.

   @note Names are not required to be unique in Oxygen.
  */
  std::shared_ptr<const NamingStrategy> naming_strategy;

  //! Policy for pruning empty nodes from imported scenes.
  /*!
   This is the only user-configurable aspect of scene-graph construction.

   The importer MUST otherwise preserve scene fidelity:

   - Preserve the authored parent/child node hierarchy.
   - Preserve authored LOD structure with 100% fidelity.
     `oxygen::data::GeometryAsset` represents LODs as one Mesh per LOD index.
     If the source provides no LOD concept, import a single LOD (LOD0).
   - Produce valid Oxygen geometry assets:
     - Each produced `oxygen::data::Mesh` MUST contain at least one SubMesh.
     - Each produced `oxygen::data::SubMesh` MUST reference a valid
       `MaterialAsset`.

   @note If `coordinate.bake_transforms_into_meshes` is enabled, pruning is
    evaluated after any transform baking has been applied.
  */
  NodePruningPolicy node_pruning = NodePruningPolicy::kKeepAll;

  //! Select which cooked content should be emitted.
  ImportContentFlags import_content = ImportContentFlags::kAll;

  //! Enable or disable content hashing across import pipelines.
  /*!
   When false, pipelines MUST NOT compute any `content_hash` values.
   This applies to textures, buffers, geometry, materials, and scenes.
  */
  bool with_content_hashing = true;

  //! How to handle vertex normals.
  /*! Default is `kGenerateMissing`. */
  GeometryAttributePolicy normal_policy
    = GeometryAttributePolicy::kGenerateMissing;

  //! How to handle vertex tangents.
  /*! Default is `kGenerateMissing`. */
  GeometryAttributePolicy tangent_policy
    = GeometryAttributePolicy::kGenerateMissing;

  //! Whether to ignore non-mesh primitives (points/lines).
  /*!
   Oxygen Mesh assets are triangle-list based.

  Importers MUST only accept explicit triangle lists:
  - FBX polygons (n-gons) are rejected.
  - glTF triangle strips and triangle fans are rejected.

   Separately, source formats may contain primitives that are not mesh geometry
  in Oxygen today (points, lines, line strips, etc.). These primitives MUST
  NOT be converted into Mesh geometry, and MUST NOT be merged with triangle
  meshes.

   When this option is true (default), the importer skips such primitives.
   When false, the importer MUST fail with a diagnostic if any are encountered.

   @note Future versions may normalize these into dedicated primitive sets with
    explicit semantics.
  */
  bool ignore_non_mesh_primitives = true;

  //! Texture import tuning for emission-time cooking.
  /*!
   When enabled, FBX import uses the texture cooker to generate mip chains and
   select output formats (including optional BC7 compression). This can
   significantly reduce runtime GPU memory use compared to pass-through
   uncompressed textures.
  */
  struct TextureTuning final {
    //! Enable texture cooking overrides.
    bool enabled = false;

    //! Texture intent for standalone imports.
    TextureIntent intent = TextureIntent::kAlbedo;

    //! Source color space for decode and filtering.
    ColorSpace source_color_space = ColorSpace::kSRGB;

    //! Flip image vertically during decode.
    bool flip_y_on_decode = false;

    //! Force RGBA output during decode.
    bool force_rgba_on_decode = true;

    //! Mip chain generation policy.
    MipPolicy mip_policy = MipPolicy::kNone;

    //! Maximum mip levels when @p mip_policy is `MipPolicy::kMaxCount`.
    uint8_t max_mip_levels = 1;

    //! Mip filter kernel used when generating mips.
    MipFilter mip_filter = MipFilter::kKaiser;

    //! Output format for color textures (e.g., base color, emissive).
    Format color_output_format = Format::kBC7UNormSRGB;

    //! Output format for data textures (e.g., normal, ORM).
    Format data_output_format = Format::kBC7UNorm;

    //! BC7 compression quality tier (applies only for BC7 outputs).
    Bc7Quality bc7_quality = Bc7Quality::kDefault;

    //! Packing policy ID ("d3d12" or "tight").
    std::string packing_policy_id = "d3d12";

    //! Use placeholder payload when texture cooking fails.
    bool placeholder_on_failure = false;

    //! Import as a cubemap using cube-specific workflows.
    /*!
     When false, the import job treats the source as a standard 2D texture.
    */
    bool import_cubemap = false;

    //! Convert an equirectangular panorama into a cubemap.
    /*!
     When false, no equirectangular conversion is attempted.
    */
    bool equirect_to_cubemap = false;

    //! Cubemap face size in pixels for equirect conversion.
    /*!
      When set to 0, equirect conversion is invalid and must be provided by the
      caller. The face size must be a multiple of 256.
    */
    uint32_t cubemap_face_size = 0;

    //! Explicit cubemap layout for layout images.
    /*!
     When set to `kUnknown`, layout extraction is skipped.
     When set to `kAuto`, the layout is detected from image dimensions.
    */
    CubeMapImageLayout cubemap_layout = CubeMapImageLayout::kUnknown;
  };

  TextureTuning texture_tuning = {};
};

} // namespace oxygen::content::import
