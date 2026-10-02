//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <vector>

#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#if defined(_MSC_VER) && defined(_DEBUG)
#  include <new>

#  include <Oxygen/Graphics/Common/Test/HeapAllocationFailure.h>
#endif
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Resources/GeometryUploader.h>
#include <Oxygen/Vortex/ScenePrep/GeometryRef.h>
#include <Oxygen/Vortex/ScenePrep/Handles.h>
#include <Oxygen/Vortex/Test/Fixtures/GeometryUploaderTest.h>

namespace {

using oxygen::content::EvictionReason;
using oxygen::frame::Slot;
using oxygen::vortex::internal::RendererTagFactory;
using oxygen::vortex::testing::GeometryUploaderTest;
using oxygen::vortex::testing::MakeGeometryAssetKey;

class GeometryUploaderEvictionTest : public GeometryUploaderTest { };

//! Asset eviction invalidates handles and drops pending uploads.
NOLINT_TEST_F(GeometryUploaderEvictionTest, AssetEvictionInvalidatesHandles)
{
  // Arrange
  auto& geo_uploader = GeoUploader();

  BeginFrame(Slot { 0 });

  const auto mesh = MakeValidTriangleMesh("Tri", true);
  const auto asset_key = MakeGeometryAssetKey("eviction_invalidates_handles");
  const oxygen::vortex::sceneprep::GeometryRef geometry {
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
  const auto asset_key = MakeGeometryAssetKey("eviction_suppresses_completion");
  const oxygen::vortex::sceneprep::GeometryRef geometry {
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

  const auto asset_key = MakeGeometryAssetKey("eviction_invalidates_all_lods");
  const auto mesh_lod0 = MakeValidTriangleMesh("TriLod0", true);
  const auto mesh_lod1 = MakeValidTriangleMesh("TriLod1", true);

  const oxygen::vortex::sceneprep::GeometryRef geometry_lod0 {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh_lod0,
  };
  const oxygen::vortex::sceneprep::GeometryRef geometry_lod1 {
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
  const auto asset_key = MakeGeometryAssetKey("eviction_then_reload");
  const oxygen::vortex::sceneprep::GeometryRef geometry {
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
  EXPECT_NE(handle_reloaded, handle);
  EXPECT_FALSE(geo_uploader.IsHandleValid(handle));
  EXPECT_TRUE(geo_uploader.IsHandleValid(handle_reloaded));

  const auto indices = geo_uploader.GetShaderVisibleIndices(handle_reloaded);
  EXPECT_NE(indices.vertex_srv_index, oxygen::kInvalidShaderVisibleIndex);
  EXPECT_NE(indices.index_srv_index, oxygen::kInvalidShaderVisibleIndex);
}

class GeometryUploaderReclaimTest : public GeometryUploaderTest {
protected:
  auto GeometryLimits() const
    -> oxygen::vortex::resources::GeometryUploader::MaintenanceLimits override
  {
    return { .max_pending_upload_visits_per_frame = 128U,
      .max_reclaimed_lods_per_frame = 1U };
  }
};

NOLINT_TEST_F(
  GeometryUploaderReclaimTest, DetachesAllLodsBeforeBudgetedReleaseAndReload)
{
  BeginFrame(Slot { 0 });
  auto& geometry = GeoUploader();
  const auto key = MakeGeometryAssetKey("budgeted multi-lod eviction");
  const auto mesh = MakeValidTriangleMesh("Resident", false);
  constexpr std::uint32_t kLodCount = 6U;
  std::vector<oxygen::vortex::sceneprep::GeometryHandle> old_handles;
  for (std::uint32_t lod = 0; lod < kLodCount; ++lod) {
    old_handles.push_back(geometry.GetOrAllocate(
      { .asset_key = key, .lod_index = lod, .mesh = mesh }));
  }
  geometry.EnsureFrameResources();
  auto& registry = GfxPtr()->GetResourceRegistry();
  const auto before = registry.GetRegisteredResourceCount();
  Loader().EmitGeometryAssetEviction(key, EvictionReason::kRefCountZero);
  geometry.OnFrameStart(RendererTagFactory::Get(), Slot { 1 });
  for (const auto handle : old_handles) {
    EXPECT_FALSE(geometry.IsHandleValid(handle));
  }
  EXPECT_EQ(registry.GetRegisteredResourceCount(), before - 1U);
  EXPECT_EQ(geometry.GetPendingUploadCount(), 0U);

  const auto replacement = geometry.GetOrAllocate({ .asset_key = key,
    .lod_index = 0U,
    .mesh = MakeValidTriangleMesh("Reloaded", false) });
  geometry.EnsureFrameResources();
  EXPECT_EQ(registry.GetRegisteredResourceCount(), before);
  for (std::uint32_t frame = 0U; frame < kLodCount - 1U; ++frame) {
    BeginFrame(Slot { frame % 2U });
    EXPECT_EQ(registry.GetRegisteredResourceCount(), before - frame - 1U);
    EXPECT_TRUE(geometry.IsHandleValid(replacement));
  }
  EXPECT_TRUE(
    geometry.GetShaderVisibleIndices(replacement).vertex_srv_index.IsValid());
  EXPECT_EQ(registry.GetRegisteredResourceCount(), before - kLodCount + 1U);
}

NOLINT_TEST_F(
  GeometryUploaderReclaimTest, AdmissionAcceptsQueuedEvictionBeforeReload)
{
  BeginFrame(Slot { 0 });
  auto& geometry = GeoUploader();
  const auto key = MakeGeometryAssetKey("eviction before admission");
  const auto mesh = MakeValidTriangleMesh("Resident", false);
  const auto original = geometry.GetOrAllocate(
    { .asset_key = key, .lod_index = 0U, .mesh = mesh });
  geometry.EnsureFrameResources();
  Loader().EmitGeometryAssetEviction(key, EvictionReason::kRefCountZero);
  const auto replacement = geometry.GetOrAllocate(
    { .asset_key = key, .lod_index = 0U, .mesh = mesh });
  EXPECT_NE(original, replacement);
  EXPECT_FALSE(geometry.IsHandleValid(original));
  geometry.OnFrameStart(RendererTagFactory::Get(), Slot { 1 });
  EXPECT_TRUE(geometry.IsHandleValid(replacement));
}

#if defined(_MSC_VER) && defined(_DEBUG)
NOLINT_TEST_F(GeometryUploaderReclaimTest,
  AllocationFailureKeepsDetachedBuffersOwnedForRetry)
{
  BeginFrame(Slot { 0 });
  auto& geometry = GeoUploader();
  const auto key = MakeGeometryAssetKey("failed retirement admission");
  const auto handle = geometry.GetOrAllocate({ .asset_key = key,
    .lod_index = 0U,
    .mesh = MakeValidTriangleMesh("Resident", false) });
  geometry.EnsureFrameResources();
  const auto before
    = GfxPtr()->GetResourceRegistry().GetRegisteredResourceCount();
  Loader().EmitGeometryAssetEviction(key, EvictionReason::kRefCountZero);
  bool failed = false;
  {
    const oxygen::graphics::testing::HeapAllocationFailure deny_allocations;
    try {
      geometry.OnFrameStart(RendererTagFactory::Get(), Slot { 1 });
    } catch (const std::bad_alloc&) {
      failed = true;
    }
  }
  EXPECT_TRUE(failed);
  EXPECT_FALSE(geometry.IsHandleValid(handle));
  EXPECT_EQ(
    GfxPtr()->GetResourceRegistry().GetRegisteredResourceCount(), before);
  geometry.OnFrameStart(RendererTagFactory::Get(), Slot { 0 });
  EXPECT_EQ(
    GfxPtr()->GetResourceRegistry().GetRegisteredResourceCount(), before - 1U);
}
#endif

} // namespace
