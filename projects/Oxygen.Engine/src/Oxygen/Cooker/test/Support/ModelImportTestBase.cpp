//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ModelImportTestBase.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

using data::pak::world::OrthographicCameraRecord;
using data::pak::world::PerspectiveCameraRecord;

auto ModelImportTestBase::LoadInspection(const std::filesystem::path& root)
  -> CookedContent
{
  return CookedContent::Open(content::cooked::RealCookerStorage(), root);
}

auto ModelImportTestBase::FindAssetOfType(const CookedContent& content,
  const AssetType type) -> std::optional<CookedAsset>
{
  const auto assets = content.Assets();
  const auto it = std::ranges::find(assets, type, &CookedAsset::type);
  if (it == assets.end()) {
    return std::nullopt;
  }
  return *it;
}

auto ModelImportTestBase::CountAssetsOfType(
  const CookedContent& content, const AssetType type) -> size_t
{
  const auto assets = content.Assets();
  return static_cast<size_t>(
    std::ranges::count(assets, type, &CookedAsset::type));
}

auto ModelImportTestBase::LoadSceneReadback(const ImportReport& report)
  -> SceneReadback
{
  const auto content = LoadInspection(report.cooked_root);

  const auto scene_entry = FindAssetOfType(content, AssetType::kScene);
  EXPECT_TRUE(scene_entry.has_value());
  if (!scene_entry.has_value()) {
    return {};
  }

  const auto scene = data::SceneAsset(scene_entry->key,
    oxygen::cooker::test::ReadBytes(
      content.DescriptorStored(scene_entry->key).file));
  const auto copy_of = [](const auto records) -> auto {
    return std::vector(records.begin(), records.end());
  };

  SceneReadback readback {};
  readback.nodes = copy_of(scene.GetNodes());
  readback.renderables = copy_of(scene.GetComponents<RenderableRecord>());
  readback.directional_lights
    = copy_of(scene.GetComponents<DirectionalLightRecord>());
  readback.point_lights = copy_of(scene.GetComponents<PointLightRecord>());
  readback.spot_lights = copy_of(scene.GetComponents<SpotLightRecord>());
  return readback;
}

auto ModelImportTestBase::ValidateSceneOutputs(
  const ImportReport& report, const ExpectedSceneOutputs& expected) -> void
{
  EXPECT_TRUE(report.success);

  const auto content = LoadInspection(report.cooked_root);

  if (expected.materials.has_value()) {
    EXPECT_EQ(CountAssetsOfType(content, AssetType::kMaterial),
      expected.materials.value());
  }
  if (expected.geometry.has_value()) {
    EXPECT_EQ(CountAssetsOfType(content, AssetType::kGeometry),
      expected.geometry.value());
  }
  if (expected.scenes.has_value()) {
    EXPECT_EQ(
      CountAssetsOfType(content, AssetType::kScene), expected.scenes.value());
  }

  const auto scene_entry = FindAssetOfType(content, AssetType::kScene);
  ASSERT_HAS_VALUE(scene_entry);

  const auto scene = data::SceneAsset(scene_entry->key,
    oxygen::cooker::test::ReadBytes(
      content.DescriptorStored(scene_entry->key).file));
  if (expected.nodes_min.has_value()) {
    EXPECT_GE(scene.GetNodes().size(), expected.nodes_min.value());
  }

  const auto renderables = scene.GetComponents<RenderableRecord>();
  if (expected.geometry.has_value()) {
    EXPECT_EQ(renderables.size(), expected.geometry.value());
  } else {
    EXPECT_GT(renderables.size(), 0U);
  }

  EXPECT_TRUE(scene.GetComponents<PerspectiveCameraRecord>().empty());
  EXPECT_TRUE(scene.GetComponents<OrthographicCameraRecord>().empty());
  EXPECT_TRUE(scene.GetComponents<DirectionalLightRecord>().empty());
  EXPECT_TRUE(scene.GetComponents<PointLightRecord>().empty());
  EXPECT_TRUE(scene.GetComponents<SpotLightRecord>().empty());

  const auto& textures = content.ResourceTable(data::ResourceKind::kTexture);
  const auto texture_count = static_cast<size_t>(textures.RowCount());
  const auto has_textures_table = !textures.RowsStored().file.empty();
  const auto has_textures_data = !textures.DataStored().file.empty();

  if (expected.texture_files.has_value()) {
    EXPECT_EQ(texture_count, expected.texture_files.value());
    if (expected.texture_files.value() > 0U) {
      EXPECT_TRUE(has_textures_table);
      EXPECT_TRUE(has_textures_data);
    } else {
      EXPECT_FALSE(has_textures_table);
      EXPECT_FALSE(has_textures_data);
    }
  } else {
    EXPECT_GT(texture_count, 0U);
    EXPECT_TRUE(has_textures_table);
    EXPECT_TRUE(has_textures_data);
  }
}

} // namespace oxygen::content::import::test
