//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Test/Resources/GeometryUploaderTest.h>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Renderer/ScenePrep/GeometryRef.h>

namespace {

using oxygen::frame::Slot;
using oxygen::renderer::testing::GeometryUploaderTest;

class GeometryUploaderLifecycleTest : public GeometryUploaderTest { };

//! EnsureFrameResources is idempotent within the same frame.
NOLINT_TEST_F(
  GeometryUploaderLifecycleTest, EnsureFrameResourcesIsIdempotentWithinFrame)
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
  const auto pending_0 = uploader.GetPendingUploadCount();
  uploader.EnsureFrameResources();
  const auto pending_1 = uploader.GetPendingUploadCount();

  // Assert
  EXPECT_EQ(pending_1, pending_0);
}

//! GetShaderVisibleIndices auto-calls EnsureFrameResources once per frame.
NOLINT_TEST_F(
  GeometryUploaderLifecycleTest, GetShaderVisibleIndicesAutoEnsuresOncePerFrame)
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
  const auto handle = uploader.GetOrAllocate(geometry);

  // Act
  (void)uploader.GetShaderVisibleIndices(handle);
  const auto pending_0 = uploader.GetPendingUploadCount();
  (void)uploader.GetShaderVisibleIndices(handle);
  const auto pending_1 = uploader.GetPendingUploadCount();

  // Assert
  EXPECT_GT(pending_0, 0U);
  EXPECT_EQ(pending_1, pending_0);
}

//! Referencing stable geometry each frame must not schedule reuploads.
NOLINT_TEST_F(
  GeometryUploaderLifecycleTest, StableMeshDoesNotReuploadEveryFrame)
{
  // Arrange
  auto& uploader = GeoUploader();

  const auto mesh = MakeValidTriangleMesh("Tri", true);
  const oxygen::data::AssetKey asset_key {
    .guid = oxygen::data::GenerateAssetGuid(),
  };
  const oxygen::engine::sceneprep::GeometryRef geometry {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh,
  };

  BeginFrame(Slot { 0 });
  const auto handle = uploader.GetOrAllocate(geometry);

  // Act: first ensure schedules the initial uploads.
  uploader.EnsureFrameResources();
  ASSERT_GT(uploader.GetPendingUploadCount(), 0U);

  // Retire tickets at the next frame boundary.
  BeginFrame(Slot { 1 });
  EXPECT_EQ(uploader.GetPendingUploadCount(), 0U);

  // Act: reference the same mesh again in a later frame.
  (void)uploader.GetOrAllocate(geometry);
  uploader.EnsureFrameResources();

  // Assert: no new upload work should be scheduled.
  EXPECT_EQ(uploader.GetPendingUploadCount(), 0U);

  // Sanity: handle remains stable for the same identity.
  const auto handle_2 = uploader.GetOrAllocate(geometry);
  EXPECT_EQ(handle_2, handle);
}

} // namespace
