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
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::NiceMock;
using ::testing::SizeIs;

using GeometryAsset = oxygen::data::GeometryAsset;
using Mesh = oxygen::data::Mesh;
using MemoryStream = oxygen::serio::MemoryStream;
using Reader = oxygen::serio::Reader<MemoryStream>;

namespace {

//=== Mock AssetLoader
//===------------------------------------------------------//

//! Mock AssetLoader for lightweight testing without PAK file dependencies.
class MockAssetLoader : public oxygen::content::AssetLoader {
public:
  MOCK_METHOD(void, AddAssetDependency,
    (const oxygen::data::AssetKey&, const oxygen::data::AssetKey&), (override));
  MOCK_METHOD(void, AddResourceDependency,
    (const oxygen::data::AssetKey&, oxygen::content::ResourceKey), (override));
};

//=== GeometryLoader Basic Tests ===---------------------------------------//

//!
//! Fixture for GeometryLoader basic tests
//!
struct GeometryLoaderBasicTest : public ::testing::Test {
protected:
  NiceMock<MockAssetLoader> asset_loader;

  //! Helper method to create LoaderContext for testing.
  template <typename StreamT>
  auto CreateLoaderContext(oxygen::serio::Reader<StreamT>& reader)
    -> oxygen::content::LoaderContext<StreamT>
  {
    return oxygen::content::LoaderContext<StreamT> { .asset_loader
      = &asset_loader,
      .current_asset_key = oxygen::data::AssetKey {}, // Test asset key
      .reader = std::ref(reader),
      .offline = false };
  }
};

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
  auto context = CreateLoaderContext(reader);
  EXPECT_THROW(
    oxygen::content::loaders::LoadGeometryAsset(context), std::runtime_error);
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
  auto context = CreateLoaderContext(reader);
  EXPECT_THROW(oxygen::content::loaders::LoadMesh(context), std::runtime_error);
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
  auto context = CreateLoaderContext(reader);
  EXPECT_THROW(
    oxygen::content::loaders::LoadGeometryAsset(context), std::runtime_error);
}

//! Scenario: LoadGeometryAsset parses a minimal valid asset
TEST_F(GeometryLoaderBasicTest,
  LoadGeometryAsset_ParsesValidMinimalAsset) // NOLINT_BASIC
{
  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  // Arrange
  constexpr uint32_t lod_count = 1;
  constexpr uint32_t submesh_count = 1;
  constexpr uint32_t mesh_view_count = 1;
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

  // Write MeshDesc (fields individually)
  char mesh_name[kMaxNameSize] = "TestMesh";
  (void)writer.write_blob(
    std::as_bytes(std::span<const char>(mesh_name, kMaxNameSize)));
  (void)writer.write(MeshType::kStandard); // mesh_type
  (void)writer.write(submesh_count);
  (void)writer.write(static_cast<uint32_t>(1)); // mesh_view_count
  (void)writer.write(static_cast<ResourceIndexT>(1)); // vertex_buffer
  (void)writer.write(static_cast<ResourceIndexT>(1)); // index_buffer
  float mesh_bbox_min[3] = { -1.0f, -1.0f, -1.0f };
  for (float v : mesh_bbox_min)
    (void)writer.write(v);
  float mesh_bbox_max[3] = { 1.0f, 1.0f, 1.0f };
  for (float v : mesh_bbox_max)
    (void)writer.write(v);

  // DEBUG: Print offset after MeshDesc
  std::cout << "[DEBUG] Offset after MeshDesc: " << stream.position().value()
            << std::endl;

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
  (void)writer.write(mesh_view_count);
  float submesh_bbox_min[3] = { -1.0f, -1.0f, -1.0f };
  for (float v : submesh_bbox_min)
    (void)writer.write(v);
  float submesh_bbox_max[3] = { 1.0f, 1.0f, 1.0f };
  for (float v : submesh_bbox_max)
    (void)writer.write(v);

  // DEBUG: Print offset after SubMeshDesc
  std::cout << "[DEBUG] Offset after SubMeshDesc: " << stream.position().value()
            << std::endl;

  // Write one valid MeshViewDesc (vertex_count > 0 required)
  MeshViewDesc mesh_view {};
  mesh_view.vertex_count = 1;
  mesh_view.index_count = 1;
  (void)writer.write(mesh_view);

  // DEBUG: Print offset after MeshViewDesc
  std::cout << "[DEBUG] Offset after MeshViewDesc: "
            << stream.position().value() << std::endl;

  // DEBUG: Hex dump the entire buffer before reading
  std::cout << "[DEBUG] Full buffer hex dump (" << stream.position().value()
            << " bytes):" << std::endl;
  const auto* data
    = reinterpret_cast<const std::uint8_t*>(stream.data().data());
  for (std::size_t i = 0; i < stream.position().value(); ++i) {
    if (i % 16 == 0)
      std::cout << std::endl << std::setw(4) << std::setfill('0') << i << ": ";
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(data[i]) << " ";
  }
  std::cout << std::dec << std::endl;

  (void)stream.seek(0); // Reset to beginning for reading
  Reader reader(stream);

  // Act
  std::unique_ptr<GeometryAsset> asset;
  auto context = CreateLoaderContext(reader);
  EXPECT_NO_THROW(asset = oxygen::content::loaders::LoadGeometryAsset(context));

  // Assert
  ASSERT_NE(asset, nullptr);
  const auto& desc
    = *reinterpret_cast<const GeometryAssetDesc*>(&asset->GetHeader());

  std::cout << "[DEBUG] lod_count: " << desc.lod_count << std::endl;
  std::cout << "[DEBUG] bbox_min[0]: " << desc.bounding_box_min[0] << std::endl;
  std::cout << "[DEBUG] bbox_max[2]: " << desc.bounding_box_max[2] << std::endl;

  EXPECT_EQ(desc.lod_count, 1u);
  EXPECT_FLOAT_EQ(desc.bounding_box_min[0], -1.0f);
  EXPECT_FLOAT_EQ(desc.bounding_box_max[2], 1.0f);
}

//=== GeometryLoader Dependency Management Tests ===------------------------//

//! Test: LoadGeometryAsset registers resource dependencies for vertex/index
//! buffers.
TEST_F(GeometryLoaderBasicTest,
  LoadGeometryAsset_ValidBuffers_RegistersResourceDependencies) // NOLINT_BASIC
{
  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  // Arrange
  constexpr uint32_t lod_count = 1;
  constexpr uint32_t submesh_count = 1;
  constexpr ResourceIndexT vertex_buffer_index = 100;
  constexpr ResourceIndexT index_buffer_index = 101;

  MemoryStream stream;
  oxygen::serio::Writer<MemoryStream> writer(stream);
  auto align_guard = writer.ScopedAlignement(1);

  // Write minimal GeometryAssetDesc
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
  (void)writer.write(header);
  (void)writer.write(lod_count);

  // Bounding boxes
  float bbox[6] = { -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
  for (float v : bbox)
    (void)writer.write(v);

  // Reserved
  std::array<std::byte, sizeof(GeometryAssetDesc::reserved)> reserved {};
  (void)writer.write_blob(reserved);

  // Write MeshDesc (fields individually)
  char mesh_name[kMaxNameSize] = "TestMesh";
  (void)writer.write_blob(
    std::as_bytes(std::span<const char>(mesh_name, kMaxNameSize)));
  (void)writer.write(MeshType::kStandard); // mesh_type
  (void)writer.write(submesh_count);
  (void)writer.write(static_cast<uint32_t>(1)); // mesh_view_count
  (void)writer.write(vertex_buffer_index); // vertex_buffer
  (void)writer.write(index_buffer_index); // index_buffer
  float mesh_bbox_min[3] = { -1.0f, -1.0f, -1.0f };
  for (float v : mesh_bbox_min)
    (void)writer.write(v);
  float mesh_bbox_max[3] = { 1.0f, 1.0f, 1.0f };
  for (float v : mesh_bbox_max)
    (void)writer.write(v);

  // Write SubMeshDesc
  char submesh_name[kMaxNameSize] = "TestSubMesh";
  (void)writer.write_blob(
    std::as_bytes(std::span<const char>(submesh_name, kMaxNameSize)));
  (void)writer.write(
    AssetKey {}); // material_asset_key (zero - no asset dependency)
  (void)writer.write(static_cast<uint32_t>(1)); // mesh_view_count
  for (float v : bbox)
    (void)writer.write(v); // submesh bounding box

  // Write MeshViewDesc
  MeshViewDesc mesh_view {};
  mesh_view.vertex_count = 1;
  mesh_view.index_count = 1;
  (void)writer.write(mesh_view);

  (void)stream.seek(0);
  Reader reader(stream);

  // Expect resource dependency registrations for vertex and index buffers
  EXPECT_CALL(
    asset_loader, AddResourceDependency(::testing::_, vertex_buffer_index));
  EXPECT_CALL(
    asset_loader, AddResourceDependency(::testing::_, index_buffer_index));

  // Act
  std::unique_ptr<GeometryAsset> asset;
  auto context = CreateLoaderContext(reader);
  EXPECT_NO_THROW(asset = oxygen::content::loaders::LoadGeometryAsset(context));

  // Assert
  ASSERT_NE(asset, nullptr);
}

//! Test: LoadGeometryAsset registers asset dependencies for materials.
TEST_F(GeometryLoaderBasicTest,
  LoadGeometryAsset_ValidMaterial_RegistersAssetDependencies) // NOLINT_BASIC
{
  using namespace oxygen::data;
  using namespace oxygen::data::pak;

  // Arrange
  constexpr uint32_t lod_count = 1;
  constexpr uint32_t submesh_count = 1;
  AssetKey material_key { .guid = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
                            14, 15, 16 } }; // Non-zero material key

  MemoryStream stream;
  oxygen::serio::Writer<MemoryStream> writer(stream);
  auto align_guard = writer.ScopedAlignement(1);

  // Write minimal GeometryAssetDesc
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
  (void)writer.write(header);
  (void)writer.write(lod_count);

  // Bounding boxes
  float bbox[6] = { -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
  for (float v : bbox)
    (void)writer.write(v);

  // Reserved
  std::array<std::byte, sizeof(GeometryAssetDesc::reserved)> reserved {};
  (void)writer.write_blob(reserved);

  // Write MeshDesc (fields individually)
  char mesh_name[kMaxNameSize] = "TestMesh";
  (void)writer.write_blob(
    std::as_bytes(std::span<const char>(mesh_name, kMaxNameSize)));
  (void)writer.write(MeshType::kStandard); // mesh_type
  (void)writer.write(submesh_count);
  (void)writer.write(static_cast<uint32_t>(1)); // mesh_view_count
  (void)writer.write(static_cast<ResourceIndexT>(1)); // vertex_buffer
  (void)writer.write(static_cast<ResourceIndexT>(1)); // index_buffer
  float mesh_bbox_min[3] = { -1.0f, -1.0f, -1.0f };
  for (float v : mesh_bbox_min)
    (void)writer.write(v);
  float mesh_bbox_max[3] = { 1.0f, 1.0f, 1.0f };
  for (float v : mesh_bbox_max)
    (void)writer.write(v);

  // Write SubMeshDesc with material dependency
  char submesh_name[kMaxNameSize] = "TestSubMesh";
  (void)writer.write_blob(
    std::as_bytes(std::span<const char>(submesh_name, kMaxNameSize)));
  (void)writer.write(material_key); // material_asset_key (non-zero)
  (void)writer.write(static_cast<uint32_t>(1)); // mesh_view_count
  for (float v : bbox)
    (void)writer.write(v); // submesh bounding box

  // Write MeshViewDesc
  MeshViewDesc mesh_view {};
  mesh_view.vertex_count = 1;
  mesh_view.index_count = 1;
  (void)writer.write(mesh_view);

  (void)stream.seek(0);
  Reader reader(stream);

  // Expect asset dependency registration for material
  EXPECT_CALL(asset_loader, AddAssetDependency(::testing::_, material_key));

  // Act
  std::unique_ptr<GeometryAsset> asset;
  auto context = CreateLoaderContext(reader);
  EXPECT_NO_THROW(asset = oxygen::content::loaders::LoadGeometryAsset(context));

  // Assert
  ASSERT_NE(asset, nullptr);
}

} // namespace
