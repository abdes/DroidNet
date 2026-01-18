//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/AssetImporter.h>
#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/Import/Naming.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

#include "FbxImporterTest.h"

namespace {

using oxygen::content::LooseCookedInspection;
using oxygen::content::import::AssetImporter;
using oxygen::content::import::ImportContentFlags;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::LooseCookedLayout;
using oxygen::content::import::NormalizeNamingStrategy;
using oxygen::data::AssetType;
using oxygen::data::loose_cooked::v1::FileKind;
using oxygen::data::pak::RenderableRecord;
using oxygen::data::pak::SceneAssetDesc;
using oxygen::data::pak::SceneComponentTableDesc;
using oxygen::data::pak::TextureResourceDesc;
using oxygen::serio::FileStream;
using oxygen::serio::Reader;

using oxygen::content::test::FbxImporterTest;

[[nodiscard]] auto FindAssetOfType(const LooseCookedInspection& inspection,
  const AssetType type) -> std::optional<LooseCookedInspection::AssetEntry>
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

[[nodiscard]] auto CountAssetsOfType(
  const LooseCookedInspection& inspection, const AssetType type) -> size_t
{
  const auto assets = inspection.Assets();
  return static_cast<size_t>(std::count_if(assets.begin(), assets.end(),
    [type](const LooseCookedInspection::AssetEntry& entry) {
      return entry.asset_type == static_cast<uint8_t>(type);
    }));
}

//! Full import validates supported FBX content is emitted.
/*!
 Uses the real FBX backend to import a full scene (Sponza) and verifies
 that all supported content types are produced and parseable.
*/
NOLINT_TEST_F(FbxImporterTest, RealBackend_ImportsFullSponzaScene)
{
  // Arrange
  const auto source_path = std::filesystem::path(
    "F:\\projects\\main_sponza\\NewSponza_Main_Zup_003.fbx");
  if (!std::filesystem::exists(source_path)) {
    GTEST_SKIP() << "Missing test asset: " << source_path.string();
  }

  const auto temp_dir = MakeTempDir("fbx_importer_full_sponza");

  AssetImporter importer;
  ImportRequest request {
    .source_path = source_path,
    .cooked_root = temp_dir / "cooked",
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content = ImportContentFlags::kAll;

  using std::chrono::duration_cast;
  using std::chrono::milliseconds;
  using std::chrono::steady_clock;

  // Act
  const auto import_start = steady_clock::now();
  const auto report = importer.ImportToLooseCooked(request);
  const auto import_end = steady_clock::now();
  const auto import_ms
    = duration_cast<milliseconds>(import_end - import_start).count();
  GTEST_LOG_(INFO) << "Sync FBX import duration: " << import_ms << " ms";

  // Assert
  GTEST_LOG_(INFO) << "Cooked root: " << report.cooked_root.string();
  EXPECT_TRUE(report.success);
  EXPECT_GT(report.materials_written, 0u);
  EXPECT_GT(report.geometry_written, 0u);
  EXPECT_GT(report.scenes_written, 0u);

  LooseCookedInspection inspection;
  inspection.LoadFromRoot(report.cooked_root);

  EXPECT_GT(CountAssetsOfType(inspection, AssetType::kMaterial), 0u);
  EXPECT_GT(CountAssetsOfType(inspection, AssetType::kGeometry), 0u);
  EXPECT_GT(CountAssetsOfType(inspection, AssetType::kScene), 0u);

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
  EXPECT_GT(scene_desc.nodes.count, 0u);

  ASSERT_GT(scene_desc.component_table_count, 0u);
  ASSERT_NE(scene_desc.component_table_directory_offset, 0u);

  auto seek_dir = scene_reader.Seek(base_pos
    + static_cast<size_t>(scene_desc.component_table_directory_offset));
  ASSERT_TRUE(seek_dir);

  std::optional<SceneComponentTableDesc> renderables_entry;
  for (uint32_t i = 0; i < scene_desc.component_table_count; ++i) {
    SceneComponentTableDesc entry {};
    auto entry_result = scene_reader.ReadBlobInto(
      std::as_writable_bytes(std::span<SceneComponentTableDesc, 1>(&entry, 1)));
    ASSERT_TRUE(entry_result);

    if (static_cast<oxygen::data::ComponentType>(entry.component_type)
      == oxygen::data::ComponentType::kRenderable) {
      renderables_entry = entry;
    }
  }

  ASSERT_TRUE(renderables_entry.has_value());
  EXPECT_EQ(renderables_entry->table.entry_size, sizeof(RenderableRecord));
  EXPECT_GT(renderables_entry->table.count, 0u);

  const auto textures_table_path = report.cooked_root
    / std::filesystem::path(request.loose_cooked_layout.TexturesTableRelPath());
  const auto textures_data_path = report.cooked_root
    / std::filesystem::path(request.loose_cooked_layout.TexturesDataRelPath());

  ASSERT_TRUE(std::filesystem::exists(textures_table_path));
  ASSERT_TRUE(std::filesystem::exists(textures_data_path));

  const auto table_size = std::filesystem::file_size(textures_table_path);
  ASSERT_EQ(table_size % sizeof(TextureResourceDesc), 0u);

  const auto texture_count
    = static_cast<size_t>(table_size / sizeof(TextureResourceDesc));
  EXPECT_GT(texture_count, 0u);

  const auto files = inspection.Files();
  const auto has_textures_table = std::any_of(files.begin(), files.end(),
    [](const LooseCookedInspection::FileEntry& entry) {
      return entry.kind == FileKind::kTexturesTable;
    });
  const auto has_textures_data = std::any_of(files.begin(), files.end(),
    [](const LooseCookedInspection::FileEntry& entry) {
      return entry.kind == FileKind::kTexturesData;
    });
  EXPECT_TRUE(has_textures_table);
  EXPECT_TRUE(has_textures_data);
}

} // namespace
