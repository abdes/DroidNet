//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <unordered_set>
#include <vector>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Renderer/ScenePrep/GeometryRef.h>
#include <Oxygen/Renderer/ScenePrep/Handles.h>

#include <Oxygen/Renderer/Test/Resources/GeometryUploaderTest.h>

namespace {

using oxygen::frame::Slot;
using oxygen::renderer::testing::GeometryUploaderTest;

class GeometryUploaderEdgeTest : public GeometryUploaderTest { };

//! Many unique identities should grow internal tables without invalidating
//! previously returned handles.
NOLINT_TEST_F(
  GeometryUploaderEdgeTest, ManyGeometriesGrowsInternalTablesHandlesRemainValid)
{
  // Arrange
  auto& uploader = GeoUploader();
  BeginFrame(Slot { 0 });

  const auto mesh = MakeValidTriangleMesh("Tri", true);

  constexpr std::size_t kCount = 256;
  std::vector<oxygen::engine::sceneprep::GeometryHandle> handles;
  handles.reserve(kCount);

  // Act
  for (std::size_t i = 0; i < kCount; ++i) {
    oxygen::data::AssetKey key {};
    key.guid[0] = static_cast<std::uint8_t>(i & 0xFFU);
    key.guid[1] = static_cast<std::uint8_t>((i >> 8) & 0xFFU);

    const oxygen::engine::sceneprep::GeometryRef geometry {
      .asset_key = key,
      .lod_index = 0U,
      .mesh = mesh,
    };

    handles.push_back(uploader.GetOrAllocate(geometry));
  }

  // Assert
  std::unordered_set<std::uint64_t> unique;
  unique.reserve(kCount);

  for (const auto h : handles) {
    EXPECT_NE(h, oxygen::engine::sceneprep::kInvalidGeometryHandle);
    EXPECT_TRUE(uploader.IsHandleValid(h));
    unique.insert(h.get());
  }

  EXPECT_EQ(unique.size(), kCount);
}

//! Repeated EnsureFrameResources calls must not continuously append tickets.
NOLINT_TEST_F(GeometryUploaderEdgeTest, RepeatedEnsureNoUnboundedTicketGrowth)
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
  uploader.EnsureFrameResources();
  const auto pending_2 = uploader.GetPendingUploadCount();

  // Assert
  EXPECT_GT(pending_0, 0U);
  EXPECT_EQ(pending_1, pending_0);
  EXPECT_EQ(pending_2, pending_0);
}

} // namespace
