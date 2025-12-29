//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/RendererTag.h>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Renderer/ScenePrep/GeometryRef.h>

#include <Oxygen/Renderer/Test/Resources/GeometryUploaderTest.h>

namespace {

using oxygen::frame::Slot;
using oxygen::renderer::internal::RendererTagFactory;
using oxygen::renderer::testing::GeometryUploaderTest;

class GeometryUploaderRetireTest : public GeometryUploaderTest { };

//! Pending tickets are retained while uploads are not complete.
NOLINT_TEST_F(GeometryUploaderRetireTest, RetireKeepsTicketsWhileIncomplete)
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
  (void)geo_uploader.GetOrAllocate(geometry);

  geo_uploader.EnsureFrameResources();
  const auto tickets_before = geo_uploader.GetPendingUploadTickets();
  ASSERT_GT(tickets_before.size(), 0U);

  // Act
  // Call GeometryUploader.OnFrameStart without advancing UploadCoordinator.
  // This exercises the `IsComplete() == false` path deterministically.
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
  const oxygen::data::AssetKey asset_key {
    .guid = oxygen::data::GenerateAssetGuid(),
  };
  const oxygen::engine::sceneprep::GeometryRef geometry {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh,
  };
  (void)geo_uploader.GetOrAllocate(geometry);

  geo_uploader.EnsureFrameResources();
  ASSERT_GT(geo_uploader.GetPendingUploadTickets().size(), 0U);

  // Act
  // Advance to the next slot to avoid UploadTracker slot-cleanup of our
  // tickets, then retire.
  uploader.OnFrameStart(RendererTagFactory::Get(), Slot { 1 });
  geo_uploader.OnFrameStart(RendererTagFactory::Get(), Slot { 1 });

  // Assert
  EXPECT_EQ(geo_uploader.GetPendingUploadTickets().size(), 0U);
}

//! TicketNotFound from UploadTracker is treated as terminal and tickets are
//! dropped.
NOLINT_TEST_F(GeometryUploaderRetireTest, RetireDropsTicketsOnTicketNotFound)
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
  (void)geo_uploader.GetOrAllocate(geometry);

  geo_uploader.EnsureFrameResources();
  ASSERT_GT(geo_uploader.GetPendingUploadTickets().size(), 0U);

  // Act
  // Re-enter the same frame slot: UploadTracker::OnFrameStart performs
  // slot-based cleanup and erases entries created in this slot, making
  // IsComplete() return TicketNotFound.
  uploader.OnFrameStart(RendererTagFactory::Get(), Slot { 0 });
  geo_uploader.OnFrameStart(RendererTagFactory::Get(), Slot { 0 });

  // Assert
  EXPECT_EQ(geo_uploader.GetPendingUploadTickets().size(), 0U);
}

} // namespace
