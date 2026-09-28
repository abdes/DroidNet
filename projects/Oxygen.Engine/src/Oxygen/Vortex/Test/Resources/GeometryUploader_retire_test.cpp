//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/ScenePrep/GeometryRef.h>
#include <Oxygen/Vortex/Test/Fixtures/GeometryUploaderTest.h>

namespace {

using oxygen::frame::Slot;
using oxygen::vortex::internal::RendererTagFactory;
using oxygen::vortex::testing::GeometryUploaderTest;
using oxygen::vortex::testing::MakeGeometryAssetKey;

class GeometryUploaderRetireTest : public GeometryUploaderTest { };

//! Pending tickets are retained while uploads are not complete.
NOLINT_TEST_F(GeometryUploaderRetireTest, RetireKeepsTicketsWhileIncomplete)
{
  // Arrange
  auto& geo_uploader = GeoUploader();

  BeginFrame(Slot { 0 });

  const auto mesh = MakeValidTriangleMesh("Tri", true);
  const auto asset_key = MakeGeometryAssetKey("retire_keeps_incomplete");
  const oxygen::vortex::sceneprep::GeometryRef geometry {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh,
  };
  (void)geo_uploader.GetOrAllocate(geometry);

  geo_uploader.EnsureFrameResources();
  const auto tickets_before = geo_uploader.GetPendingUploadTickets();
  ASSERT_GT(tickets_before.size(), 0U);

  // Act
  // Call GeometryUploader.OnFrameStart without advancing UploadCoordinator.
  // The consumer observes pending results without advancing queue progress.
  geo_uploader.OnFrameStart(RendererTagFactory::Get(), Slot { 1 });

  // Assert
  const auto tickets_after = geo_uploader.GetPendingUploadTickets();
  EXPECT_EQ(tickets_after.size(), tickets_before.size());
}

//! Completed tickets are retired once UploadCoordinator reports completion.
NOLINT_TEST_F(GeometryUploaderRetireTest, RetireRemovesTicketsWhenComplete)
{
  // Arrange
  auto& uploader = Uploader();
  auto& geo_uploader = GeoUploader();

  BeginFrame(Slot { 0 });

  const auto mesh = MakeValidTriangleMesh("Tri", true);
  const auto asset_key = MakeGeometryAssetKey("retire_removes_complete");
  const oxygen::vortex::sceneprep::GeometryRef geometry {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh,
  };
  (void)geo_uploader.GetOrAllocate(geometry);

  geo_uploader.EnsureFrameResources();
  ASSERT_GT(geo_uploader.GetPendingUploadTickets().size(), 0U);

  // Act
  // Observe completion before the geometry consumer polls its tickets.
  uploader.OnFrameStart(RendererTagFactory::Get(), Slot { 1 });
  geo_uploader.OnFrameStart(RendererTagFactory::Get(), Slot { 1 });

  // Assert
  EXPECT_EQ(geo_uploader.GetPendingUploadTickets().size(), 0U);
}

//! A paused consumer can publish completed uploads after many frame cycles.
NOLINT_TEST_F(
  GeometryUploaderRetireTest, DelayedConsumerPublishesRetainedResults)
{
  // Arrange
  auto& uploader = Uploader();
  auto& geo_uploader = GeoUploader();

  BeginFrame(Slot { 0 });

  const auto mesh = MakeValidTriangleMesh("Tri", true);
  const auto asset_key = MakeGeometryAssetKey("delayed_consumer");
  const oxygen::vortex::sceneprep::GeometryRef geometry {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh,
  };
  const auto handle = geo_uploader.GetOrAllocate(geometry);

  geo_uploader.EnsureFrameResources();
  ASSERT_GT(geo_uploader.GetPendingUploadTickets().size(), 0U);

  for (auto frame = 0U; frame < 20U; ++frame) {
    uploader.OnFrameStart(RendererTagFactory::Get(), Slot { frame % 2U });
  }
  geo_uploader.OnFrameStart(RendererTagFactory::Get(), Slot { 0 });

  const auto indices = geo_uploader.GetShaderVisibleIndices(handle);
  EXPECT_TRUE(indices.vertex_srv_index.IsValid());
  EXPECT_TRUE(indices.index_srv_index.IsValid());
  EXPECT_EQ(geo_uploader.GetPendingUploadTickets().size(), 0U);
}

//! Reusing the submission slot while first observing completion must publish
//! the original buffers, without scheduling replacement uploads.
NOLINT_TEST_F(GeometryUploaderRetireTest, RecycledSlotPublishesCompletedBuffers)
{
  BeginFrame(Slot { 0 });
  auto& geometry_uploader = GeoUploader();
  const oxygen::vortex::sceneprep::GeometryRef geometry {
    .asset_key = MakeGeometryAssetKey("completion_at_slot_reuse"),
    .lod_index = 0U,
    .mesh = MakeValidTriangleMesh("Recycled slot", true),
  };
  const auto handle = geometry_uploader.GetOrAllocate(geometry);
  geometry_uploader.EnsureFrameResources();
  ASSERT_EQ(geometry_uploader.GetPendingUploadCount(), 2U);

  Uploader().OnFrameStart(RendererTagFactory::Get(), Slot { 0 });
  geometry_uploader.OnFrameStart(RendererTagFactory::Get(), Slot { 0 });
  const auto indices = geometry_uploader.GetShaderVisibleIndices(handle);

  EXPECT_TRUE(indices.vertex_srv_index.IsValid());
  EXPECT_TRUE(indices.index_srv_index.IsValid());
  EXPECT_EQ(geometry_uploader.GetPendingUploadCount(), 0U);
}

} // namespace
