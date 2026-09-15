//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/DescriptorDependencies.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>

#include "Fixtures/LoaderTestFixtures.h"

using oxygen::content::loaders::LoadGeometryAsset;

namespace {

class GeometryLoaderContractTest
  : public oxygen::content::testing::BinaryAssetLoaderFixtureBase {
protected:
  template <typename T> auto WriteBlob(const T& value) -> void
  {
    const auto bytes = std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(&value), sizeof(T));
    const auto result = desc_writer_.WriteBlob(bytes);
    ASSERT_TRUE(result) << result.error().message();
  }

  auto WriteProceduralGeometry(const std::string_view recipe_name,
    const std::span<const std::byte> parameters = {}) -> void
  {
    using namespace oxygen::data::pak::geometry;
    GeometryAssetDesc desc {};
    desc.header.asset_type
      = static_cast<uint8_t>(oxygen::data::AssetType::kGeometry);
    desc.lod_count = 1;
    MeshDesc mesh {};
    ASSERT_LT(recipe_name.size(), sizeof(mesh.name));
    std::memcpy(mesh.name, recipe_name.data(), recipe_name.size());
    mesh.mesh_type = static_cast<uint8_t>(oxygen::data::MeshType::kProcedural);
    mesh.submesh_count = 1;
    mesh.mesh_view_count = 1;
    mesh.info.procedural.params_size = static_cast<uint32_t>(parameters.size());
    SubMeshDesc submesh {};
    submesh.material_asset_key
      = oxygen::data::AssetKey::FromVirtualPath("/Art/Materials/Shared.omat");
    submesh.mesh_view_count = 1;
    const MeshViewDesc view {
      .first_index = 0, .index_count = 36, .first_vertex = 0, .vertex_count = 24
    };
    auto packed = desc_writer_.ScopedAlignment(1);
    WriteBlob(desc);
    WriteBlob(mesh);
    if (!parameters.empty()) {
      ASSERT_TRUE(desc_writer_.WriteBlob(parameters));
    }
    WriteBlob(submesh);
    WriteBlob(view);
  }
};

//! Malformed mesh type is a structural decode error and must throw.
NOLINT_TEST_F(
  GeometryLoaderContractTest, LoadGeometryAssetUnsupportedMeshTypeThrows)
{
  oxygen::data::pak::geometry::GeometryAssetDesc desc {};
  desc.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kGeometry);
  desc.lod_count = 1;

  oxygen::data::pak::geometry::MeshDesc mesh {};
  mesh.mesh_type = 0xFF;

  {
    auto packed = desc_writer_.ScopedAlignment(1);
    WriteBlob(desc);
    WriteBlob(mesh);
  }

  const auto context = MakeLoaderContext(false, false);
  EXPECT_THROW({ (void)LoadGeometryAsset(context); }, std::runtime_error);
}

//! Unsupported recipes are decode errors, including metadata-only inspection.
NOLINT_TEST_F(GeometryLoaderContractTest, UnsupportedProceduralGeneratorThrows)
{
  ASSERT_NO_FATAL_FAILURE(WriteProceduralGeometry("UnsupportedGenerator/Mesh"));
  for (const auto parse_only : { false, true }) {
    auto [context, collector] = MakeDecodeLoaderContext();
    context.parse_only = parse_only;
    try {
      (void)LoadGeometryAsset(context);
      FAIL() << "Unsupported procedural generator must fail decoding";
    } catch (const std::runtime_error& error) {
      EXPECT_NE(std::string(error.what()).find("UnsupportedGenerator/Mesh"),
        std::string::npos);
    }
  }
}

//! Rejected procedural dimensions must never reach Mesh's bounds calculation.
NOLINT_TEST_F(GeometryLoaderContractTest, InvalidProceduralParametersThrow)
{
  // Capsule requires at least one hemisphere segment; a complete zero-valued
  // first parameter is an invalid recipe, not a truncated descriptor.
  const uint32_t hemisphere_segments = 0;
  ASSERT_NO_FATAL_FAILURE(WriteProceduralGeometry(
    "Capsule/Mesh", std::as_bytes(std::span(&hemisphere_segments, 1))));
  for (const auto parse_only : { false, true }) {
    auto [context, collector] = MakeDecodeLoaderContext();
    context.parse_only = parse_only;
    EXPECT_THROW((void)LoadGeometryAsset(context), std::runtime_error);
  }
}

//! Valid procedural recipes still materialize in parse-only inspector mode.
NOLINT_TEST_F(
  GeometryLoaderContractTest, ValidProceduralMeshLoadsInDecodeAndParseOnlyModes)
{
  ASSERT_NO_FATAL_FAILURE(WriteProceduralGeometry("Cube/Mesh"));
  for (const auto parse_only : { false, true }) {
    auto [context, collector] = MakeDecodeLoaderContext();
    context.parse_only = parse_only;
    const auto geometry = LoadGeometryAsset(context);
    ASSERT_NE(geometry, nullptr);
    ASSERT_EQ(geometry->LodCount(), 1U);
    const auto& mesh = geometry->MeshAt(0);
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->VertexCount(), 24U);
    EXPECT_EQ(mesh->IndexCount(), 36U);
    ASSERT_EQ(collector->AssetDependencies().size(), 1U);
  }
}

//! Dependency inspection reads material keys without external buffer readers.
NOLINT_TEST_F(
  GeometryLoaderContractTest, InspectDependenciesWithoutLoadingBuffers)
{
  using namespace oxygen::data::pak::geometry;
  const auto material
    = oxygen::data::AssetKey::FromVirtualPath("/Art/Materials/Shared.omat");
  GeometryAssetDesc desc {};
  desc.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kGeometry);
  desc.lod_count = 1;
  MeshDesc mesh {};
  mesh.mesh_type = static_cast<uint8_t>(oxygen::data::MeshType::kStandard);
  mesh.submesh_count = 2;
  SubMeshDesc submesh {};
  submesh.material_asset_key = material;
  {
    auto packed = desc_writer_.ScopedAlignment(1);
    WriteBlob(desc);
    WriteBlob(mesh);
    WriteBlob(submesh);
    WriteBlob(submesh);
  }
  auto context = MakeLoaderContext(true, true);
  const auto result = oxygen::content::InspectDescriptorDependencies(
    *context.desc_reader, {}, oxygen::data::AssetType::kGeometry);
  EXPECT_TRUE(result.complete);
  ASSERT_EQ(result.assets.size(), 1U);
  EXPECT_EQ(result.assets.front(), material);
}

} // namespace
