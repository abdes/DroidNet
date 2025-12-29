//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Renderer/ScenePrep/GeometryRef.h>

#include <Oxygen/Renderer/Test/Resources/GeometryUploaderTest.h>

namespace {

using oxygen::frame::Slot;
using oxygen::renderer::testing::GeometryUploaderTest;

class GeometryUploaderUpdateTest : public GeometryUploaderTest { };

//! Update marks geometry dirty and re-schedules uploads on next ensure.
NOLINT_TEST_F(GeometryUploaderUpdateTest,
  UpdateSameEpochMarksDirtyAndSchedulesUploadNextEnsure)
{
  // Arrange
  auto& uploader = GeoUploader();

  BeginFrame(Slot { 0 });
  const auto mesh_v1 = MakeValidTriangleMesh("Tri", true);
  const oxygen::data::AssetKey asset_key {
    .guid = oxygen::data::GenerateAssetGuid(),
  };
  const oxygen::engine::sceneprep::GeometryRef geometry_v1 {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh_v1,
  };
  const auto handle = uploader.GetOrAllocate(geometry_v1);

  uploader.EnsureFrameResources();
  const auto tickets_0 = uploader.GetPendingUploadTickets();
  ASSERT_GT(tickets_0.size(), 0U);

  // Let the upload coordinator observe a frame boundary, then retire.
  // This mimics real Renderer order: UploadCoordinator.OnFrameStart() then
  // GeometryUploader.OnFrameStart().
  BeginFrame(Slot { 1 });

  // Act: hot-reload with a new mesh object (same identity by name here).
  const auto mesh_v2 = MakeValidTriangleMesh("Tri", true);
  const oxygen::engine::sceneprep::GeometryRef geometry_v2 {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh_v2,
  };
  uploader.Update(handle, geometry_v2);

  // Ensure in the same frame triggers upload scheduling for dirty entries.
  uploader.EnsureFrameResources();

  // Assert: new tickets were issued (fresh upload work was scheduled).
  const auto tickets_1 = uploader.GetPendingUploadTickets();
  EXPECT_GT(tickets_1.size(), 0U);

  // Contract: interning map updates so new mesh instance yields same handle.
  const auto handle_v2 = uploader.GetOrAllocate(geometry_v2);
  EXPECT_EQ(handle_v2, handle);
}

//! Update must not be used to rebind a handle to unrelated geometry.
NOLINT_TEST_F(
  GeometryUploaderUpdateTest, UpdateDifferentMeshTriggersDebugAssert)
{
  // Arrange
  auto& uploader = GeoUploader();

  BeginFrame(Slot { 0 });
  const auto mesh_a = MakeValidTriangleMesh("MeshA", true);
  const auto mesh_b = MakeValidTriangleMesh("MeshB", true);
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
  const auto handle = uploader.GetOrAllocate(geometry_a);

  // Act / Assert
#if LOGURU_DEBUG_CHECKS
  EXPECT_DEATH(uploader.Update(handle, geometry_b), "");
#else
  uploader.Update(handle, geometry_b);
#endif
}

} // namespace
