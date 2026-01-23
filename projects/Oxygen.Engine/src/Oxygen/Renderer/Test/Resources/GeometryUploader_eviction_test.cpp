//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/RendererTag.h>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Renderer/ScenePrep/GeometryRef.h>

#include <Oxygen/Renderer/Test/Resources/GeometryUploaderTest.h>

namespace {

using oxygen::content::EvictionReason;
using oxygen::frame::Slot;
using oxygen::renderer::internal::RendererTagFactory;
using oxygen::renderer::testing::GeometryUploaderTest;

class GeometryUploaderEvictionTest : public GeometryUploaderTest { };

//! Asset eviction invalidates handles and drops pending uploads.
NOLINT_TEST_F(GeometryUploaderEvictionTest, AssetEvictionInvalidatesHandles)
{
  // Arrange
  auto& geo_uploader = GeoUploader();

  BeginFrame(Slot { 0 });

  const auto mesh = MakeValidTriangleMesh("Tri", true);
  const oxygen::data::AssetKey asset_key {
    .guid = oxygen::data::GenerateAssetGuid(),
  };
  const oxygen::engine::sceneprep::GeometryRef geometry {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh,
  };

  const auto handle = geo_uploader.GetOrAllocate(geometry);
  geo_uploader.EnsureFrameResources();
  ASSERT_GT(geo_uploader.GetPendingUploadTickets().size(), 0U);

  // Act
  Loader().EmitGeometryAssetEviction(asset_key, EvictionReason::kRefCountZero);
  geo_uploader.OnFrameStart(RendererTagFactory::Get(), Slot { 1 });

  // Assert
  EXPECT_FALSE(geo_uploader.IsHandleValid(handle));

  const auto indices = geo_uploader.GetShaderVisibleIndices(handle);
  EXPECT_EQ(indices.vertex_srv_index, oxygen::kInvalidShaderVisibleIndex);
  EXPECT_EQ(indices.index_srv_index, oxygen::kInvalidShaderVisibleIndex);
  EXPECT_EQ(geo_uploader.GetPendingUploadCount(), 0U);
}

//! Late upload completions are ignored after asset eviction.
NOLINT_TEST_F(GeometryUploaderEvictionTest, EvictionSuppressesLateCompletion)
{
  // Arrange
  auto& uploader = Uploader();
  auto& geo_uploader = GeoUploader();

  BeginFrame(Slot { 0 });

  const auto mesh = MakeValidTriangleMesh("Tri", true);
  const oxygen::data::AssetKey asset_key {
    .guid = oxygen::data::GenerateAssetGuid(),
  };
  const oxygen::engine::sceneprep::GeometryRef geometry {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh,
  };

  const auto handle = geo_uploader.GetOrAllocate(geometry);
  geo_uploader.EnsureFrameResources();
  ASSERT_GT(geo_uploader.GetPendingUploadTickets().size(), 0U);

  // Act
  Loader().EmitGeometryAssetEviction(asset_key, EvictionReason::kRefCountZero);
  geo_uploader.OnFrameStart(RendererTagFactory::Get(), Slot { 1 });

  uploader.OnFrameStart(RendererTagFactory::Get(), Slot { 2 });
  geo_uploader.OnFrameStart(RendererTagFactory::Get(), Slot { 2 });

  // Assert
  EXPECT_FALSE(geo_uploader.IsHandleValid(handle));

  const auto indices = geo_uploader.GetShaderVisibleIndices(handle);
  EXPECT_EQ(indices.vertex_srv_index, oxygen::kInvalidShaderVisibleIndex);
  EXPECT_EQ(indices.index_srv_index, oxygen::kInvalidShaderVisibleIndex);
}

//! Asset eviction invalidates all LOD handles for the asset.
NOLINT_TEST_F(GeometryUploaderEvictionTest, AssetEvictionInvalidatesAllLods)
{
  // Arrange
  auto& geo_uploader = GeoUploader();

  BeginFrame(Slot { 0 });

  const oxygen::data::AssetKey asset_key {
    .guid = oxygen::data::GenerateAssetGuid(),
  };
  const auto mesh_lod0 = MakeValidTriangleMesh("TriLod0", true);
  const auto mesh_lod1 = MakeValidTriangleMesh("TriLod1", true);

  const oxygen::engine::sceneprep::GeometryRef geometry_lod0 {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh_lod0,
  };
  const oxygen::engine::sceneprep::GeometryRef geometry_lod1 {
    .asset_key = asset_key,
    .lod_index = 1U,
    .mesh = mesh_lod1,
  };

  const auto handle_lod0 = geo_uploader.GetOrAllocate(geometry_lod0);
  const auto handle_lod1 = geo_uploader.GetOrAllocate(geometry_lod1);
  geo_uploader.EnsureFrameResources();
  ASSERT_GT(geo_uploader.GetPendingUploadTickets().size(), 0U);

  // Act
  Loader().EmitGeometryAssetEviction(asset_key, EvictionReason::kRefCountZero);
  geo_uploader.OnFrameStart(RendererTagFactory::Get(), Slot { 1 });

  // Assert
  EXPECT_FALSE(geo_uploader.IsHandleValid(handle_lod0));
  EXPECT_FALSE(geo_uploader.IsHandleValid(handle_lod1));

  const auto indices_lod0 = geo_uploader.GetShaderVisibleIndices(handle_lod0);
  const auto indices_lod1 = geo_uploader.GetShaderVisibleIndices(handle_lod1);
  EXPECT_EQ(indices_lod0.vertex_srv_index, oxygen::kInvalidShaderVisibleIndex);
  EXPECT_EQ(indices_lod0.index_srv_index, oxygen::kInvalidShaderVisibleIndex);
  EXPECT_EQ(indices_lod1.vertex_srv_index, oxygen::kInvalidShaderVisibleIndex);
  EXPECT_EQ(indices_lod1.index_srv_index, oxygen::kInvalidShaderVisibleIndex);
}

//! Evicted assets can be reloaded and publish indices again.
NOLINT_TEST_F(GeometryUploaderEvictionTest, EvictionThenReloadPublishes)
{
  // Arrange
  auto& geo_uploader = GeoUploader();

  BeginFrame(Slot { 0 });

  const auto mesh = MakeValidTriangleMesh("Tri", true);
  const oxygen::data::AssetKey asset_key {
    .guid = oxygen::data::GenerateAssetGuid(),
  };
  const oxygen::engine::sceneprep::GeometryRef geometry {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh,
  };

  const auto handle = geo_uploader.GetOrAllocate(geometry);
  geo_uploader.EnsureFrameResources();

  // Act
  Loader().EmitGeometryAssetEviction(asset_key, EvictionReason::kRefCountZero);
  BeginFrame(Slot { 1 });

  const auto handle_reloaded = geo_uploader.GetOrAllocate(geometry);
  geo_uploader.EnsureFrameResources();
  BeginFrame(Slot { 2 });

  // Assert
  EXPECT_EQ(handle_reloaded.get(), handle.get());
  EXPECT_TRUE(geo_uploader.IsHandleValid(handle_reloaded));

  const auto indices = geo_uploader.GetShaderVisibleIndices(handle_reloaded);
  EXPECT_NE(indices.vertex_srv_index, oxygen::kInvalidShaderVisibleIndex);
  EXPECT_NE(indices.index_srv_index, oxygen::kInvalidShaderVisibleIndex);
}

} // namespace
