//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/ScenePrep/GeometryRef.h>
#include <Oxygen/Vortex/Test/Fixtures/GeometryUploaderTest.h>

namespace {

using oxygen::frame::Slot;
using oxygen::vortex::testing::GeometryUploaderTest;
using oxygen::vortex::testing::MakeGeometryAssetKey;

class GeometryUploaderUpdateTest : public GeometryUploaderTest { };

//! Update marks geometry dirty and re-schedules uploads on next ensure.
NOLINT_TEST_F(GeometryUploaderUpdateTest,
  UpdateSameEpochMarksDirtyAndSchedulesUploadNextEnsure)
{
  // Arrange
  auto& uploader = GeoUploader();

  BeginFrame(Slot { 0 });
  const auto mesh_v1 = MakeValidTriangleMesh("Tri", true);
  const auto asset_key = MakeGeometryAssetKey("update_dirty_reschedules");
  const oxygen::vortex::sceneprep::GeometryRef geometry_v1 {
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
  const oxygen::vortex::sceneprep::GeometryRef geometry_v2 {
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

//! Same-size content replacement keeps handles/SRVs but changes resident
//! content.
NOLINT_TEST_F(
  GeometryUploaderUpdateTest, ContentRevisionChangesOnUpdateAndHotReload)
{
  auto& uploader = GeoUploader();
  BeginFrame(Slot { 0 });
  auto geometry = oxygen::vortex::sceneprep::GeometryRef {
    .asset_key = MakeGeometryAssetKey("content_revision"),
    .mesh = MakeValidTriangleMesh("Original", true),
  };
  const auto handle = uploader.GetOrAllocate(geometry);
  EXPECT_EQ(uploader.GetShaderVisibleIndices(handle).content_revision, 0U);
  BeginFrame(Slot { 1 });
  const auto original = uploader.GetShaderVisibleIndices(handle);
  ASSERT_NE(original.content_revision, 0U);
  ASSERT_TRUE(original.vertex_srv_index.IsValid());
  ASSERT_TRUE(original.index_srv_index.IsValid());
  for (const bool explicit_update : { true, false }) {
    SCOPED_TRACE(explicit_update);
    const auto before = uploader.GetShaderVisibleIndices(handle);
    // Immutable meshes have equal topology/counts but represent new contents.
    const auto vertices = geometry.mesh->Vertices();
    auto moved
      = std::vector<oxygen::data::Vertex>(vertices.begin(), vertices.end());
    moved[0].position.x += 0.25F;
    geometry.mesh
      = oxygen::data::MeshBuilder(0, "Reloaded")
          .WithVertices(moved)
          .WithIndices(std::vector<std::uint32_t> { 0U, 1U, 2U })
          .BeginSubMesh("default", oxygen::data::MaterialAsset::CreateDefault())
          .WithMeshView({ .first_index = 0U,
            .index_count = 3U,
            .first_vertex = 0U,
            .vertex_count = 3U })
          .EndSubMesh()
          .Build();
    if (explicit_update) {
      uploader.Update(handle, geometry);
    } else {
      EXPECT_EQ(uploader.GetOrAllocate(geometry), handle);
    }
    EXPECT_EQ(uploader.GetShaderVisibleIndices(handle).content_revision, 0U);
    BeginFrame(Slot { explicit_update ? 0U : 1U });
    const auto after = uploader.GetShaderVisibleIndices(handle);
    EXPECT_EQ(uploader.GetOrAllocate(geometry), handle);
    EXPECT_EQ(after.vertex_srv_index, original.vertex_srv_index);
    EXPECT_EQ(after.index_srv_index, original.index_srv_index);
    EXPECT_GT(after.content_revision, before.content_revision);
    EXPECT_EQ(uploader.GetShaderVisibleIndices(handle).content_revision,
      after.content_revision);
  }
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
  const auto asset_key_a = MakeGeometryAssetKey("update_debug_assert_a");
  const auto asset_key_b = MakeGeometryAssetKey("update_debug_assert_b");
  const oxygen::vortex::sceneprep::GeometryRef geometry_a {
    .asset_key = asset_key_a,
    .lod_index = 0U,
    .mesh = mesh_a,
  };
  const oxygen::vortex::sceneprep::GeometryRef geometry_b {
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

//! Stale handles must be rejected by Update after eviction/reload generation
//! changes.
NOLINT_TEST_F(GeometryUploaderUpdateTest, UpdateStaleHandleIsRejected)
{
  // Arrange
  auto& uploader = GeoUploader();

  BeginFrame(Slot { 0 });
  const auto mesh_v1 = MakeValidTriangleMesh("TriV1", true);
  const auto asset_key = MakeGeometryAssetKey("update_stale_handle_rejected");
  const oxygen::vortex::sceneprep::GeometryRef geometry_v1 {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh_v1,
  };
  const auto stale_handle = uploader.GetOrAllocate(geometry_v1);
  uploader.EnsureFrameResources();

  Loader().EmitGeometryAssetEviction(
    asset_key, oxygen::content::EvictionReason::kRefCountZero);
  BeginFrame(Slot { 1 });

  const auto mesh_v2 = MakeValidTriangleMesh("TriV2", true);
  const oxygen::vortex::sceneprep::GeometryRef geometry_v2 {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh_v2,
  };
  const auto current_handle = uploader.GetOrAllocate(geometry_v2);
  ASSERT_NE(current_handle, stale_handle);

  // Act: stale handle update should be ignored; current handle remains valid.
  uploader.Update(stale_handle, geometry_v2);
  uploader.EnsureFrameResources();

  // Assert
  EXPECT_FALSE(uploader.IsHandleValid(stale_handle));
  EXPECT_TRUE(uploader.IsHandleValid(current_handle));
}

} // namespace
