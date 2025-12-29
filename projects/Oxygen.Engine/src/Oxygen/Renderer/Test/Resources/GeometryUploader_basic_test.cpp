//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Renderer/ScenePrep/GeometryRef.h>
#include <Oxygen/Renderer/ScenePrep/Handles.h>

#include <Oxygen/Renderer/Test/Resources/GeometryUploaderTest.h>

namespace {

using oxygen::kInvalidShaderVisibleIndex;
using oxygen::engine::sceneprep::kInvalidGeometryHandle;
using oxygen::renderer::resources::GeometryUploader;
using oxygen::renderer::testing::GeometryUploaderTest;

class GeometryUploaderBasicTest : public GeometryUploaderTest { };

//! MeshShaderVisibleIndices uses ShaderVisibleIndex invalid sentinel.
NOLINT_TEST_F(GeometryUploaderBasicTest,
  MeshShaderVisibleIndicesDefaultsToInvalidShaderVisibleIndex)
{
  // Arrange
  const auto indices = GeometryUploader::MeshShaderVisibleIndices {};

  // Assert
  EXPECT_EQ(indices.vertex_srv_index, kInvalidShaderVisibleIndex);
  EXPECT_EQ(indices.index_srv_index, kInvalidShaderVisibleIndex);
}

//! GetOrAllocate returns a valid handle for a valid mesh.
NOLINT_TEST_F(
  GeometryUploaderBasicTest, GetOrAllocateValidMeshReturnsValidHandle)
{
  // Arrange
  auto& uploader = GeoUploader();
  BeginFrame(oxygen::frame::Slot { 0 });

  const auto mesh = MakeValidTriangleMesh("Tri", true);
  const oxygen::data::AssetKey asset_key {
    .guid = oxygen::data::GenerateAssetGuid(),
  };
  const oxygen::engine::sceneprep::GeometryRef geometry {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh,
  };

  // Act
  const auto handle = uploader.GetOrAllocate(geometry);

  // Assert
  EXPECT_TRUE(uploader.IsHandleValid(handle));
}

//! Same (AssetKey, lod) must return the same handle.
NOLINT_TEST_F(
  GeometryUploaderBasicTest, GetOrAllocateSameIdentityReturnsSameHandle)
{
  // Arrange
  auto& uploader = GeoUploader();
  BeginFrame(oxygen::frame::Slot { 0 });

  const auto mesh = MakeValidTriangleMesh("Tri", true);
  const oxygen::data::AssetKey asset_key {
    .guid = oxygen::data::GenerateAssetGuid(),
  };
  const oxygen::engine::sceneprep::GeometryRef geometry {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh,
  };

  // Act
  const auto handle_0 = uploader.GetOrAllocate(geometry);
  const auto handle_1 = uploader.GetOrAllocate(geometry);

  // Assert
  EXPECT_EQ(handle_1, handle_0);
}

//! Different (AssetKey, lod) must produce different handles.
NOLINT_TEST_F(GeometryUploaderBasicTest,
  GetOrAllocateDifferentIdentityReturnsDifferentHandle)
{
  // Arrange
  auto& uploader = GeoUploader();
  BeginFrame(oxygen::frame::Slot { 0 });

  const auto mesh_a = MakeValidTriangleMesh("TriA", true);
  const auto mesh_b = MakeValidTriangleMesh("TriB", true);
  const oxygen::data::AssetKey asset_key_a {
    .guid = oxygen::data::GenerateAssetGuid(),
  };
  const oxygen::data::AssetKey asset_key_b {
    .guid = oxygen::data::GenerateAssetGuid(),
  };
  const oxygen::engine::sceneprep::GeometryRef geometry_a {
    .asset_key = asset_key_a,
    .lod_index = 0U,
    .mesh = mesh_a,
  };
  const oxygen::engine::sceneprep::GeometryRef geometry_b {
    .asset_key = asset_key_b,
    .lod_index = 0U,
    .mesh = mesh_b,
  };

  // Act
  const auto handle_a = uploader.GetOrAllocate(geometry_a);
  const auto handle_b = uploader.GetOrAllocate(geometry_b);

  // Assert
  EXPECT_NE(handle_a, handle_b);
}

//! Criticality upgrades must not force reupload and must keep handle stable.
NOLINT_TEST_F(
  GeometryUploaderBasicTest, GetOrAllocateCriticalityUpgradeIsSticky)
{
  // Arrange
  auto& uploader = GeoUploader();
  BeginFrame(oxygen::frame::Slot { 0 });

  const auto mesh = MakeValidTriangleMesh("Tri", true);
  const oxygen::data::AssetKey asset_key {
    .guid = oxygen::data::GenerateAssetGuid(),
  };
  const oxygen::engine::sceneprep::GeometryRef geometry {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh,
  };

  const auto handle_0 = uploader.GetOrAllocate(geometry, false);
  uploader.EnsureFrameResources();
  ASSERT_GT(uploader.GetPendingUploadCount(), 0U);

  // Retire initial uploads.
  BeginFrame(oxygen::frame::Slot { 1 });
  ASSERT_EQ(uploader.GetPendingUploadCount(), 0U);

  // Act: upgrade criticality.
  const auto handle_1 = uploader.GetOrAllocate(geometry, true);
  uploader.EnsureFrameResources();

  // Assert
  EXPECT_EQ(handle_1, handle_0);
  EXPECT_EQ(uploader.GetPendingUploadCount(), 0U);

  // Act: attempt downgrade; upgrade must remain sticky.
  const auto handle_2 = uploader.GetOrAllocate(geometry, false);
  uploader.EnsureFrameResources();

  // Assert
  EXPECT_EQ(handle_2, handle_0);
  EXPECT_EQ(uploader.GetPendingUploadCount(), 0U);
}

//! Invalid meshes either assert (debug checks) or return invalid handle.
NOLINT_TEST_F(GeometryUploaderBasicTest,
  GetOrAllocateInvalidMeshReturnsInvalidGeometryHandleOrAsserts)
{
  // Arrange
  auto& uploader = GeoUploader();
  BeginFrame(oxygen::frame::Slot { 0 });

  const auto mesh = MakeInvalidMesh_NoVertices("Bad");
  const oxygen::data::AssetKey asset_key {
    .guid = oxygen::data::GenerateAssetGuid(),
  };
  const oxygen::engine::sceneprep::GeometryRef geometry {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh,
  };

  // Act / Assert
#if LOGURU_DEBUG_CHECKS
  EXPECT_DEATH((void)uploader.GetOrAllocate(geometry), "");
#else
  const auto handle = uploader.GetOrAllocate(geometry);
  EXPECT_EQ(handle, kInvalidGeometryHandle);
#endif
}

//! Invalid handle sentinel is reported invalid.
NOLINT_TEST_F(GeometryUploaderBasicTest, IsHandleValidInvalidHandleIsFalse)
{
  // Arrange
  auto& uploader = GeoUploader();

  // Act / Assert
  EXPECT_FALSE(uploader.IsHandleValid(kInvalidGeometryHandle));
}

//! Invalid handle must return invalid SRV indices.
NOLINT_TEST_F(GeometryUploaderBasicTest,
  GetShaderVisibleIndicesInvalidHandleReturnsInvalidIndices)
{
  // Arrange
  auto& uploader = GeoUploader();

  // Act
  const auto indices = uploader.GetShaderVisibleIndices(kInvalidGeometryHandle);

  // Assert
  EXPECT_EQ(indices.vertex_srv_index, kInvalidShaderVisibleIndex);
  EXPECT_EQ(indices.index_srv_index, kInvalidShaderVisibleIndex);
}

} // namespace
