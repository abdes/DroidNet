//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/SurfaceId.h>

// packed structs intentionally embed unaligned NamedType ResourceIndexT fields
OXYGEN_DIAGNOSTIC_PUSH
OXYGEN_DIAGNOSTIC_DISABLE_MSVC(4315)
// NOLINTBEGIN(*-avoid-c-arrays)

//! Oxygen PAK format geometry domain schema.
/*!
 Owns geometry asset descriptors and mesh/submesh/view records.
*/
namespace oxygen::data::pak::geometry {

//! Geometry asset descriptor version for current PAK schema.
[[maybe_unused]] constexpr uint8_t kGeometryAssetVersion
  = version::kGeometryAssetVersion;

//! The most LODs a geometry asset has.
[[maybe_unused]] constexpr uint8_t kMaxLodCount = 8;

//! The most LOD boundaries a policy has: boundary `i` lies between LOD `i` and
//! LOD `i + 1`.
[[maybe_unused]] constexpr uint8_t kMaxLodBoundaryCount = kMaxLodCount - 1;

//! How a LOD policy selects the LOD shown per view.
enum class LodPolicyType : uint8_t {
  kFixed = 0, //!< Always `lod_index`, clamped to the LOD count.
  kDistance = 1, //!< By normalized view distance, with symmetric hysteresis.
  kScreenSpaceError = 2, //!< By screen-space error, with directional
                         //!< hysteresis.
};

//! A geometry LOD policy, the value of the scene schema's `lod-policy` type.
/*!
  Each policy type reads only its own fields; the others are zero.

  - `kFixed`: `lod_index`, below kMaxLodCount.
  - `kDistance`: `thresholds[boundary_count]`, non-decreasing, and
    `hysteresis_ratio` in [0, 0.99].
  - `kScreenSpaceError`: `enter_finer_sse[boundary_count]` and
    `exit_coarser_sse[boundary_count]`, each non-decreasing.

  `boundary_count` is at most kMaxLodBoundaryCount, since a geometry has at most
  kMaxLodCount LODs. Every threshold is finite.

  @see IsValidLodPolicy
*/
#pragma pack(push, 1)
struct LodPolicyRecord {
  LodPolicyType type = LodPolicyType::kFixed;
  uint8_t lod_index = 0;
  uint8_t boundary_count = 0;
  float thresholds[kMaxLodBoundaryCount] = {};
  float hysteresis_ratio = 0.0F;
  float enter_finer_sse[kMaxLodBoundaryCount] = {};
  float exit_coarser_sse[kMaxLodBoundaryCount] = {};
};
#pragma pack(pop)
static_assert(sizeof(LodPolicyRecord) == 91);

//! The surface is drawn by default (the schema's surface `Visibility`).
[[maybe_unused]] constexpr uint32_t kSurfaceFlag_Visible = (1U << 0U);
//! The surface casts shadows by default (the schema's surface `CastsShadows`).
[[maybe_unused]] constexpr uint32_t kSurfaceFlag_CastsShadows = (1U << 1U);

//! The asset defaults of one declared surface.
/*!
  A geometry lists one SurfaceDesc per SurfaceId its submeshes declare, and no
  other. The default material stays on each SubMeshDesc, since LODs may bind
  different defaults to one surface.

  `flags` holds `kSurfaceFlag_*` bits; the other bits are zero.
*/
#pragma pack(push, 1)
struct SurfaceDesc {
  SurfaceId surface_id;
  uint32_t flags = 0;
};
#pragma pack(pop)
static_assert(sizeof(SurfaceDesc) == 20);

//! Geometry asset descriptor
/*!
  Describes a geometry asset, with one or more levels of detail (LODs) for
  efficient rendering. This structure provides the metadata and bounding
  information for the geometry, and is followed by an array of MeshDesc
  structures (one per LOD), then by the surface table.

  ### Relationships

  - 1 GeometryAssetDesc : N MeshDesc (LODs)
  - 1 GeometryAssetDesc : N SurfaceDesc (declared surfaces)
  - 1 MeshDesc : N SubMeshDesc (submeshes)
  - 1 SubMeshDesc : N MeshViewDesc (mesh views)
  - 1 SubMeshDesc : 1 MaterialAsset (by AssetKey)

  ### Notes

  - `lod_count`: Number of LODs, from 1 to kMaxLodCount. Each LOD is described
    by a MeshDesc.
  - `bounding_box_min`, `bounding_box_max`: Axis-aligned bounding box (AABB) for
    the entire geometry, used for culling and spatial queries.
  - Bounds are required and must be pre-computed by tooling. All-zero bounds are
    valid and must not be interpreted as "missing".
  - `surface_count`: Number of SurfaceDesc records, one per declared surface.
  - `default_lod_policy`: The LOD policy of a renderable that does not override
    it.

  @see MeshDesc, SubMeshDesc, MeshViewDesc, SurfaceDesc, AssetHeader
*/
#pragma pack(push, 1)
struct GeometryAssetDesc {
  core::AssetHeader header;
  uint32_t lod_count = 0; // Number of LODs (1 to kMaxLodCount)
  float bounding_box_min[3] = {}; // AABB min coordinates
  float bounding_box_max[3] = {}; // AABB max coordinates
  uint32_t surface_count = 0; // Number of SurfaceDesc records
  LodPolicyRecord default_lod_policy; // LOD policy unless overridden
};
// Followed by: MeshDesc meshes[lod_count], SurfaceDesc surfaces[surface_count],
// then the descriptor string table.
#pragma pack(pop)
static_assert(sizeof(GeometryAssetDesc) == 174);

//! Fields for a standard (static) mesh
/*! Boundaries are required and must be pre-computed by tooling. All-zero
  bounds are valid and must not be interpreted as "missing". */
#pragma pack(push, 1)
struct StandardMeshInfo {
  //!< Reference to vertex buffer
  ResourceReferenceIndex vertex_buffer = kNoResourceReference;
  //!< Reference to index buffer
  ResourceReferenceIndex index_buffer = kNoResourceReference;
  float bounding_box_min[3] = {}; //!< AABB min coordinates
  float bounding_box_max[3] = {}; //!< AABB max coordinates
  uint8_t _reserved[40] = {}; //!< Union arm tail padding to 72 bytes
};
#pragma pack(pop)
static_assert(sizeof(StandardMeshInfo) == 72);

//! Fields for a skinned mesh
/*! Stores references to the vertex and index buffers plus skinning buffers.
  Bounds are required and must be pre-computed by tooling. All-zero bounds are
  valid and must not be interpreted as "missing". */
#pragma pack(push, 1)
struct SkinnedMeshInfo {
  //!< Reference to vertex buffer
  ResourceReferenceIndex vertex_buffer = kNoResourceReference;
  //!< Reference to index buffer
  ResourceReferenceIndex index_buffer = kNoResourceReference;
  //!< Joint indices buffer
  ResourceReferenceIndex joint_index_buffer = kNoResourceReference;
  //!< Joint weights buffer
  ResourceReferenceIndex joint_weight_buffer = kNoResourceReference;
  //!< Inverse bind matrices buffer
  ResourceReferenceIndex inverse_bind_buffer = kNoResourceReference;
  //!< Mesh-to-skeleton remap buffer
  ResourceReferenceIndex joint_remap_buffer = kNoResourceReference;
  //!< Skeleton asset reference - TODO: future
  AssetKey skeleton_asset_key;
  //!< Number of joints referenced by this mesh
  uint16_t joint_count = 0;
  //!< Influences per vertex (1..8)
  uint16_t influences_per_vertex = 0;
  //!< Skinning flags (LBS/DQS, normalization)
  uint32_t flags = 0;
  float bounding_box_min[3] = {}; //!< AABB min coordinates
  float bounding_box_max[3] = {}; //!< AABB max coordinates
};
#pragma pack(pop)
static_assert(sizeof(SkinnedMeshInfo) == 72);

//! Fields for a procedural mesh
#pragma pack(push, 1)
struct ProceduralMeshInfo {
  uint32_t params_size = 0; //!< Size of procedural parameter blob (bytes)
  uint8_t _reserved[68] = {}; //!< Union arm tail padding to 72 bytes
};
#pragma pack(pop)
static_assert(sizeof(ProceduralMeshInfo) == 72);

//! Mesh descriptor (85 bytes + SubMesh table)
/*!
  Describes a single mesh LOD within a geometry asset. Each MeshDesc contains
  references to vertex and index buffers, a list of submeshes, and bounding
  information for the mesh.

  ### Relationships

  - 1 MeshDesc : N SubMeshDesc (submeshes)
  - 1 MeshDesc : 1 vertex buffer, 1 index buffer (by ResourceIndexT)
  - MeshDesc are grouped under GeometryAssetDesc

  ### Notes

  - `submesh_count`: Number of SubMeshDesc structures following this mesh.
  - `mesh_view_count`: Total number of MeshViewDesc structures in all submeshes.
  - `bounding_box_min`, `bounding_box_max`: AABB for the mesh (required).
  - All-zero bounds are valid and must not be interpreted as "missing".

  @see SubMeshDesc, MeshViewDesc, GeometryAssetDesc
*/
#pragma pack(push, 1)
struct MeshDesc {
  //! Mesh name, in the geometry descriptor's string table.
  core::StringTableOffsetT name_offset = 0;
  uint8_t mesh_type = 0; // MeshType enum value
  uint32_t submesh_count = 0; // Number of SubMeshes
  uint32_t mesh_view_count = 0; // Total number of MeshViews (all SubMeshes)
  union {
    //! Static Mesh. All info is self-contained in this structure.
    StandardMeshInfo standard;
    //! Skinned Mesh. Contains skinning buffer references.
    SkinnedMeshInfo skinned;
    //! Procedural Mesh. Parameters blob follow the MeshDesc immediately. Mesh
    //! name is used to identify the procedural mesh type, and should be in the
    //! format: `Generator/MeshName`, where `Generator` is a known procedural
    //! mesh generator type (e.g., `Terrain`, `Plane`, `Sphere`, etc.)
    //! understandable or resolvable by the geometry loader.
    ProceduralMeshInfo procedural;
  } info {};

#define OXYGEN_MESH_IS(NAME, ENUM)                                             \
  [[nodiscard]] constexpr bool Is##NAME() const                                \
  {                                                                            \
    return static_cast<std::underlying_type_t<MeshType>>(MeshType::ENUM)       \
      == mesh_type;                                                            \
  }

  // NOLINTBEGIN
  OXYGEN_MESH_IS(Standard, kStandard)
  OXYGEN_MESH_IS(Procedural, kProcedural)
  OXYGEN_MESH_IS(Skinned, kSkinned)
  OXYGEN_MESH_IS(MorphTarget, kMorphTarget)
  OXYGEN_MESH_IS(Instanced, kInstanced)
  OXYGEN_MESH_IS(Collision, kCollision)
  OXYGEN_MESH_IS(Navigation, kNavigation)
  OXYGEN_MESH_IS(Billboard, kBillboard)
  OXYGEN_MESH_IS(Voxel, kVoxel)
  // NOLINTEND

#undef OXYGEN_MESH_IS
};
// Followed by:
// - Optional blob of data depending on `mesh_type`. Blob size is specified by
//   the MeshInfo structure.
// - SubMeshDesc submeshes[submesh_count];
#pragma pack(pop)
static_assert(sizeof(MeshDesc) == 85);

//! Sub-mesh descriptor (64 bytes + MeshView table)
/*!
  Describes a logical partition of a mesh, typically corresponding to a region
  rendered with a single material. Each SubMeshDesc references a material asset
  and contains a list of mesh views (geometry ranges).

  ### Relationships
  - 1 SubMeshDesc : N MeshViewDesc (mesh views)
  - 1 SubMeshDesc : 1 MaterialAsset (by AssetKey)
  - SubMeshDesc are grouped under MeshDesc

  ### Notes
  - `mesh_view_count`: Number of MeshViewDesc structures following this submesh.
  - `bounding_box_min`, `bounding_box_max`: AABB for the submesh.

  @see MeshDesc, MeshViewDesc, MaterialAssetDesc
*/
#pragma pack(push, 1)
struct SubMeshDesc {
  //! Submesh name, in the geometry descriptor's string table.
  core::StringTableOffsetT name_offset = 0;
  AssetKey material_asset_key; // AssetKey reference to MaterialAsset
  SurfaceId surface_id; // Semantic declaration, shared only explicitly
  uint32_t mesh_view_count = 0; // Number of MeshViews in this SubMesh
  float bounding_box_min[3] = {}; // AABB min coordinates
  float bounding_box_max[3] = {}; // AABB max coordinates
};
// Followed by: MeshViewDesc mesh_views[mesh_view_count]
#pragma pack(pop)
static_assert(sizeof(SubMeshDesc) == 64);

//! Mesh view descriptor
/*!
  Describes a contiguous range of indices and vertices within a mesh. Each view
  is one draw and one occlusion culling unit, so it carries the local bounds of
  its own triangles.

  ### Relationships

  - 1 MeshViewDesc : 1 range in index buffer, 1 range in vertex buffer
  - MeshViewDesc are grouped under SubMeshDesc

  ### Notes

  - `bounding_box_min`, `bounding_box_max`: local AABB of the view's triangles,
    required to lie inside the owning submesh's bounds.

  @see MeshDesc, SubMeshDesc
*/
#pragma pack(push, 1)
struct MeshViewDesc {
  //! Buffer index type for mesh views
  using BufferIndexT = core::DataBlobSizeT;

  BufferIndexT first_index = 0; // Start index in index buffer
  BufferIndexT index_count = 0; // Number of indices
  BufferIndexT first_vertex = 0; // Start vertex in vertex buffer
  BufferIndexT vertex_count = 0; // Number of vertices
  float bounding_box_min[3] = {}; // AABB min coordinates
  float bounding_box_max[3] = {}; // AABB max coordinates
};
#pragma pack(pop)
static_assert(sizeof(MeshViewDesc) == 40);

} // namespace oxygen::data::pak::geometry

// NOLINTEND(*-avoid-c-arrays)
OXYGEN_DIAGNOSTIC_POP
