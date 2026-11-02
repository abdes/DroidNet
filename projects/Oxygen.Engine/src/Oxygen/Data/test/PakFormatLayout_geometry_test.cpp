//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include "PakFormatWireBytes.h"

#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace geometry = oxygen::data::pak::geometry;

using oxygen::data::MeshType;
using oxygen::data::ResourceReferenceIndex;

using oxygen::data::pak::testing::Encoded;
using oxygen::data::pak::testing::HeaderBytes;
using oxygen::data::pak::testing::Key;
using oxygen::data::pak::testing::MakeHeader;
using oxygen::data::pak::testing::Surface;
using oxygen::data::pak::testing::WireBytes;

//! A LOD policy with distinct values.
auto MakePolicy() -> geometry::LodPolicyRecord
{
  return geometry::LodPolicyRecord {
    .type = geometry::LodPolicyType::kScreenSpaceError,
    .lod_index = 3U,
    .boundary_count = 7U,
    .thresholds = { 1.5F, 2.5F, 3.5F, 4.5F, 5.5F, 6.5F, 7.5F, },
    .hysteresis_ratio = 8.5F,
    .enter_finer_sse = { 9.5F, 10.5F, 11.5F, 12.5F, 13.5F, 14.5F, 15.5F, },
    .exit_coarser_sse = { 16.5F, 17.5F, 18.5F, 19.5F, 20.5F, 21.5F, 22.5F, },
  };
}

//! The documented bytes of the policy `MakePolicy` returns: type, fixed LOD,
//! boundary count, thresholds, hysteresis, enter and exit screen-space errors.
auto PolicyBytes() -> WireBytes
{
  auto bytes = WireBytes {};
  bytes.U8(2U).U8(3U).U8(7U).Floats(1.5F, 7).F32(8.5F).Floats(9.5F, 7).Floats(
    16.5F, 7);
  return bytes;
}

//! A LOD policy is its type, fixed LOD index, boundary count, distance
//! thresholds, hysteresis ratio, and enter and exit screen-space errors.
NOLINT_TEST(PakFormatLayoutGeometryTest, LodPolicyRecordWireBytes)
{
  const auto policy = MakePolicy();

  EXPECT_EQ(Encoded(policy), PolicyBytes().Bytes());
}

//! A surface is its surface id and flags.
NOLINT_TEST(PakFormatLayoutGeometryTest, SurfaceDescWireBytes)
{
  const auto surface = geometry::SurfaceDesc {
    .surface_id = Surface(0x10U),
    .flags = 0x21222324U,
  };

  const auto expected = WireBytes {}.Run(0x10U, 16).U32(0x21222324U);

  EXPECT_EQ(Encoded(surface), expected.Bytes());
}

//! A geometry asset is its header, LOD count, bounds, surface count and
//! default LOD policy.
NOLINT_TEST(PakFormatLayoutGeometryTest, GeometryAssetDescWireBytes)
{
  const auto desc = geometry::GeometryAssetDesc {
    .header = MakeHeader(),
    .lod_count = 0x81828384U,
    .bounding_box_min = { 1.5F, 2.5F, 3.5F, },
    .bounding_box_max = { 4.5F, 5.5F, 6.5F, },
    .surface_count = 0x91929394U,
    .default_lod_policy = MakePolicy(),
  };

  auto expected = WireBytes {};
  expected.Append(HeaderBytes())
    .U32(0x81828384U)
    .Floats(1.5F, 3)
    .Floats(4.5F, 3)
    .U32(0x91929394U)
    .Append(PolicyBytes());

  EXPECT_EQ(Encoded(desc), expected.Bytes());
}

//! A standard mesh is its name, type, submesh and view counts, then the
//! standard arm: vertex and index buffer references, bounds and a zeroed
//! reserve.
NOLINT_TEST(PakFormatLayoutGeometryTest, MeshDescStandardArmWireBytes)
{
  auto mesh = geometry::MeshDesc {
    .name_offset = 0x11121314U,
    .mesh_type = std::to_underlying(MeshType::kStandard),
    .submesh_count = 0x21222324U,
    .mesh_view_count = 0x31323334U,
  };
  mesh.info.standard = geometry::StandardMeshInfo {
    .vertex_buffer = ResourceReferenceIndex { 0x41424344U },
    .index_buffer = ResourceReferenceIndex { 0x51525354U },
    .bounding_box_min = { 1.5F, 2.5F, 3.5F, },
    .bounding_box_max = { 4.5F, 5.5F, 6.5F, },
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .U8(1U)
                          .U32(0x21222324U)
                          .U32(0x31323334U)
                          .U32(0x41424344U)
                          .U32(0x51525354U)
                          .Floats(1.5F, 3)
                          .Floats(4.5F, 3)
                          .Zeros(40);

  EXPECT_EQ(Encoded(mesh), expected.Bytes());
}

//! A skinned mesh is its name, type, submesh and view counts, then the skinned
//! arm: six buffer references, skeleton key, joint count, influences, flags
//! and bounds.
NOLINT_TEST(PakFormatLayoutGeometryTest, MeshDescSkinnedArmWireBytes)
{
  auto mesh = geometry::MeshDesc {
    .name_offset = 0x11121314U,
    .mesh_type = std::to_underlying(MeshType::kSkinned),
    .submesh_count = 0x21222324U,
    .mesh_view_count = 0x31323334U,
  };
  mesh.info.skinned = geometry::SkinnedMeshInfo {
    .vertex_buffer = ResourceReferenceIndex { 0x41424344U },
    .index_buffer = ResourceReferenceIndex { 0x51525354U },
    .joint_index_buffer = ResourceReferenceIndex { 0x61626364U },
    .joint_weight_buffer = ResourceReferenceIndex { 0x71727374U },
    .inverse_bind_buffer = ResourceReferenceIndex { 0x81828384U },
    .joint_remap_buffer = ResourceReferenceIndex { 0x91929394U },
    .skeleton_asset_key = Key(0xA0U),
    .joint_count = 0xB1B2U,
    .influences_per_vertex = 0xC1C2U,
    .flags = 0xD1D2D3D4U,
    .bounding_box_min = { 1.5F, 2.5F, 3.5F, },
    .bounding_box_max = { 4.5F, 5.5F, 6.5F, },
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .U8(3U)
                          .U32(0x21222324U)
                          .U32(0x31323334U)
                          .U32(0x41424344U)
                          .U32(0x51525354U)
                          .U32(0x61626364U)
                          .U32(0x71727374U)
                          .U32(0x81828384U)
                          .U32(0x91929394U)
                          .Run(0xA0U, 16)
                          .U16(0xB1B2U)
                          .U16(0xC1C2U)
                          .U32(0xD1D2D3D4U)
                          .Floats(1.5F, 3)
                          .Floats(4.5F, 3);

  EXPECT_EQ(Encoded(mesh), expected.Bytes());
}

//! A procedural mesh is its name, type, submesh and view counts, then the
//! procedural arm: parameter blob size and a zeroed reserve.
NOLINT_TEST(PakFormatLayoutGeometryTest, MeshDescProceduralArmWireBytes)
{
  auto mesh = geometry::MeshDesc {
    .name_offset = 0x11121314U,
    .mesh_type = std::to_underlying(MeshType::kProcedural),
    .submesh_count = 0x21222324U,
    .mesh_view_count = 0x31323334U,
  };
  mesh.info.procedural = geometry::ProceduralMeshInfo {
    .params_size = 0x41424344U,
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .U8(2U)
                          .U32(0x21222324U)
                          .U32(0x31323334U)
                          .U32(0x41424344U)
                          .Zeros(68);

  EXPECT_EQ(Encoded(mesh), expected.Bytes());
}

//! A submesh is its name, material key, surface id, view count and bounds.
NOLINT_TEST(PakFormatLayoutGeometryTest, SubMeshDescWireBytes)
{
  const auto submesh = geometry::SubMeshDesc {
    .name_offset = 0x11121314U,
    .material_asset_key = Key(0x20U),
    .surface_id = Surface(0x30U),
    .mesh_view_count = 0x41424344U,
    .bounding_box_min = { 1.5F, 2.5F, 3.5F, },
    .bounding_box_max = { 4.5F, 5.5F, 6.5F, },
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .Run(0x20U, 16)
                          .Run(0x30U, 16)
                          .U32(0x41424344U)
                          .Floats(1.5F, 3)
                          .Floats(4.5F, 3);

  EXPECT_EQ(Encoded(submesh), expected.Bytes());
}

//! A mesh view is its index and vertex ranges and bounds.
NOLINT_TEST(PakFormatLayoutGeometryTest, MeshViewDescWireBytes)
{
  const auto view = geometry::MeshViewDesc {
    .first_index = 0x11121314U,
    .index_count = 0x21222324U,
    .first_vertex = 0x31323334U,
    .vertex_count = 0x41424344U,
    .bounding_box_min = { 1.5F, 2.5F, 3.5F, },
    .bounding_box_max = { 4.5F, 5.5F, 6.5F, },
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .U32(0x21222324U)
                          .U32(0x31323334U)
                          .U32(0x41424344U)
                          .Floats(1.5F, 3)
                          .Floats(4.5F, 3);

  EXPECT_EQ(Encoded(view), expected.Bytes());
}

} // namespace
