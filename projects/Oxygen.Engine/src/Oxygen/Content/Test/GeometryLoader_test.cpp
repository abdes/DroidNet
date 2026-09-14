//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <span>
#include <stdexcept>

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
