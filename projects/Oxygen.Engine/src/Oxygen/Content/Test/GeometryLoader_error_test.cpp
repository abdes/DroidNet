//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/MemoryStream.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Reader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>
#include <array>
#include <span>
#include <vector>

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::SizeIs;

using GeometryAsset = oxygen::data::GeometryAsset;
using Mesh = oxygen::data::Mesh;
using MemoryStream = oxygen::serio::MemoryStream;
using Reader = oxygen::serio::Reader<MemoryStream>;

namespace {

//=== GeometryLoader Edge Tests ===---------------------------------------//

//!
//! Fixture for GeometryLoader edge tests
//!
struct GeometryLoaderEdgeTest : public ::testing::Test { };

//! Scenario: LoadMesh throws if name is not null-terminated
TEST_F(
  GeometryLoaderEdgeTest, LoadMesh_ThrowsIfNameNotNullTerminated) // NOLINT_EDGE
{
  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  // Arrange
  std::array<std::byte, sizeof(MeshDesc) + sizeof(SubMeshDesc)> buffer {};
  auto* mesh_desc = reinterpret_cast<MeshDesc*>(buffer.data());
  for (size_t i = 0; i < kMaxNameSize; ++i) {
    mesh_desc->name[i] = 'B';
  }
  mesh_desc->vertex_buffer = 1;
  mesh_desc->index_buffer = 1;
  mesh_desc->submesh_count = 1; // At least one submesh required
  mesh_desc->mesh_view_count = 0;
  for (int i = 0; i < 3; ++i) {
    mesh_desc->bounding_box_min[i] = 0.0f;
    mesh_desc->bounding_box_max[i] = 0.0f;
  }
  // Add a dummy submesh with valid structure
  auto* submesh_desc
    = reinterpret_cast<SubMeshDesc*>(buffer.data() + sizeof(MeshDesc));
  submesh_desc->name[0] = 'S';
  submesh_desc->name[1] = '\0';
  submesh_desc->material_asset_key
    = AssetKey {}; // Use default constructed AssetKey
  submesh_desc->mesh_view_count = 0;
  for (int i = 0; i < 3; ++i) {
    submesh_desc->bounding_box_min[i] = 0.0f;
    submesh_desc->bounding_box_max[i] = 0.0f;
  }
  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act & Assert
  EXPECT_THROW(oxygen::content::loaders::LoadMesh(reader), std::exception);
}

//=== GeometryLoader Error Tests ===---------------------------------------//

//!
//! Fixture for GeometryLoader error tests
//!
struct GeometryLoaderErrorTest : public ::testing::Test { };

//! Scenario: LoadGeometryAsset throws if header is invalid
TEST_F(GeometryLoaderErrorTest,
  LoadGeometryAsset_ThrowsOnInvalidHeader) // NOLINT_ERROR
{
  // Arrange
  std::array<std::byte, sizeof(oxygen::data::pak::GeometryAssetDesc)> buffer {};
  auto* desc
    = reinterpret_cast<oxygen::data::pak::GeometryAssetDesc*>(buffer.data());
  desc->header.asset_type = 255; // Invalid asset type
  desc->lod_count = 0;
  for (int i = 0; i < 3; ++i) {
    desc->bounding_box_min[i] = 0.0f;
    desc->bounding_box_max[i] = 0.0f;
  }
  std::fill(reinterpret_cast<uint8_t*>(desc->reserved),
    reinterpret_cast<uint8_t*>(desc->reserved) + sizeof(desc->reserved), 0);
  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act & Assert
  EXPECT_THROW(
    oxygen::content::loaders::LoadGeometryAsset(reader), std::runtime_error);
}

//!
//! Scenario: LoadMesh throws if buffer is too short for MeshDesc (already
//! covered in basic)
//!
TEST_F(GeometryLoaderErrorTest, LoadMesh_ThrowsOnShortBuffer) // NOLINT_ERROR
{
  // Already covered in basic tests.
}

//!
//! Scenario: LoadMesh throws if vertex buffer index is invalid
//!
TEST_F(GeometryLoaderErrorTest,
  LoadMesh_ThrowsOnInvalidVertexBufferIndex) // NOLINT_ERROR
{
  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  // Arrange
  std::array<std::byte, sizeof(MeshDesc) + sizeof(SubMeshDesc)> buffer {};
  auto* mesh_desc = reinterpret_cast<MeshDesc*>(buffer.data());
  mesh_desc->name[0] = 'A';
  mesh_desc->name[1] = '\0';
  mesh_desc->vertex_buffer = static_cast<ResourceIndexT>(-1); // Invalid index
  mesh_desc->index_buffer = 1;
  mesh_desc->submesh_count = 1;
  mesh_desc->mesh_view_count = 0;
  for (int i = 0; i < 3; ++i) {
    mesh_desc->bounding_box_min[i] = 0.0f;
    mesh_desc->bounding_box_max[i] = 0.0f;
  }
  auto* submesh_desc
    = reinterpret_cast<SubMeshDesc*>(buffer.data() + sizeof(MeshDesc));
  submesh_desc->name[0] = 'S';
  submesh_desc->name[1] = '\0';
  submesh_desc->material_asset_key
    = AssetKey {}; // Use default constructed AssetKey
  submesh_desc->mesh_view_count = 0;
  for (int i = 0; i < 3; ++i) {
    submesh_desc->bounding_box_min[i] = 0.0f;
    submesh_desc->bounding_box_max[i] = 0.0f;
  }
  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act & Assert
  EXPECT_THROW(oxygen::content::loaders::LoadMesh(reader), std::exception);
}

//!
//! Scenario: LoadMesh throws if index buffer index is invalid
//!
TEST_F(GeometryLoaderErrorTest,
  LoadMesh_ThrowsOnInvalidIndexBufferIndex) // NOLINT_ERROR
{
  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  // Arrange
  std::array<std::byte, sizeof(MeshDesc) + sizeof(SubMeshDesc)> buffer {};
  auto* mesh_desc = reinterpret_cast<MeshDesc*>(buffer.data());
  mesh_desc->name[0] = 'A';
  mesh_desc->name[1] = '\0';
  mesh_desc->vertex_buffer = 1;
  mesh_desc->index_buffer = static_cast<ResourceIndexT>(-1); // Invalid index
  mesh_desc->submesh_count = 1;
  mesh_desc->mesh_view_count = 0;
  for (int i = 0; i < 3; ++i) {
    mesh_desc->bounding_box_min[i] = 0.0f;
    mesh_desc->bounding_box_max[i] = 0.0f;
  }
  auto* submesh_desc
    = reinterpret_cast<SubMeshDesc*>(buffer.data() + sizeof(MeshDesc));
  submesh_desc->name[0] = 'S';
  submesh_desc->name[1] = '\0';
  submesh_desc->material_asset_key
    = AssetKey {}; // Use default constructed AssetKey
  submesh_desc->mesh_view_count = 0;
  for (int i = 0; i < 3; ++i) {
    submesh_desc->bounding_box_min[i] = 0.0f;
    submesh_desc->bounding_box_max[i] = 0.0f;
  }
  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act & Assert
  EXPECT_THROW(oxygen::content::loaders::LoadMesh(reader), std::exception);
}

//!
//! Scenario: LoadMesh throws if submesh_count is excessively large
//!
TEST_F(GeometryLoaderErrorTest,
  LoadMesh_ThrowsOnSubMeshCountOverflow) // NOLINT_ERROR
{
  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  // Arrange
  std::array<std::byte, sizeof(MeshDesc)> buffer {};
  auto* mesh_desc = reinterpret_cast<MeshDesc*>(buffer.data());
  mesh_desc->name[0] = 'A';
  mesh_desc->name[1] = '\0';
  mesh_desc->vertex_buffer = 1;
  mesh_desc->index_buffer = 1;
  mesh_desc->submesh_count = 0xFFFFFFFF; // Unreasonably large
  mesh_desc->mesh_view_count = 0;
  for (int i = 0; i < 3; ++i) {
    mesh_desc->bounding_box_min[i] = 0.0f;
    mesh_desc->bounding_box_max[i] = 0.0f;
  }
  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act & Assert
  EXPECT_THROW(oxygen::content::loaders::LoadMesh(reader), std::exception);
}

//!
//! Scenario: LoadMesh throws if mesh_view_count is excessively large
//!
TEST_F(GeometryLoaderErrorTest,
  LoadMesh_ThrowsOnMeshViewCountOverflow) // NOLINT_ERROR
{
  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  // Arrange
  std::array<std::byte, sizeof(MeshDesc) + sizeof(SubMeshDesc)> buffer {};
  auto* mesh_desc = reinterpret_cast<MeshDesc*>(buffer.data());
  mesh_desc->name[0] = 'A';
  mesh_desc->name[1] = '\0';
  mesh_desc->vertex_buffer = 1;
  mesh_desc->index_buffer = 1;
  mesh_desc->submesh_count = 1;
  mesh_desc->mesh_view_count = 0xFFFFFFFF; // Unreasonably large
  for (int i = 0; i < 3; ++i) {
    mesh_desc->bounding_box_min[i] = 0.0f;
    mesh_desc->bounding_box_max[i] = 0.0f;
  }
  auto* submesh_desc
    = reinterpret_cast<SubMeshDesc*>(buffer.data() + sizeof(MeshDesc));
  submesh_desc->name[0] = 'S';
  submesh_desc->name[1] = '\0';
  submesh_desc->material_asset_key
    = AssetKey {}; // Use default constructed AssetKey
  submesh_desc->mesh_view_count = 0;
  for (int i = 0; i < 3; ++i) {
    submesh_desc->bounding_box_min[i] = 0.0f;
    submesh_desc->bounding_box_max[i] = 0.0f;
  }
  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act & Assert
  EXPECT_THROW(oxygen::content::loaders::LoadMesh(reader), std::exception);
}

//!
//! Scenario: LoadMesh throws if bounding box contains NaN or Inf
//!
TEST_F(
  GeometryLoaderErrorTest, LoadMesh_ThrowsOnInvalidBoundingBox) // NOLINT_ERROR
{
  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  // Arrange
  std::array<std::byte, sizeof(MeshDesc) + sizeof(SubMeshDesc)> buffer {};
  auto* mesh_desc = reinterpret_cast<MeshDesc*>(buffer.data());
  mesh_desc->name[0] = 'A';
  mesh_desc->name[1] = '\0';
  mesh_desc->vertex_buffer = 1;
  mesh_desc->index_buffer = 1;
  mesh_desc->submesh_count = 1;
  mesh_desc->mesh_view_count = 0;
  mesh_desc->bounding_box_min[0] = std::numeric_limits<float>::quiet_NaN();
  mesh_desc->bounding_box_min[1] = 0.0f;
  mesh_desc->bounding_box_min[2] = 0.0f;
  mesh_desc->bounding_box_max[0] = std::numeric_limits<float>::infinity();
  mesh_desc->bounding_box_max[1] = 0.0f;
  mesh_desc->bounding_box_max[2] = 0.0f;
  auto* submesh_desc
    = reinterpret_cast<SubMeshDesc*>(buffer.data() + sizeof(MeshDesc));
  submesh_desc->name[0] = 'S';
  submesh_desc->name[1] = '\0';
  submesh_desc->material_asset_key
    = AssetKey {}; // Use default constructed AssetKey
  submesh_desc->mesh_view_count = 0;
  for (int i = 0; i < 3; ++i) {
    submesh_desc->bounding_box_min[i] = 0.0f;
    submesh_desc->bounding_box_max[i] = 0.0f;
  }
  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act & Assert
  EXPECT_THROW(oxygen::content::loaders::LoadMesh(reader), std::exception);
}

} // namespace
