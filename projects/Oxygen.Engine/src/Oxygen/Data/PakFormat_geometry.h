//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Data/PakFormat_core.h>

// packed structs intentionally embed unaligned NamedType ResourceIndexT fields
OXYGEN_DIAGNOSTIC_PUSH
OXYGEN_DIAGNOSTIC_DISABLE_MSVC(4315)
// NOLINTBEGIN(*-avoid-c-arrays,*-magic-numbers)

//! Oxygen PAK format geometry domain schema.
/*!
 Owns geometry asset descriptors and mesh/submesh/view records.
*/
namespace oxygen::data::pak::geometry {

//! Geometry asset descriptor version for current PAK schema.
[[maybe_unused]] constexpr uint8_t kGeometryAssetVersion = 1;

//! Geometry asset descriptor (256 bytes)
/*!
  Describes a geometry asset, with one or more levels of detail (LODs) for
  efficient rendering. This structure provides the metadata and bounding
  information for the geometry, and is followed by an array of MeshDesc
  structures (one per LOD).

  ### Relationships

  - 1 GeometryAssetDesc : N MeshDesc (LODs)
  - 1 MeshDesc : N SubMeshDesc (submeshes)
  - 1 SubMeshDesc : N MeshViewDesc (mesh views)
  - 1 SubMeshDesc : 1 MaterialAsset (by AssetKey)

  ### Notes

  - `lod_count`: Number of LODs (must be >= 1). Each LOD is described by a
    MeshDesc.
  - `bounding_box_min`, `bounding_box_max`: Axis-aligned bounding box (AABB) for
    the entire geometry, used for culling and spatial queries.
  - Bounds are required and must be pre-computed by tooling. All-zero bounds are
    valid and must not be interpreted as "missing".

  @see MeshDesc, SubMeshDesc, MeshViewDesc, AssetHeader
*/
#pragma pack(push, 1)
struct GeometryAssetDesc {
  core::AssetHeader header;
  uint32_t lod_count = 0; // Number of LODs (must be >= 1)
  float bounding_box_min[3] = {}; // AABB min coordinates
  float bounding_box_max[3] = {}; // AABB max coordinates

  // Reserved for future use
  uint8_t reserved[133] = {};
};
// Followed by: MeshDesc meshes[lod_count];
#pragma pack(pop)
static_assert(sizeof(GeometryAssetDesc) == 256);

//! Fields for a standard (static) mesh
/*! Boundaries are required and must be pre-computed by tooling. All-zero
  bounds are valid and must not be interpreted as "missing". */
#pragma pack(push, 1)
struct StandardMeshInfo {
  core::ResourceIndexT vertex_buffer
    = core::kNoResourceIndex; //!< Reference to vertex buffer
  core::ResourceIndexT index_buffer
    = core::kNoResourceIndex; //!< Reference to index buffer
  float bounding_box_min[3] = {}; //!< AABB min coordinates
  float bounding_box_max[3] = {}; //!< AABB max coordinates
};
#pragma pack(pop)
static_assert(sizeof(StandardMeshInfo) == 32);

//! Fields for a skinned mesh
/*! Stores references to the vertex and index buffers plus skinning buffers.
  Bounds are required and must be pre-computed by tooling. All-zero bounds are
  valid and must not be interpreted as "missing". */
#pragma pack(push, 1)
struct SkinnedMeshInfo {
  core::ResourceIndexT vertex_buffer
    = core::kNoResourceIndex; //!< Reference to vertex buffer
  core::ResourceIndexT index_buffer
    = core::kNoResourceIndex; //!< Reference to index buffer
  core::ResourceIndexT joint_index_buffer
    = core::kNoResourceIndex; //!< Joint indices buffer
  core::ResourceIndexT joint_weight_buffer
    = core::kNoResourceIndex; //!< Joint weights buffer
  core::ResourceIndexT inverse_bind_buffer
    = core::kNoResourceIndex; //!< Inverse bind matrices buffer
  core::ResourceIndexT joint_remap_buffer
    = core::kNoResourceIndex; //!< Mesh-to-skeleton remap buffer
  AssetKey skeleton_asset_key = {}; //!< Skeleton asset reference - TODO: future
  uint16_t joint_count = 0; //!< Number of joints referenced by this mesh
  uint16_t influences_per_vertex = 0; //!< Influences per vertex (1..8)
  uint32_t flags = 0; //!< Skinning flags (LBS/DQS, normalization)
  float bounding_box_min[3] = {}; //!< AABB min coordinates
  float bounding_box_max[3] = {}; //!< AABB max coordinates
};
#pragma pack(pop)
static_assert(sizeof(SkinnedMeshInfo) == 72);

//! Fields for a procedural mesh
#pragma pack(push, 1)
struct ProceduralMeshInfo {
  uint32_t params_size = 0; //!< Size of procedural parameter blob (bytes)
};
#pragma pack(pop)
static_assert(sizeof(ProceduralMeshInfo) == 4);

//! Mesh descriptor (104 bytes + SubMesh table)
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
  char name[core::kMaxNameSize] = {};
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
static_assert(sizeof(MeshDesc) == 145);

//! Sub-mesh descriptor (108 bytes + MeshView table)
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
  char name[core::kMaxNameSize] = {};
  AssetKey material_asset_key; // AssetKey reference to MaterialAsset
  uint32_t mesh_view_count = 0; // Number of MeshViews in this SubMesh
  float bounding_box_min[3] = {}; // AABB min coordinates
  float bounding_box_max[3] = {}; // AABB max coordinates
};
// Followed by: MeshViewDesc mesh_views[mesh_view_count]
#pragma pack(pop)
static_assert(sizeof(SubMeshDesc) == 108);

//! Mesh view descriptor (16 bytes)
/*!
  Describes a contiguous range of indices and vertices within a mesh, used for
  rendering a portion of geometry (e.g., a primitive group or section).

  ### Relationships

  - 1 MeshViewDesc : 1 range in index buffer, 1 range in vertex buffer
  - MeshViewDesc are grouped under SubMeshDesc

  @see MeshDesc, SubMeshDesc
*/
#pragma pack(push, 1)
struct MeshViewDesc {
  //! Buffer index type for mesh views (4 bytes)
  using BufferIndexT = core::DataBlobSizeT;

  BufferIndexT first_index = 0; // Start index in index buffer
  BufferIndexT index_count = 0; // Number of indices
  BufferIndexT first_vertex = 0; // Start vertex in vertex buffer
  BufferIndexT vertex_count = 0; // Number of vertices
};
#pragma pack(pop)
static_assert(sizeof(MeshViewDesc) == 16);

} // namespace oxygen::data::pak::geometry

// NOLINTEND(*-avoid-c-arrays,*-magic-numbers)
OXYGEN_DIAGNOSTIC_POP
