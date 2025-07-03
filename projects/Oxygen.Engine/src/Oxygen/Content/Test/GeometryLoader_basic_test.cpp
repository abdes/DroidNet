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

namespace {

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::SizeIs;

using GeometryAsset = oxygen::data::GeometryAsset;
using Mesh = oxygen::data::Mesh;
using MemoryStream = oxygen::serio::MemoryStream;
using Reader = oxygen::serio::Reader<MemoryStream>;

//=== GeometryLoader Basic Tests ===---------------------------------------//

//! Scenario: LoadMesh returns nullptr for empty buffer
TEST(GeometryLoader_basic, LoadMesh_ReturnsNullptrOnEmptyBuffer) // NOLINT_BASIC
{
  // Arrange
  std::array<std::byte, 0> buffer {};
  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act & Assert
  EXPECT_THROW(oxygen::content::loaders::LoadMesh(reader), std::runtime_error);
}

//! Scenario: LoadGeometryAsset returns nullptr for empty buffer
TEST(GeometryLoader_basic,
  LoadGeometryAsset_ReturnsNullptrOnEmptyBuffer) // NOLINT_BASIC
{
  // Arrange
  std::array<std::byte, 0> buffer {};
  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act & Assert
  EXPECT_THROW(
    oxygen::content::loaders::LoadGeometryAsset(reader), std::runtime_error);
}

//! Scenario: LoadMesh throws if name is not null-terminated
TEST(GeometryLoader_edge, LoadMesh_ThrowsIfNameNotNullTerminated) // NOLINT_EDGE
{
  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  // Arrange
  std::array<std::byte, sizeof(MeshDesc)> buffer {};
  auto* mesh_desc = reinterpret_cast<MeshDesc*>(buffer.data());
  for (size_t i = 0; i < kMaxNameSize; ++i) {
    mesh_desc->name[i] = 'B';
  }
  // Set vertex and index buffer indices to nonzero (simulate valid references)
  mesh_desc->vertex_buffer = 1;
  mesh_desc->index_buffer = 1;
  mesh_desc->submesh_count = 0;
  mesh_desc->mesh_view_count = 0;
  for (int i = 0; i < 3; ++i) {
    mesh_desc->bounding_box_min[i] = 0.0f;
    mesh_desc->bounding_box_max[i] = 0.0f;
  }
  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act & Assert
  EXPECT_THROW(oxygen::content::loaders::LoadMesh(reader), std::runtime_error);
}

//! Scenario: LoadGeometryAsset throws if header is invalid
TEST(
  GeometryLoader_error, LoadGeometryAsset_ThrowsOnInvalidHeader) // NOLINT_ERROR
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

} // anonymous namespace
