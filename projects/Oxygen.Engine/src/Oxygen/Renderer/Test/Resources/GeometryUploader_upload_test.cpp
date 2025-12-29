//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Renderer/ScenePrep/GeometryRef.h>

#include <Oxygen/Renderer/Test/Resources/GeometryUploaderTest.h>

namespace {

using oxygen::frame::Slot;
using oxygen::renderer::testing::GeometryUploaderTest;

class GeometryUploaderUploadTest : public GeometryUploaderTest { };

//! First use of a geometry schedules upload work and exposes pending tickets.
NOLINT_TEST_F(
  GeometryUploaderUploadTest, FirstUseSchedulesUploadAndReturnsPendingTickets)
{
  // Arrange
  auto& uploader = GeoUploader();
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
  (void)uploader.GetOrAllocate(geometry);

  // Act
  uploader.EnsureFrameResources();

  // Assert
  EXPECT_GT(uploader.GetPendingUploadTickets().size(), 0U);
}

//! Indexed meshes schedule both VB and IB uploads; non-indexed schedule VB
//! only.
NOLINT_TEST_F(
  GeometryUploaderUploadTest, DirtyEntrySubmitsVertexAndIndexUploadsWhenPresent)
{
  // Arrange
  auto& uploader = GeoUploader();

  const oxygen::data::AssetKey asset_key_a {
    .guid = oxygen::data::GenerateAssetGuid(),
  };
  const oxygen::data::AssetKey asset_key_b {
    .guid = oxygen::data::GenerateAssetGuid(),
  };

  // Act/Assert: indexed mesh
  BeginFrame(Slot { 0 });

  const auto mesh_indexed = MakeValidTriangleMesh("Indexed", true);
  const oxygen::engine::sceneprep::GeometryRef geometry_indexed {
    .asset_key = asset_key_a,
    .lod_index = 0U,
    .mesh = mesh_indexed,
  };

  (void)uploader.GetOrAllocate(geometry_indexed);
  uploader.EnsureFrameResources();

  const auto indexed_tickets = uploader.GetPendingUploadTickets();
  EXPECT_EQ(indexed_tickets.size(), 2U);

  // Retire at a frame boundary.
  BeginFrame(Slot { 1 });
  EXPECT_EQ(uploader.GetPendingUploadCount(), 0U);

  // Act/Assert: non-indexed mesh
  BeginFrame(Slot { 2 });

  const auto mesh_non_indexed = MakeValidTriangleMesh("NonIndexed", false);
  const oxygen::engine::sceneprep::GeometryRef geometry_non_indexed {
    .asset_key = asset_key_b,
    .lod_index = 0U,
    .mesh = mesh_non_indexed,
  };

  (void)uploader.GetOrAllocate(geometry_non_indexed);
  uploader.EnsureFrameResources();

  const auto non_indexed_tickets = uploader.GetPendingUploadTickets();
  EXPECT_EQ(non_indexed_tickets.size(), 1U);
}

} // namespace
