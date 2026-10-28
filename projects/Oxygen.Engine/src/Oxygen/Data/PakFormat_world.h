//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>

// packed structs intentionally embed unaligned NamedType ResourceIndexT fields
OXYGEN_DIAGNOSTIC_PUSH
OXYGEN_DIAGNOSTIC_DISABLE_MSVC(4315)
// NOLINTBEGIN(*-avoid-c-arrays)

//! Oxygen PAK format world domain schema.
/*!
 Owns scene graph descriptors and world component/environment/light records.
*/
namespace oxygen::data::pak::world {

//! Scene node flags for `NodeRecord::node_flags`.
[[maybe_unused]] constexpr uint32_t kSceneNodeFlag_Visible = (1U << 0U);
[[maybe_unused]] constexpr uint32_t kSceneNodeFlag_Static = (1U << 1U);
[[maybe_unused]] constexpr uint32_t kSceneNodeFlag_CastsShadows = (1U << 2U);
[[maybe_unused]] constexpr uint32_t kSceneNodeFlag_ReceivesShadows = (1U << 3U);
[[maybe_unused]] constexpr uint32_t kSceneNodeFlag_RayCastingSelectable
  = (1U << 4U);
[[maybe_unused]] constexpr uint32_t kSceneNodeFlag_IgnoreParentTransform
  = (1U << 5U);

//! Flags with authored Local/Inherit source modes.
inline constexpr uint32_t kSceneNodeFlags_Inheritable = kSceneNodeFlag_Visible
  | kSceneNodeFlag_CastsShadows | kSceneNodeFlag_ReceivesShadows;
inline constexpr uint32_t kSceneNodeFlags_Known = kSceneNodeFlags_Inheritable
  | kSceneNodeFlag_Static | kSceneNodeFlag_RayCastingSelectable
  | kSceneNodeFlag_IgnoreParentTransform;

//! Scene asset descriptor version for current PAK schema.
//!
//! @note Scene descriptors include a trailing SceneEnvironment block (empty
//! allowed).
inline constexpr uint8_t kSceneAssetVersion = version::kSceneAssetVersion;
//! Index type for scene node tables.
using SceneNodeIndexT = uint32_t;

//! Index type for scene component tables.
using SceneComponentIndexT = uint32_t;

//! `SceneAssetDesc::starting_camera.node_index` of a scene without one.
inline constexpr SceneNodeIndexT kNoStartingCamera = 0xFFFFFFFFU;

//! Index of a `NodePathRecord` in the scene's `ComponentType::kNodePath` table.
using NodePathIndexT = uint32_t;

//! `NodeRef::node_path` of a reference to one of the scene's own nodes.
inline constexpr NodePathIndexT kNoNodePath = 0xFFFFFFFFU;

#pragma pack(push, 1)
//! The one way a cooked scene names a node: one of its own nodes, or a node an
//! instance brings in.
/*!
 With `kNoNodePath`, `node_index` is the node. Otherwise `node_index` is an
 instance root and `node_path` the index of the `NodePathRecord` whose elements
 name one node per nesting level below it, shallowest first: the first is a
 node of the root's source. References order by node index, then path.
*/
struct NodeRef {
  SceneNodeIndexT node_index = 0;
  NodePathIndexT node_path = kNoNodePath;

  [[nodiscard]] friend constexpr auto operator<=>(
    const NodeRef&, const NodeRef&) noexcept = default;
};
#pragma pack(pop)
static_assert(sizeof(NodeRef) == 8);

#pragma pack(push, 1)
//! A node path (`ComponentType::kNodePath`): `count` elements of the
//! `ComponentType::kNodePathElement` table from `first`. Identical paths share
//! one record.
struct NodePathRecord {
  uint32_t first = 0;
  uint32_t count = 0;
};
#pragma pack(pop)
static_assert(sizeof(NodePathRecord) == 8);

#pragma pack(push, 1)
//! One nesting level of a node path (`ComponentType::kNodePathElement`): the
//! ID of a node in the scene that defines it.
struct NodePathElement {
  NodeId node_id;
};
#pragma pack(pop)
static_assert(sizeof(NodePathElement) == 16);

//! Scene asset flags.
enum class SceneAssetFlags : uint32_t {
  kNone = 0,
  //! The scene is the output of a model import (glTF, FBX): a read-only
  //! model scene that authored scenes instance, never run on its own.
  kModelScene = OXYGEN_FLAG(0),
};
OXYGEN_DEFINE_FLAGS_OPERATORS(SceneAssetFlags)

//! Scene data table descriptor.
/*!
 Describes a packed array of records inside a scene descriptor.

 Offsets are bytes relative to the start of the scene descriptor payload.
*/
#pragma pack(push, 1)
struct SceneDataTable {
  core::OffsetT offset = 0;
  uint32_t count = 0;
  uint32_t entry_size = 0;
};
#pragma pack(pop)
static_assert(sizeof(SceneDataTable) == 16);

//! Scene node table descriptor (alias).
using SceneNodeTable = SceneDataTable;

//! Scene component table descriptor (alias).
using SceneComponentTable = SceneDataTable;

//! Scene asset descriptor
/*!
 Describes a scene (level) asset. As with all asset descriptors in this file,
 `AssetHeader` is the first field.

 The descriptor payload is a packed byte blob (no implicit padding) and is
 followed by:

 - `NodeRecord nodes[nodes.count];` at `nodes.offset`
 - optional component tables (e.g. `RenderableRecord[]`) described by the
   component table directory at `component_table_directory_offset`

 The directory also holds scene-level tables: `SceneInstanceRecord[]`
 (model scenes instanced under authored nodes) and
 `StartingInputContextRecord[]` (input contexts the scene starts with).
 `starting_camera` names the camera node the scene starts viewing through, an
 owned node or one an instance brings in, or holds `kNoStartingCamera` in its
 `node_index`. The node path tables (`NodePathRecord[]`, `NodePathElement[]`)
 hold the paths every `NodeRef` of the scene names.

 `nodes.entry_size` MUST match the corresponding struct size for the scene
 format version. Component tables declare their own `entry_size` via
 `SceneComponentTableDesc::table.entry_size`.

 The descriptor string table (`header.strings`) follows the nodes.
 `NodeRecord::scene_name_offset` and the slot parameter strings are byte offsets
 into it.

 @note Scene graph indices have no sentinel values by contract. Indices are
   always valid for their type; out-of-range indices are treated as errors by
   loaders/tooling.
*/
#pragma pack(push, 1)
struct SceneAssetDesc {
  core::AssetHeader header;

  SceneNodeTable nodes = {};

  // Directory of component tables.
  // Points to an array of `SceneComponentTableDesc` entries.
  core::OffsetT component_table_directory_offset = 0;
  uint32_t component_table_count = 0;
  //! Descriptor-local script slots; each slot locates its own parameter array.
  SceneComponentTable script_slots {};
  SceneAssetFlags flags = SceneAssetFlags::kNone;
  //! Camera node the scene starts viewing through.
  NodeRef starting_camera { .node_index = kNoStartingCamera };
};
#pragma pack(pop)
static_assert(sizeof(SceneAssetDesc) == 107);

//! Scene component table directory entry.
/*!
 Describes an optional component table attached to scene nodes.

 All offsets are bytes relative to the start of the descriptor payload.

 @note This is a forward-compatible extension point. Loaders may ignore unknown
   component types. Known component tables are typically sorted by `node_index`
   for efficient loading.
*/
#pragma pack(push, 1)
struct SceneComponentTableDesc {
  uint32_t component_type = 0; // Format-defined component kind
  SceneComponentTable table = {};
};
#pragma pack(pop)
static_assert(sizeof(SceneComponentTableDesc) == 20);

#pragma pack(push, 1)

//! Node record used by the cooked scene descriptor.
/*!
  Describes a single node in the scene hierarchy. Nodes are stored in a flat
  array in the `SceneAssetDesc`. Changes and node paths name a node by its
  `node_id` alone.

  ### Hierarchy
  - The node at index 0 is always the root node.
  - `parent_index` refers to the index of the parent node in the same table.
  - If `parent_index` equals the node's own index, the node has no parent (is a
    root).

  ### Transform
  - Transforms are local to the parent.
  - Rotation is stored as a quaternion (x, y, z, w).

  @see SceneAssetDesc
*/
struct NodeRecord {
  //! Unique within the scene: an authored node's editor ID, a model node's ID
  //! derived from its anchor.
  NodeId node_id;
  core::StringTableOffsetT scene_name_offset
    = 0; // Offset into the descriptor string table

  SceneNodeIndexT parent_index = 0; // Index of parent node (or self if root)

  //! Local values from `kSceneNodeFlag_*`. Inherited value bits are zero.
  uint32_t node_flags = 0;

  //! Source modes: set bits inherit; clear bits use node_flags locally.
  //! Only kSceneNodeFlags_Inheritable bits are valid. Resolved values are
  //! runtime state and are never stored in this record.
  uint32_t inherited_flags = 0;

  // Local Transform (TRS)
  float translation[3] = { 0.0F, 0.0F, 0.0F };
  float rotation[4] = { 0.0F, 0.0F, 0.0F, 1.0F }; // Quaternion (XYZW)
  float scale[3] = { 1.0F, 1.0F, 1.0F };
};
#pragma pack(pop)
static_assert(sizeof(NodeRecord) == 72);

//! Rejects unknown flags, unsupported inheritance and ambiguous inherited
//! values.
[[nodiscard]] constexpr auto HasCanonicalNodeFlags(
  const NodeRecord& node) noexcept -> bool
{
  return (node.node_flags & ~kSceneNodeFlags_Known) == 0U
    && (node.inherited_flags & ~kSceneNodeFlags_Inheritable) == 0U
    && (node.node_flags & node.inherited_flags) == 0U;
}

#pragma pack(push, 1)

//! Renderable component record.
/*!
  Attaches a geometry asset to a scene node.

  ### Relationships
  - Links to a `NodeRecord` via `node_index`.
  - References a `GeometryAsset` via `geometry_key`.
  - The scene's changes of the geometry's asset defaults are `OverrideRecord`
    entries of the same node.

  @note Component tables are typically sorted by `node_index` for efficient
  loading.
*/
struct RenderableRecord {
  SceneNodeIndexT node_index = 0; // Index of the owner node
  AssetKey geometry_key; // Geometry asset to render
};
#pragma pack(pop)
static_assert(sizeof(RenderableRecord) == 20);

#pragma pack(push, 1)
//! A model scene instanced under an authored node
//! (`ComponentType::kSceneInstance`).
/*!
 When the scene is instantiated, the root nodes of the scene named by
 `scene_key` become children of `node_index`.
*/
struct SceneInstanceRecord {
  SceneNodeIndexT node_index = 0;
  AssetKey scene_key;
};
#pragma pack(pop)
static_assert(sizeof(SceneInstanceRecord) == 20);

#pragma pack(push, 1)
//! An input mapping context the scene starts with
//! (`ComponentType::kStartingInputContext`), in activation order.
struct StartingInputContextRecord {
  AssetKey context_key;
  int32_t priority = 0;
};
#pragma pack(pop)
static_assert(sizeof(StartingInputContextRecord) == 20);

//! Index of an entry of an override value record table, such as
//! `ComponentType::kOverrideLodPolicy`.
using OverrideRecordIndex = NamedType<uint32_t, struct OverrideRecordIndexTag,
  Comparable, Hashable, Printable, DefaultInitialized>;
static_assert(sizeof(OverrideRecordIndex) == sizeof(uint32_t));

#pragma pack(push, 1)
//! The value of one override; the property its `override_id` names decides
//! which member holds it (`OverrideProperty::member` in the scene schema's
//! `kOverrideProperties`).
/*!
 - `as_bool`: 0 or 1.
 - `as_int32`: an int, or the wire value of an enum member.
 - `as_float`: a float; `as_vec3`: the three floats of a vec3.
 - `as_quat`: a rotation quaternion (x, y, z, w), the change of a rotation
   property (`schema::Encoding::kEulerXyzDegreesToQuat`).
 - `as_asset`: the key of an asset of the property's asset type.
 - `as_string_offset`: an offset into the descriptor string table.
 - `as_record_index`: the index of an entry of the record table of the value
   type, such as `ComponentType::kOverrideLodPolicy` for a LOD policy. An index
   survives relocating the descriptor's tables.

 Bytes the member does not use are zero.
*/
union OverrideValue {
  uint8_t as_bool;
  int32_t as_int32;
  float as_float;
  float as_vec3[3];
  float as_quat[4];
  AssetKey as_asset {}; // Initializes every byte to zero.
  core::StringTableOffsetT as_string_offset;
  OverrideRecordIndex as_record_index;
};
#pragma pack(pop)
static_assert(sizeof(OverrideValue) == 16);

#pragma pack(push, 1)
//! A scene's change of one property of a node (`ComponentType::kOverride`).
/*!
 `override_id` names the property. A target with `kNoNodePath` is one of the
 scene's own renderable nodes, and the property is asset-sourced: the change
 replaces the geometry's value. A target with a node path is a node an
 instance brings in, and the property is any changeable one. A part property
 names its part in `part_id` (a surface's SurfaceId; surfaces are the only
 part kind) and carries the geometry layout revision the override was resolved
 against; a property of the component itself has a nil `part_id` and a zero
 `layout_revision`. A target holds at most one override per property and part.
 The table is sorted by target, override id and part.
*/
// `override_id` is no tag: its property's schema value type selects the member.
// NOLINTNEXTLINE(bugprone-tagged-union-member-count)
struct OverrideRecord {
  NodeRef target;
  schema::OverrideId override_id = schema::OverrideId::kInvalid;
  SurfaceId part_id;
  base::Sha256Digest layout_revision {};
  OverrideValue value {};
};
#pragma pack(pop)
static_assert(sizeof(OverrideRecord) == 74);

#pragma pack(push, 1)

//! Node-attached local fog volume component record.
/*!
  Authored fields are value-initialized: the scene schema owns the defaults,
  and a writer sets every field (see `MakeDefaultRecord`).
*/
struct LocalFogVolumeRecord {
  SceneNodeIndexT node_index = 0;
  uint32_t enabled {};

  float radial_fog_extinction {};
  float height_fog_extinction {};
  float height_fog_falloff {};
  float height_fog_offset {};
  float fog_phase_g {};
  float fog_albedo[3] {};
  float fog_emissive[3] {};
  int32_t sort_priority {};
};
#pragma pack(pop)
static_assert(sizeof(LocalFogVolumeRecord) == 56);

//! Whether `record` holds valid values of the local fog volume's schema
//! properties; cooking and loading share this contract.
[[nodiscard]] inline auto HasValidLocalFogVolumeValues(
  const LocalFogVolumeRecord& record) -> bool
{
  namespace fog = schema::local_fog_volume;
  return record.enabled <= 1U
    && fog::kRadialFogExtinctionRange.Contains(record.radial_fog_extinction)
    && fog::kHeightFogExtinctionRange.Contains(record.height_fog_extinction)
    && fog::kHeightFogFalloffRange.Contains(record.height_fog_falloff)
    && fog::kHeightFogOffsetRange.Contains(record.height_fog_offset)
    && fog::kFogPhaseGRange.Contains(record.fog_phase_g)
    && fog::kFogAlbedoRRange.Contains(record.fog_albedo[0])
    && fog::kFogAlbedoGRange.Contains(record.fog_albedo[1])
    && fog::kFogAlbedoBRange.Contains(record.fog_albedo[2])
    && fog::kFogEmissiveRRange.Contains(record.fog_emissive[0])
    && fog::kFogEmissiveGRange.Contains(record.fog_emissive[1])
    && fog::kFogEmissiveBRange.Contains(record.fog_emissive[2])
    && fog::kSortPriorityRange.Contains(record.sort_priority);
}

#pragma pack(push, 1)

//! Perspective camera component record.
/*!
  Attaches a perspective camera to a scene node.

  ### Coordinate System
  - The camera looks down the -Z axis in its local space.
  - FOV is vertical, in radians.

  Authored fields are value-initialized: the scene schema owns the defaults,
  and a writer sets every field (see `MakeDefaultRecord`).
*/
struct PerspectiveCameraRecord {
  SceneNodeIndexT node_index = 0; // Index of the owner node
  float fov_y {}; // Vertical FOV in radians
  float aspect_ratio {}; // Retained Fixed width / height
  float near_plane {}; // Distance to near clipping plane
  float far_plane {}; // Distance to far clipping plane
  float aperture_f {};
  float shutter_rate {};
  float iso {};
  CameraAspectMode aspect_mode {};
};
#pragma pack(pop)
static_assert(sizeof(PerspectiveCameraRecord) == 33);
static_assert(offsetof(PerspectiveCameraRecord, aperture_f) == 20);
static_assert(offsetof(PerspectiveCameraRecord, shutter_rate) == 24);
static_assert(offsetof(PerspectiveCameraRecord, iso) == 28);
static_assert(offsetof(PerspectiveCameraRecord, aspect_mode) == 32);

//! Whether `record` holds valid values of the perspective camera's schema
//! properties and a far plane beyond its near plane; cooking and loading share
//! this contract.
[[nodiscard]] inline auto HasValidPerspectiveCameraValues(
  const PerspectiveCameraRecord& record) -> bool
{
  namespace camera = schema::perspective_camera;
  return schema::IsMember(
           camera::kAspectModeValues, nostd::to_underlying(record.aspect_mode))
    && camera::kFieldOfViewYRadiansRange.Contains(record.fov_y)
    && camera::kAspectRatioRange.Contains(record.aspect_ratio)
    && camera::kNearPlaneRange.Contains(record.near_plane)
    && camera::kFarPlaneRange.Contains(record.far_plane)
    && record.far_plane > record.near_plane
    && camera::kApertureFRange.Contains(record.aperture_f)
    && camera::kShutterRateRange.Contains(record.shutter_rate)
    && camera::kIsoRange.Contains(record.iso);
}

#pragma pack(push, 1)

//! Orthographic camera component record.
/*!
  Attaches an orthographic camera to a scene node.

  ### Volume
  - Defined by a box (left, right, bottom, top, near, far) in local space.
  - Fixed keeps these extents; Auto keeps the vertical extents and horizontal
    centre and derives the width from each render target's aspect ratio.

  Authored fields are value-initialized: the scene schema owns the defaults,
  and a writer sets every field (see `MakeDefaultRecord`).
*/
struct OrthographicCameraRecord {
  SceneNodeIndexT node_index = 0; // Index of the owner node
  float left {};
  float right {};
  float bottom {};
  float top {};
  float near_plane {};
  float far_plane {};
  float aperture_f {};
  float shutter_rate {};
  float iso {};
  CameraAspectMode aspect_mode {};
};
#pragma pack(pop)
static_assert(sizeof(OrthographicCameraRecord) == 41);
static_assert(offsetof(OrthographicCameraRecord, aperture_f) == 28);
static_assert(offsetof(OrthographicCameraRecord, shutter_rate) == 32);
static_assert(offsetof(OrthographicCameraRecord, iso) == 36);
static_assert(offsetof(OrthographicCameraRecord, aspect_mode) == 40);

//! Whether `record` holds valid values of the orthographic camera's schema
//! properties, a non-empty volume and a far plane beyond its near plane;
//! cooking and loading share this contract.
/*!
 The volume's half height is the schema's orthographic size, and its width over
 its height the schema's aspect ratio.
*/
[[nodiscard]] inline auto HasValidOrthographicCameraValues(
  const OrthographicCameraRecord& record) -> bool
{
  namespace camera = schema::orthographic_camera;
  if (!schema::IsMember(
        camera::kAspectModeValues, nostd::to_underlying(record.aspect_mode))
    || !(record.right > record.left) || !(record.top > record.bottom)) {
    return false;
  }
  // Halved before subtracting: the half extents of finite bounds are finite.
  const auto half_height = (0.5F * record.top) - (0.5F * record.bottom);
  const auto half_width = (0.5F * record.right) - (0.5F * record.left);
  return camera::kOrthographicSizeRange.Contains(half_height)
    && camera::kAspectRatioRange.Contains(half_width / half_height)
    && camera::kNearPlaneRange.Contains(record.near_plane)
    && camera::kFarPlaneRange.Contains(record.far_plane)
    && record.far_plane > record.near_plane
    && camera::kApertureFRange.Contains(record.aperture_f)
    && camera::kShutterRateRange.Contains(record.shutter_rate)
    && camera::kIsoRange.Contains(record.iso);
}

//=== Scene: Lights and Environment -----------------------------------------//

//! Environment system type tags used by scene persistence.
enum class EnvironmentComponentType : uint32_t {
  kSkyAtmosphere = 0,
  kVolumetricClouds = 1,
  kFog = 2,
  kSkyLight = 3,
  kSkySphere = 4,
  kPostProcessVolume = 5,
  kBackground = 6,
};

//! Header for the trailing SceneEnvironment block.
/*!
 The environment block is stored immediately after the Scene payload.

 - `byte_size` includes this header and all following system records.
 - The block is always present for scene descriptors in this layer.
   A scene with "no environment" uses `systems_count == 0` and
   `byte_size == sizeof(SceneEnvironmentBlockHeader)`.
*/
#pragma pack(push, 1)
struct SceneEnvironmentBlockHeader {
  uint32_t byte_size = 0;
  uint32_t systems_count = 0;
};
#pragma pack(pop)
static_assert(sizeof(SceneEnvironmentBlockHeader) == 8);

//! Header for a single environment-system record in the trailing block.
/*!
 - `system_type` is an `EnvironmentComponentType` enum value.
 - `record_size` is the byte size of the entire record, including this header.
   Unknown `system_type` values can be skipped using `record_size`.
*/
#pragma pack(push, 1)
struct SceneEnvironmentSystemRecordHeader {
  uint32_t system_type = 0;
  uint32_t record_size = 0;
};
#pragma pack(pop)
static_assert(sizeof(SceneEnvironmentSystemRecordHeader) == 8);

//! Packed SkyAtmosphere environment record.
#pragma pack(push, 1)
struct SkyAtmosphereEnvironmentRecord {
  SceneEnvironmentSystemRecordHeader header = {
    .system_type
    = nostd::to_underlying(EnvironmentComponentType::kSkyAtmosphere),
    .record_size = sizeof(SkyAtmosphereEnvironmentRecord),
  };

  uint32_t enabled = 1; //!< Whether this system is enabled (boolean).

  float planet_radius_m = 6360000.0F;
  float atmosphere_height_m = 80000.0F;

  float ground_albedo_rgb[3] = { 0.4F, 0.4F, 0.4F };

  float rayleigh_scattering_rgb[3] = { 5.8e-6F, 13.5e-6F, 33.1e-6F };
  float rayleigh_scale_height_m = 8000.0F;

  float mie_scattering_rgb[3] = { 21.0e-6F, 21.0e-6F, 21.0e-6F };
  float mie_absorption_rgb[3] = { 0.0F, 0.0F, 0.0F };
  float mie_scale_height_m = 1200.0F;
  float mie_g = 0.8F;

  float absorption_rgb[3] = { 0.0F, 0.0F, 0.0F };
  float ozone_density_profile[3] = { 25000.0F, 0.0F, 0.0F };

  float multi_scattering_factor = 1.0F;
  float sky_luminance_factor_rgb[3] = { 1.0F, 1.0F, 1.0F };
  float sky_and_aerial_perspective_luminance_factor_rgb[3]
    = { 1.0F, 1.0F, 1.0F };

  float aerial_perspective_distance_scale = 1.0F;
  float aerial_scattering_strength = 1.0F;
  float aerial_perspective_start_depth_m = 0.0F;
  float height_fog_contribution = 1.0F;
  float trace_sample_count_scale = 1.0F;
  float transmittance_min_light_elevation_deg = -90.0F;

  uint32_t sun_disk_enabled = 1;
  uint32_t holdout = 0;
  uint32_t render_in_main_pass = 1;
};
#pragma pack(pop)
static_assert(sizeof(SkyAtmosphereEnvironmentRecord) == 168);

//! Packed VolumetricClouds environment record.
#pragma pack(push, 1)
struct VolumetricCloudsEnvironmentRecord {
  SceneEnvironmentSystemRecordHeader header = {
    .system_type
    = nostd::to_underlying(EnvironmentComponentType::kVolumetricClouds),
    .record_size = sizeof(VolumetricCloudsEnvironmentRecord),
  };

  uint32_t enabled = 1; //!< Whether this system is enabled (boolean).

  float base_altitude_m = 1500.0F;
  float layer_thickness_m = 4000.0F;

  float coverage = 0.5F;
  float extinction_sigma_t_per_m = 1.0e-3F;

  float single_scattering_albedo_rgb[3] = { 0.9F, 0.9F, 0.9F };
  float phase_g = 0.6F;

  float wind_dir_ws[3] = { 1.0F, 0.0F, 0.0F };
  float wind_speed_mps = 10.0F;

  float shadow_strength = 0.8F;
};
#pragma pack(pop)
static_assert(sizeof(VolumetricCloudsEnvironmentRecord) == 64);

//! Packed Fog environment record.
#pragma pack(push, 1)
struct FogEnvironmentRecord {
  SceneEnvironmentSystemRecordHeader header = {
    .system_type = nostd::to_underlying(EnvironmentComponentType::kFog),
    .record_size = sizeof(FogEnvironmentRecord),
  };

  uint32_t enabled = 1; //!< Whether this system is enabled (boolean).

  uint32_t model = 0; //!< FogModel (0=ExponentialHeight, 1=Volumetric).
  float extinction_sigma_t_per_m = 0.002F;
  float height_falloff_per_m = 0.02F;
  float height_offset_m = 0.0F;
  float start_distance_m = 0.0F;
  float max_opacity = 1.0F;
  float single_scattering_albedo_rgb[3] = { 1.0F, 1.0F, 1.0F };
  float anisotropy_g = 0.0F;

  uint32_t enable_height_fog = 1;
  uint32_t enable_volumetric_fog = 0;

  float second_fog_density = 0.0F;
  float second_fog_height_falloff = 0.0F;
  float second_fog_height_offset = 0.0F;

  float fog_inscattering_luminance[3] = { 0.0F, 0.0F, 0.0F };
  float sky_atmosphere_ambient_contribution_color_scale[3]
    = { 1.0F, 1.0F, 1.0F };
  //! Cube texture binding in the scene's resource reference table.
  ResourceReferenceIndex inscattering_color_cubemap = kNoResourceReference;
  std::array<uint32_t, 3> inscattering_color_cubemap_reserved {};
  float inscattering_color_cubemap_angle = 0.0F;
  float inscattering_texture_tint[3] = { 1.0F, 1.0F, 1.0F };
  float fully_directional_inscattering_color_distance = 0.0F;
  float non_directional_inscattering_color_distance = 0.0F;
  float directional_inscattering_luminance[3] = { 0.0F, 0.0F, 0.0F };
  float directional_inscattering_exponent = 4.0F;
  float directional_inscattering_start_distance = 10000.0F;
  float end_distance_m = 0.0F;
  float fog_cutoff_distance_m = 0.0F;

  float volumetric_fog_scattering_distribution = 0.0F;
  float volumetric_fog_albedo[3] = { 1.0F, 1.0F, 1.0F };
  float volumetric_fog_emissive[3] = { 0.0F, 0.0F, 0.0F };
  float volumetric_fog_extinction_scale = 1.0F;
  float volumetric_fog_distance = 0.0F;
  float volumetric_fog_start_distance = 0.0F;
  float volumetric_fog_near_fade_in_distance = 0.0F;
  float volumetric_fog_static_lighting_scattering_intensity = 1.0F;
  uint32_t override_light_colors_with_fog_inscattering_colors = 0;

  uint32_t holdout = 0;
  uint32_t render_in_main_pass = 1;
  uint32_t visible_in_reflection_captures = 1;
  uint32_t visible_in_real_time_sky_captures = 1;
};
#pragma pack(pop)
static_assert(sizeof(FogEnvironmentRecord) == 232);

//! Packed SkyLight (IBL) environment record.

#pragma pack(push, 1)
struct SkyLightEnvironmentRecord {
  SceneEnvironmentSystemRecordHeader header = {
    .system_type = nostd::to_underlying(EnvironmentComponentType::kSkyLight),
    .record_size = sizeof(SkyLightEnvironmentRecord),
  };

  uint32_t enabled = 1; //!< Whether this system is enabled (boolean).

  uint32_t source = 0; // SkyLightSource

  //! Cube texture binding in the scene's resource reference table.
  ResourceReferenceIndex cubemap = kNoResourceReference;
  //! Illuminance, in lux, that the source delivers on an upward-facing
  //! surface. The renderer scales the imported radiance to deliver it; zero
  //! uses the radiance as imported. Former content wrote zero here.
  float illuminance_lux = 0.0F;
  std::array<uint32_t, 2> cubemap_reserved {};

  float intensity = 1.0F;
  float tint_rgb[3] = { 1.0F, 1.0F, 1.0F };

  float diffuse_intensity = 1.0F;
  float specular_intensity = 1.0F;
  float lower_hemisphere_color[3] = { 0.0F, 0.0F, 0.0F };
  float volumetric_scattering_intensity = 1.0F;
  uint32_t affect_reflections = 1;

  float source_cubemap_angle_radians = 0.0F;
  uint32_t lower_hemisphere_is_solid_color = 1;
  float lower_hemisphere_blend_alpha = 1.0F;
};
#pragma pack(pop)
static_assert(sizeof(SkyLightEnvironmentRecord) == 88);

//! Packed SkySphere environment record.
#pragma pack(push, 1)
struct SkySphereEnvironmentRecord {
  SceneEnvironmentSystemRecordHeader header = {
    .system_type = nostd::to_underlying(EnvironmentComponentType::kSkySphere),
    .record_size = sizeof(SkySphereEnvironmentRecord),
  };

  uint32_t enabled = 1; //!< Whether this system is enabled (boolean).

  uint32_t source = 0; // SkySphereSource

  //! Cube texture binding in the scene's resource reference table.
  ResourceReferenceIndex cubemap = kNoResourceReference;
  //! Illuminance, in lux, that the source delivers on an upward-facing
  //! surface. The renderer scales the imported radiance to deliver it; zero
  //! uses the radiance as imported. Former content wrote zero here.
  float illuminance_lux = 0.0F;
  std::array<uint32_t, 2> cubemap_reserved {};

  float solid_color_rgb[3] = { 0.0F, 0.0F, 0.0F };
  float intensity = 1.0F;
  float rotation_radians = 0.0F;
  float tint_rgb[3] = { 1.0F, 1.0F, 1.0F };
};
#pragma pack(pop)
static_assert(sizeof(SkySphereEnvironmentRecord) == 64);

//! Packed PostProcessVolume environment record.
inline constexpr uint32_t kExposureExtensionVersion
  = version::kExposureExtensionVersion;

#pragma pack(push, 1)
struct ExposureCompensationKeyRecord {
  float metered_ev { 0.0F };
  float compensation_ev { 0.0F };
};
#pragma pack(pop)
static_assert(sizeof(ExposureCompensationKeyRecord) == 8U);

#pragma pack(push, 1)
struct PostProcessVolumeEnvironmentRecord {
  SceneEnvironmentSystemRecordHeader header = {
    .system_type
    = nostd::to_underlying(EnvironmentComponentType::kPostProcessVolume),
    .record_size = sizeof(PostProcessVolumeEnvironmentRecord),
  };

  uint32_t enabled = 1; //!< Whether this system is enabled (boolean).

  engine::ToneMapper tone_mapper = engine::ToneMapper::kAcesFitted;
  engine::ExposureMode exposure_mode = engine::ExposureMode::kAuto;
  float exposure_compensation_ev = 0.0F;

  float auto_exposure_min_ev = -6.0F;
  float auto_exposure_max_ev = 16.0F;
  float auto_exposure_speed_up = 3.0F;
  float auto_exposure_speed_down = 1.0F;

  float bloom_intensity = 0.0F;
  float bloom_threshold = 1.0F;

  float saturation = 1.0F;
  float contrast = 1.0F;
  float vignette_intensity = 0.0F;

  uint32_t exposure_enabled = 1;
  float exposure_key = 10.0F;
  float manual_exposure_ev = 9.7F;
  engine::MeteringMode auto_exposure_metering_mode
    = engine::MeteringMode::kAverage;
  float auto_exposure_low_percentile = 0.1F;
  float auto_exposure_high_percentile = 0.9F;
  float auto_exposure_min_log_luminance = -12.0F;
  float auto_exposure_log_luminance_range = 25.0F;
  float auto_exposure_target_luminance = 0.18F;
  float auto_exposure_spot_meter_radius = 0.2F;
  float display_gamma = 2.2F;
  uint32_t exposure_extension_version = kExposureExtensionVersion;
  float auto_exposure_black_influence = 0.0F;
  float auto_exposure_transition_distance_ev
    = engine::kDefaultExposureTransitionDistance;
  ResourceReferenceIndex auto_exposure_metering_mask = kNoResourceReference;
  std::array<uint32_t, 3> exposure_reserved {};
  uint32_t curve_key_count = 0U;
  std::array<uint32_t, 2> curve_reserved {};
};
#pragma pack(pop)
static_assert(sizeof(PostProcessVolumeEnvironmentRecord) == 144U);
static_assert(
  offsetof(PostProcessVolumeEnvironmentRecord, exposure_extension_version)
  == 104U);
static_assert(
  offsetof(PostProcessVolumeEnvironmentRecord, auto_exposure_black_influence)
  == 108U);
static_assert(offsetof(PostProcessVolumeEnvironmentRecord,
                auto_exposure_transition_distance_ev)
  == 112U);
static_assert(
  offsetof(PostProcessVolumeEnvironmentRecord, auto_exposure_metering_mask)
  == 116U);
static_assert(
  offsetof(PostProcessVolumeEnvironmentRecord, exposure_reserved) == 120U);
static_assert(
  offsetof(PostProcessVolumeEnvironmentRecord, curve_key_count) == 132U);
static_assert(
  offsetof(PostProcessVolumeEnvironmentRecord, curve_reserved) == 136U);

//! Validate authored scalar domains before exposing or writing a packed record.
[[nodiscard]] inline auto HasValidPostProcessVolumeValues(
  const PostProcessVolumeEnvironmentRecord& record) noexcept -> bool
{
  const auto scalars = std::array {
    record.exposure_compensation_ev,
    record.auto_exposure_min_ev,
    record.auto_exposure_max_ev,
    record.auto_exposure_speed_up,
    record.auto_exposure_speed_down,
    record.bloom_intensity,
    record.bloom_threshold,
    record.saturation,
    record.contrast,
    record.vignette_intensity,
    record.exposure_key,
    record.manual_exposure_ev,
    record.auto_exposure_low_percentile,
    record.auto_exposure_high_percentile,
    record.auto_exposure_min_log_luminance,
    record.auto_exposure_log_luminance_range,
    record.auto_exposure_target_luminance,
    record.auto_exposure_spot_meter_radius,
    record.display_gamma,
    record.auto_exposure_black_influence,
    record.auto_exposure_transition_distance_ev,
  };
  if (!std::ranges::all_of(scalars,
        [](const float value) -> bool { return std::isfinite(value); })) {
    return false;
  }
  const double histogram_end
    = static_cast<double>(record.auto_exposure_min_log_luminance)
    + record.auto_exposure_log_luminance_range;
  return record.header.system_type
    == nostd::to_underlying(EnvironmentComponentType::kPostProcessVolume)
    && record.enabled <= 1U && record.exposure_enabled <= 1U
    && record.tone_mapper <= engine::ToneMapper::kReinhard
    && record.exposure_mode <= engine::ExposureMode::kAuto
    && record.auto_exposure_metering_mode <= engine::MeteringMode::kSpot
    && record.exposure_key > 0.0F
    && record.auto_exposure_min_ev <= record.auto_exposure_max_ev
    && record.auto_exposure_speed_up >= 0.0F
    && record.auto_exposure_speed_down >= 0.0F
    && record.auto_exposure_low_percentile >= 0.0F
    && record.auto_exposure_high_percentile <= 1.0F
    && record.auto_exposure_low_percentile
    < record.auto_exposure_high_percentile
    && record.auto_exposure_min_log_luminance
    >= engine::kMinExposureLogLuminance
    && record.auto_exposure_log_luminance_range > 0.0F
    && std::isfinite(1.0F / record.auto_exposure_log_luminance_range)
    && histogram_end <= engine::kMaxExposureLogLuminance
    && record.auto_exposure_target_luminance >= 0.0F
    && record.auto_exposure_spot_meter_radius >= 0.0F
    && record.auto_exposure_black_influence >= 0.0F
    && record.auto_exposure_black_influence <= 1.0F
    && record.auto_exposure_transition_distance_ev > 0.0F
    && record.bloom_intensity >= 0.0F && record.bloom_threshold >= 0.0F
    && record.saturation >= 0.0F && record.contrast >= 0.0F
    && record.vignette_intensity >= 0.0F && record.vignette_intensity <= 1.0F
    && record.display_gamma >= engine::kMinDisplayGamma;
}

//! Packed display background, in linear SDR RGB without lighting contribution.
#pragma pack(push, 1)
struct BackgroundEnvironmentRecord {
  SceneEnvironmentSystemRecordHeader header = {
    .system_type = nostd::to_underlying(EnvironmentComponentType::kBackground),
    .record_size = sizeof(BackgroundEnvironmentRecord),
  };

  uint32_t enabled = 1;
  float color_rgb[3] = { 0.0F, 0.0F, 0.0F };
};
#pragma pack(pop)
static_assert(sizeof(BackgroundEnvironmentRecord) == 24);

//! Known SceneEnvironment record tags and packed prefix sizes.
struct EnvironmentRecordSizeEntry {
  uint32_t system_type = 0;
  size_t record_size = 0;
};

inline constexpr std::array kKnownEnvironmentRecordSizes {
  EnvironmentRecordSizeEntry {
    .system_type
    = nostd::to_underlying(EnvironmentComponentType::kSkyAtmosphere),
    .record_size = sizeof(SkyAtmosphereEnvironmentRecord),
  },
  EnvironmentRecordSizeEntry {
    .system_type
    = nostd::to_underlying(EnvironmentComponentType::kVolumetricClouds),
    .record_size = sizeof(VolumetricCloudsEnvironmentRecord),
  },
  EnvironmentRecordSizeEntry {
    .system_type = nostd::to_underlying(EnvironmentComponentType::kFog),
    .record_size = sizeof(FogEnvironmentRecord),
  },
  EnvironmentRecordSizeEntry {
    .system_type = nostd::to_underlying(EnvironmentComponentType::kSkyLight),
    .record_size = sizeof(SkyLightEnvironmentRecord),
  },
  EnvironmentRecordSizeEntry {
    .system_type = nostd::to_underlying(EnvironmentComponentType::kSkySphere),
    .record_size = sizeof(SkySphereEnvironmentRecord),
  },
  EnvironmentRecordSizeEntry {
    .system_type
    = nostd::to_underlying(EnvironmentComponentType::kPostProcessVolume),
    .record_size = sizeof(PostProcessVolumeEnvironmentRecord),
  },
  EnvironmentRecordSizeEntry {
    .system_type = nostd::to_underlying(EnvironmentComponentType::kBackground),
    .record_size = sizeof(BackgroundEnvironmentRecord),
  },
};

//! Validate the current record length, including the bounded exposure-curve
//! tail.
[[nodiscard]] constexpr auto IsValidEnvironmentRecordSize(
  const SceneEnvironmentSystemRecordHeader& header) noexcept -> bool
{
  const auto system_type = header.system_type;
  const auto record_size = header.record_size;
  if (system_type
    == nostd::to_underlying(EnvironmentComponentType::kPostProcessVolume)) {
    constexpr auto kPrefixSize = sizeof(PostProcessVolumeEnvironmentRecord);
    constexpr auto kKeySize = sizeof(ExposureCompensationKeyRecord);
    return record_size >= kPrefixSize
      && record_size
      <= kPrefixSize + (engine::kMaxExposureCompensationCurveKeys * kKeySize)
      && (record_size - kPrefixSize) % kKeySize == 0U;
  }
  for (const auto& entry : kKnownEnvironmentRecordSizes) {
    if (entry.system_type == system_type) {
      return record_size == entry.record_size;
    }
  }
  // Unknown system records retain the envelope's existing skip semantics.
  return true;
}

//! Common shadow settings packed into light component records.
#pragma pack(push, 1)
struct LightShadowSettingsRecord {
  float bias {};
  float normal_bias {};
  uint32_t contact_shadows {};
  uint8_t resolution_hint {}; // ShadowResolutionHint
};
#pragma pack(pop)
static_assert(sizeof(LightShadowSettingsRecord) == 13);

//! Common authored properties shared by all light records.
/*!
  Authored fields are value-initialized: the scene schema owns the defaults,
  and a writer sets every field (see `MakeDefaultRecord`).

  @note Intensity fields are now in specific light records:
  - DirectionalLightRecord::intensity_lux (lux, lm/m²)
  - PointLightRecord::luminous_flux_lm (lumens)
  - SpotLightRecord::luminous_flux_lm (lumens)
*/
#pragma pack(push, 1)
struct LightCommonRecord {
  uint32_t affects_world {};
  float color_rgb[3] {};
  // intensity REMOVED - now in specific light records with physical units

  uint8_t casts_shadows {};

  LightShadowSettingsRecord shadow {};
  float exposure_compensation_ev {};
};
#pragma pack(pop)
static_assert(sizeof(LightCommonRecord) == 34);

//! Packed directional light component record.
/*!
  Contains `intensity_lux` for physical illuminance in lux (lm/m²).

 * Typical values: 100,000 lux (bright sun), 10,000 lux (overcast).

  Authored fields are value-initialized: the scene schema owns the defaults,
  and a writer sets every field (see `MakeDefaultRecord`).
*/
#pragma pack(push, 1)
struct DirectionalLightRecord {
  world::SceneNodeIndexT node_index = 0;
  LightCommonRecord common {};
  float angular_size_radians {};
  uint8_t atmosphere_light_slot {}; // None=0, Primary=1, Secondary=2
  uint8_t use_per_pixel_atmosphere_transmittance {};
  float atmosphere_disk_luminance_scale_rgb[3] {};

  uint32_t cascade_count {};
  float cascade_distances[4] {};
  float distribution_exponent {};
  uint8_t split_mode {};
  float max_shadow_distance {};
  float transition_fraction {};
  float distance_fadeout_fraction {};

  float intensity_lux {}; //!< Illuminance in lux (lm/m²)
};
#pragma pack(pop)
static_assert(sizeof(DirectionalLightRecord) == 97);

//! Packed point light component record.
/*!
  Contains `luminous_flux_lm` for physical luminous flux in lumens.
  Typical values: 800 lm (~60W incandescent), 1600 lm (~100W).

  Authored fields are value-initialized: the scene schema owns the defaults,
  and a writer sets every field (see `MakeDefaultRecord`).
*/
#pragma pack(push, 1)
struct PointLightRecord {
  world::SceneNodeIndexT node_index = 0;
  LightCommonRecord common {};
  float range {};
  float source_radius {};
  float luminous_flux_lm {}; //!< Luminous flux in lumens (lm)
};
#pragma pack(pop)
static_assert(sizeof(PointLightRecord) == 50);

//! Packed spot light component record.
/*!
  Contains `luminous_flux_lm` for physical luminous flux in lumens.
  Typical values: 800 lm (~60W incandescent), 1600 lm (~100W).

  Authored fields are value-initialized: the scene schema owns the defaults,
  and a writer sets every field (see `MakeDefaultRecord`).
*/
#pragma pack(push, 1)
struct SpotLightRecord {
  world::SceneNodeIndexT node_index = 0;
  LightCommonRecord common {};
  float range {};
  float inner_cone_angle_radians {};
  float outer_cone_angle_radians {};
  float source_radius {};
  float luminous_flux_lm {}; //!< Luminous flux in lumens (lm)
};
#pragma pack(pop)
static_assert(sizeof(SpotLightRecord) == 58);
static_assert(offsetof(LightCommonRecord, casts_shadows) == 16);
static_assert(offsetof(LightCommonRecord, shadow) == 17);
static_assert(offsetof(LightCommonRecord, exposure_compensation_ev) == 30);
static_assert(offsetof(PointLightRecord, range) == 38);
static_assert(offsetof(PointLightRecord, source_radius) == 42);
static_assert(offsetof(PointLightRecord, luminous_flux_lm) == 46);
static_assert(offsetof(SpotLightRecord, inner_cone_angle_radians) == 42);
static_assert(offsetof(SpotLightRecord, outer_cone_angle_radians) == 46);
static_assert(offsetof(SpotLightRecord, source_radius) == 50);
static_assert(offsetof(SpotLightRecord, luminous_flux_lm) == 54);
static_assert(offsetof(DirectionalLightRecord, angular_size_radians) == 38);
static_assert(offsetof(DirectionalLightRecord, atmosphere_light_slot) == 42);
static_assert(
  offsetof(DirectionalLightRecord, use_per_pixel_atmosphere_transmittance)
  == 43);
static_assert(
  offsetof(DirectionalLightRecord, atmosphere_disk_luminance_scale_rgb) == 44);
static_assert(offsetof(DirectionalLightRecord, cascade_count) == 56);
static_assert(offsetof(DirectionalLightRecord, cascade_distances) == 60);
static_assert(offsetof(DirectionalLightRecord, distribution_exponent) == 76);
static_assert(offsetof(DirectionalLightRecord, split_mode) == 80);
static_assert(offsetof(DirectionalLightRecord, max_shadow_distance) == 81);
static_assert(offsetof(DirectionalLightRecord, transition_fraction) == 85);
static_assert(
  offsetof(DirectionalLightRecord, distance_fadeout_fraction) == 89);
static_assert(offsetof(DirectionalLightRecord, intensity_lux) == 93);

} // namespace oxygen::data::pak::world

// NOLINTEND(*-avoid-c-arrays)
OXYGEN_DIAGNOSTIC_POP
