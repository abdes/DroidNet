//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/ScenePrep/GeometryRef.h>
#include <Oxygen/Vortex/test/Fixtures/GeometryUploaderTest.h>

namespace {

using oxygen::frame::Slot;
using oxygen::vortex::testing::GeometryUploaderTest;
using oxygen::vortex::testing::MakeGeometryAssetKey;

class GeometryUploaderUploadTest : public GeometryUploaderTest { };

//! First use of a geometry schedules upload work and exposes pending tickets.
NOLINT_TEST_F(
  GeometryUploaderUploadTest, FirstUseSchedulesUploadAndReturnsPendingTickets)
{
  // Arrange
  auto& uploader = GeoUploader();
  BeginFrame(Slot { 0 });

  const auto mesh = MakeValidTriangleMesh("Tri");
  const auto asset_key = MakeGeometryAssetKey("upload_first_use_schedules");
  const oxygen::vortex::sceneprep::GeometryRef geometry {
    .asset_key = asset_key,
    .lod_index = oxygen::data::LodIndex {},
    .mesh = mesh,
  };
  (void)uploader.GetOrAllocate(geometry);

  // Act
  uploader.EnsureFrameResources();

  // Assert
  EXPECT_GT(uploader.GetPendingUploadTickets().size(), 0U);
}

//! A mesh schedules both VB and IB uploads.
NOLINT_TEST_F(
  GeometryUploaderUploadTest, DirtyEntrySubmitsVertexAndIndexUploads)
{
  // Arrange
  auto& uploader = GeoUploader();
  BeginFrame(Slot { 0 });
  const oxygen::vortex::sceneprep::GeometryRef geometry {
    .asset_key = MakeGeometryAssetKey("upload_indexed"),
    .lod_index = oxygen::data::LodIndex {},
    .mesh = MakeValidTriangleMesh("Indexed"),
  };

  // Act
  (void)uploader.GetOrAllocate(geometry);
  uploader.EnsureFrameResources();

  // Assert
  EXPECT_EQ(uploader.GetPendingUploadTickets().size(), 2U);
  BeginFrame(Slot { 1 });
  EXPECT_EQ(uploader.GetPendingUploadCount(), 0U);
}

} // namespace
