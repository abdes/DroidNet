//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Renderer/ScenePrep/GeometryRef.h>

#include <Oxygen/Renderer/Test/Resources/GeometryUploaderTest.h>

namespace {

using oxygen::kInvalidShaderVisibleIndex;
using oxygen::frame::Slot;
using oxygen::renderer::testing::GeometryUploaderTest;

class GeometryUploaderResidencyTest : public GeometryUploaderTest { };

//! SRV indices remain invalid while uploads are pending, then become valid
//! after completion.
NOLINT_TEST_F(GeometryUploaderResidencyTest,
  IndicesAreInvalidUntilUploadCompletesThenPublish)
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

  // Act: first ensure schedules uploads, but indices must stay invalid.
  uploader.EnsureFrameResources();
  const auto indices_0 = uploader.GetShaderVisibleIndices(handle);

  // Assert
  EXPECT_EQ(indices_0.vertex_srv_index, kInvalidShaderVisibleIndex);
  EXPECT_EQ(indices_0.index_srv_index, kInvalidShaderVisibleIndex);

  // Act: advance frame/slot to allow upload coordinator retirement.
  BeginFrame(Slot { 1 });
  const auto indices_1 = uploader.GetShaderVisibleIndices(handle);

  // Assert: published indices are now valid.
  EXPECT_NE(indices_1.vertex_srv_index, kInvalidShaderVisibleIndex);
  EXPECT_NE(indices_1.index_srv_index, kInvalidShaderVisibleIndex);
}

} // namespace
