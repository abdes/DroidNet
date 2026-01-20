//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "AsyncImporterFullTestBase.h"

namespace oxygen::content::import::test {

auto AsyncImporterFullTestBase::LoadInspection(
  const std::filesystem::path& root) -> LooseCookedInspection
{
  LooseCookedInspection inspection;
  inspection.LoadFromRoot(root);
  return inspection;
}

auto AsyncImporterFullTestBase::FindAssetOfType(
  const LooseCookedInspection& inspection, const AssetType type)
  -> std::optional<LooseCookedInspection::AssetEntry>
{
  const auto assets = inspection.Assets();
  const auto it = std::find_if(assets.begin(), assets.end(),
    [type](const LooseCookedInspection::AssetEntry& entry) {
      return entry.asset_type == static_cast<uint8_t>(type);
    });
  if (it == assets.end()) {
    return std::nullopt;
  }
  return *it;
}

auto AsyncImporterFullTestBase::CountAssetsOfType(
  const LooseCookedInspection& inspection, const AssetType type) -> size_t
{
  const auto assets = inspection.Assets();
  return static_cast<size_t>(std::count_if(assets.begin(), assets.end(),
    [type](const LooseCookedInspection::AssetEntry& entry) {
      return entry.asset_type == static_cast<uint8_t>(type);
    }));
}

auto AsyncImporterFullTestBase::ValidateSceneOutputs(
  const ImportReport& report, const ExpectedSceneOutputs& expected) -> void
{
  EXPECT_TRUE(report.success);

  LooseCookedInspection inspection;
  inspection.LoadFromRoot(report.cooked_root);

  if (expected.materials.has_value()) {
    EXPECT_EQ(CountAssetsOfType(inspection, AssetType::kMaterial),
      expected.materials.value());
  }
  if (expected.geometry.has_value()) {
    EXPECT_EQ(CountAssetsOfType(inspection, AssetType::kGeometry),
      expected.geometry.value());
  }
  if (expected.scenes.has_value()) {
    EXPECT_EQ(CountAssetsOfType(inspection, AssetType::kScene),
      expected.scenes.value());
  }

  const auto scene_entry = FindAssetOfType(inspection, AssetType::kScene);
  ASSERT_TRUE(scene_entry.has_value());

  const auto scene_path = report.cooked_root
    / std::filesystem::path(scene_entry->descriptor_relpath);

  FileStream<> scene_stream(scene_path, std::ios::in);
  Reader<FileStream<>> scene_reader(scene_stream);
  auto packed = scene_reader.ScopedAlignment(1);

  const auto base_pos_res = scene_reader.Position();
  ASSERT_TRUE(base_pos_res);
  const auto base_pos = *base_pos_res;

  SceneAssetDesc scene_desc {};
  auto scene_desc_result = scene_reader.ReadBlobInto(
    std::as_writable_bytes(std::span<SceneAssetDesc, 1>(&scene_desc, 1)));
  ASSERT_TRUE(scene_desc_result);
  if (expected.nodes_min.has_value()) {
    EXPECT_GE(scene_desc.nodes.count,
      static_cast<uint32_t>(expected.nodes_min.value()));
  }

  ASSERT_GT(scene_desc.component_table_count, 0u);
  ASSERT_NE(scene_desc.component_table_directory_offset, 0u);

  auto seek_dir = scene_reader.Seek(base_pos
    + static_cast<size_t>(scene_desc.component_table_directory_offset));
  ASSERT_TRUE(seek_dir);

  std::optional<SceneComponentTableDesc> renderables_entry;
  bool has_perspective = false;
  bool has_orthographic = false;
  bool has_directional = false;
  bool has_point = false;
  bool has_spot = false;

  for (uint32_t i = 0; i < scene_desc.component_table_count; ++i) {
    SceneComponentTableDesc entry {};
    auto entry_result = scene_reader.ReadBlobInto(
      std::as_writable_bytes(std::span<SceneComponentTableDesc, 1>(&entry, 1)));
    ASSERT_TRUE(entry_result);

    const auto type = static_cast<ComponentType>(entry.component_type);
    if (type == ComponentType::kRenderable) {
      renderables_entry = entry;
    } else if (type == ComponentType::kPerspectiveCamera) {
      has_perspective = true;
    } else if (type == ComponentType::kOrthographicCamera) {
      has_orthographic = true;
    } else if (type == ComponentType::kDirectionalLight) {
      has_directional = true;
    } else if (type == ComponentType::kPointLight) {
      has_point = true;
    } else if (type == ComponentType::kSpotLight) {
      has_spot = true;
    }
  }

  ASSERT_TRUE(renderables_entry.has_value());
  EXPECT_EQ(renderables_entry->table.entry_size, sizeof(RenderableRecord));
  if (expected.geometry.has_value()) {
    EXPECT_EQ(renderables_entry->table.count,
      static_cast<uint32_t>(expected.geometry.value()));
  } else {
    EXPECT_GT(renderables_entry->table.count, 0u);
  }

  EXPECT_FALSE(has_perspective);
  EXPECT_FALSE(has_orthographic);
  EXPECT_FALSE(has_directional);
  EXPECT_FALSE(has_point);
  EXPECT_FALSE(has_spot);

  const auto textures_table_path = report.cooked_root
    / std::filesystem::path(LooseCookedLayout {}.TexturesTableRelPath());
  const auto textures_data_path = report.cooked_root
    / std::filesystem::path(LooseCookedLayout {}.TexturesDataRelPath());

  const auto table_exists = std::filesystem::exists(textures_table_path);
  const auto data_exists = std::filesystem::exists(textures_data_path);

  size_t texture_count = 0u;
  if (table_exists) {
    const auto table_size = std::filesystem::file_size(textures_table_path);
    ASSERT_EQ(table_size % sizeof(TextureResourceDesc), 0u);
    texture_count
      = static_cast<size_t>(table_size / sizeof(TextureResourceDesc));
  }

  if (expected.texture_files.has_value()) {
    EXPECT_EQ(texture_count, expected.texture_files.value());
    if (expected.texture_files.value() > 0u) {
      EXPECT_TRUE(table_exists);
      EXPECT_TRUE(data_exists);
    } else {
      EXPECT_FALSE(table_exists);
      EXPECT_FALSE(data_exists);
    }
  } else {
    EXPECT_GT(texture_count, 0u);
    EXPECT_TRUE(table_exists);
    EXPECT_TRUE(data_exists);
  }

  const auto files = inspection.Files();
  const auto has_textures_table = std::any_of(files.begin(), files.end(),
    [](const LooseCookedInspection::FileEntry& entry) {
      return entry.kind == FileKind::kTexturesTable;
    });
  const auto has_textures_data = std::any_of(files.begin(), files.end(),
    [](const LooseCookedInspection::FileEntry& entry) {
      return entry.kind == FileKind::kTexturesData;
    });

  if (expected.texture_files.has_value()) {
    if (expected.texture_files.value() > 0u) {
      EXPECT_TRUE(has_textures_table);
      EXPECT_TRUE(has_textures_data);
    } else {
      EXPECT_FALSE(has_textures_table);
      EXPECT_FALSE(has_textures_data);
    }
  } else {
    EXPECT_TRUE(has_textures_table);
    EXPECT_TRUE(has_textures_data);
  }
}

} // namespace oxygen::content::import::test
