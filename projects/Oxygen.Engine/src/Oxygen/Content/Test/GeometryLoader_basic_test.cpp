//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <span>
#include <vector>

#include <Oxygen/Base/MemoryStream.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Reader.h>
#include <Oxygen/Base/Writer.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::SizeIs;

using GeometryAsset = oxygen::data::GeometryAsset;
using Mesh = oxygen::data::Mesh;
using MemoryStream = oxygen::serio::MemoryStream;
using Reader = oxygen::serio::Reader<MemoryStream>;

namespace {

//=== GeometryLoader Basic Tests ===---------------------------------------//

//!
//! Fixture for GeometryLoader basic tests
//!
struct GeometryLoaderBasicTest : public ::testing::Test { };

//! Scenario: LoadMesh throws if buffer is too short for MeshDesc
TEST_F(GeometryLoaderBasicTest, LoadMesh_ThrowsOnShortBuffer) // NOLINT_BASIC
{
  // Arrange
  std::array<std::byte, sizeof(oxygen::data::pak::MeshDesc) - 4> buffer {};
  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act & Assert
  EXPECT_THROW(oxygen::content::loaders::LoadMesh(reader), std::runtime_error);
}

//! Scenario: LoadGeometryAsset throws if buffer is too short for
//! GeometryAssetDesc
TEST_F(GeometryLoaderBasicTest,
  LoadGeometryAsset_ThrowsOnShortBuffer) // NOLINT_BASIC
{
  // Arrange
  std::array<std::byte, sizeof(oxygen::data::pak::GeometryAssetDesc) - 4>
    buffer {};
  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act & Assert
  EXPECT_THROW(
    oxygen::content::loaders::LoadGeometryAsset(reader), std::runtime_error);
}

//! Scenario: LoadMesh returns nullptr for empty buffer
TEST_F(
  GeometryLoaderBasicTest, LoadMesh_ReturnsNullptrOnEmptyBuffer) // NOLINT_BASIC
{
  // Arrange
  std::array<std::byte, 0> buffer {};
  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act & Assert
  EXPECT_THROW(oxygen::content::loaders::LoadMesh(reader), std::runtime_error);
}

//! Scenario: LoadGeometryAsset returns nullptr for empty buffer
TEST_F(GeometryLoaderBasicTest,
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

//! Scenario: LoadGeometryAsset parses a minimal valid asset
TEST_F(GeometryLoaderBasicTest,
  LoadGeometryAsset_ParsesValidMinimalAsset) // NOLINT_BASIC
{
  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  // Arrange
  // GeometryAssetDesc + 1 MeshDesc + 1 SubMeshDesc
  constexpr uint32_t lod_count = 1;
  constexpr uint32_t submesh_count = 1;
  // Use dynamically growing MemoryStream (internal buffer)
  MemoryStream stream;
  oxygen::serio::Writer<MemoryStream> writer(stream);
  auto align_guard = writer.ScopedAlignement(1);

  // Write AssetHeader as a struct to match loader and format
  AssetHeader header {};
  header.asset_type = 1;
  {
    constexpr auto src = "TestAsset";
    constexpr auto src_len = std::char_traits<char>::length(src);
    constexpr auto copy_len = std::min(src_len, kMaxNameSize - 1);
    std::copy_n(src, copy_len, header.name);
    std::fill(header.name + copy_len, header.name + kMaxNameSize, '\0');
  }
  header.version = 1;
  header.streaming_priority = 0;
  header.content_hash = 0x12345678;
  header.variant_flags = 0;
  (void)writer.write(header);

  // lod_count
  (void)writer.write(lod_count);

  // bounding_box_min
  float bbox_min[3] = { -1.0f, -1.0f, -1.0f };
  for (float v : bbox_min)
    (void)writer.write(v);

  // bounding_box_max
  float bbox_max[3] = { 1.0f, 1.0f, 1.0f };
  for (float v : bbox_max)
    (void)writer.write(v);

  // reserved (write zeros)
  std::array<std::byte, sizeof(oxygen::data::pak::GeometryAssetDesc::reserved)>
    reserved {};
  (void)writer.write_blob(reserved);

  // MeshDesc (write fields individually)
  {
    constexpr auto src = "TestMesh";
    constexpr auto src_len = std::char_traits<char>::length(src);
    constexpr auto copy_len = std::min(src_len, kMaxNameSize - 1);
    char mesh_name[kMaxNameSize] = {};
    std::copy_n(src, copy_len, mesh_name);
    std::fill(mesh_name + copy_len, mesh_name + kMaxNameSize, '\0');
    (void)writer.write_blob(
      std::as_bytes(std::span<const char>(mesh_name, kMaxNameSize)));
  }
  (void)writer.write(static_cast<ResourceIndexT>(1)); // vertex_buffer
  (void)writer.write(static_cast<ResourceIndexT>(1)); // index_buffer
  (void)writer.write(submesh_count);
  (void)writer.write(
    static_cast<uint32_t>(1)); // mesh_view_count = 1 (required)
  float mesh_bbox_min[3] = { -1.0f, -1.0f, -1.0f };
  for (float v : mesh_bbox_min)
    (void)writer.write(v);
  float mesh_bbox_max[3] = { 1.0f, 1.0f, 1.0f };
  for (float v : mesh_bbox_max)
    (void)writer.write(v);

  // SubMeshDesc (write fields individually)
  {
    constexpr auto src = "TestSubMesh";
    constexpr auto src_len = std::char_traits<char>::length(src);
    constexpr auto copy_len = std::min(src_len, kMaxNameSize - 1);
    char submesh_name[kMaxNameSize] = {};
    std::copy_n(src, copy_len, submesh_name);
    std::fill(submesh_name + copy_len, submesh_name + kMaxNameSize, '\0');
    (void)writer.write_blob(
      std::as_bytes(std::span<const char>(submesh_name, kMaxNameSize)));
  }
  (void)writer.write(oxygen::data::AssetKey {}); // material_asset_key
  (void)writer.write(
    static_cast<uint32_t>(1)); // mesh_view_count = 1 (required)
  float submesh_bbox_min[3] = { -1.0f, -1.0f, -1.0f };
  for (float v : submesh_bbox_min)
    (void)writer.write(v);
  float submesh_bbox_max[3] = { 1.0f, 1.0f, 1.0f };
  for (float v : submesh_bbox_max)
    (void)writer.write(v);

  // Write one valid MeshViewDesc (vertex_count > 0 required)
  MeshViewDesc mesh_view {};
  mesh_view.vertex_count = 1;
  mesh_view.index_count = 1;
  (void)writer.write(mesh_view);

  (void)stream.seek(0); // Reset to beginning for reading
  Reader reader(stream);

  // Act
  std::unique_ptr<GeometryAsset> asset;
  EXPECT_NO_THROW(asset = oxygen::content::loaders::LoadGeometryAsset(reader));

  // Assert
  ASSERT_NE(asset, nullptr);
  // Use direct access to desc_ for assertions, since public API is limited
  const auto& desc
    = *reinterpret_cast<const GeometryAssetDesc*>(&asset->GetHeader());
  // ...

  EXPECT_EQ(desc.lod_count, 1u);
  EXPECT_FLOAT_EQ(desc.bounding_box_min[0], -1.0f);
  EXPECT_FLOAT_EQ(desc.bounding_box_max[2], 1.0f);
  // No further mesh/submesh structure is accessible from GeometryAsset API
}

} // namespace
