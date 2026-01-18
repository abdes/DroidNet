//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <latch>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/Async/AsyncImportService.h>
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

namespace {

using oxygen::content::LooseCookedInspection;
using oxygen::content::import::AsyncImportService;
using oxygen::content::import::ImportContentFlags;
using oxygen::content::import::ImportJobId;
using oxygen::content::import::ImportReport;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::LooseCookedLayout;
using oxygen::content::import::NormalizeNamingStrategy;
using oxygen::data::AssetType;
using oxygen::data::ComponentType;
using oxygen::data::loose_cooked::v1::FileKind;
using oxygen::data::pak::RenderableRecord;
using oxygen::data::pak::SceneAssetDesc;
using oxygen::data::pak::SceneComponentTableDesc;
using oxygen::data::pak::TextureResourceDesc;
using oxygen::serio::FileStream;
using oxygen::serio::Reader;
using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::steady_clock;

class AsyncFbxImporterFullTest : public ::testing::Test {
protected:
  [[nodiscard]] static auto MakeTempDir(std::string_view suffix)
    -> std::filesystem::path
  {
    const auto root
      = std::filesystem::temp_directory_path() / "oxgn-cntt-tests";
    const auto out_dir = root / std::filesystem::path(std::string(suffix));

    std::error_code ec;
    std::filesystem::remove_all(out_dir, ec);
    std::filesystem::create_directories(out_dir);

    return out_dir;
  }
};

[[nodiscard]] auto TestModelsDirFromFile() -> std::filesystem::path
{
  auto path = std::filesystem::path(__FILE__).parent_path();
  path /= "..";
  path /= "Models";
  return path.lexically_normal();
}

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

[[nodiscard]] auto MakeMaxConcurrencyConfig() -> AsyncImportService::Config
{
  constexpr uint32_t kVirtualCores = 32U;
  const auto total_workers = kVirtualCores;

  auto fraction_workers = [&](const uint32_t percent) -> uint32_t {
    const auto count = (total_workers * percent) / 100U;
    return std::max(1U, count);
  };

  AsyncImportService::Config config {
    .thread_pool_size = total_workers,
    .max_in_flight_jobs = total_workers,
  };
  config.concurrency = {
    .texture = {
      .workers = fraction_workers(40),
      .queue_capacity = 64,
    },
    .buffer = {
      .workers = fraction_workers(20),
      .queue_capacity = 64,
    },
    .material = {
      .workers = fraction_workers(20),
      .queue_capacity = 64,
    },
    .geometry = {
      .workers = fraction_workers(20),
      .queue_capacity = 32,
    },
    .scene = {
      .workers = 1U,
      .queue_capacity = 8,
    },
  };
  return config;
}

//! Full async import validates supported FBX content is emitted.
/*!
 Uses the async FBX import job to process dino-a.fbx and verifies the cooked
 outputs contain the supported content types.

 Expectations derived from Python MCP analysis of the FBX source:
 - 1 mesh geometry
 - 7 materials
 - 89 scene nodes (Model entries)
 - 2 unique texture files referenced
*/
NOLINT_TEST_F(AsyncFbxImporterFullTest, AsyncBackend_ImportsFullDinoScene)
{
  // Arrange
  const auto models_dir = TestModelsDirFromFile();
  const auto source_path = models_dir / "dino-a.fbx";
  if (!std::filesystem::exists(source_path)) {
    GTEST_SKIP() << "Missing test asset: " << source_path.string();
  }

  const auto temp_dir = MakeTempDir("async_fbx_dino");
  ImportRequest request {
    .source_path = source_path,
    .cooked_root = temp_dir,
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content = ImportContentFlags::kAll;

  constexpr size_t kExpectedMaterials = 7u;
  constexpr size_t kExpectedGeometry = 1u;
  constexpr size_t kExpectedScenes = 1u;
  constexpr size_t kExpectedNodesMin = 89u;
  constexpr size_t kExpectedTextureFiles = 2u;

  AsyncImportService service(MakeMaxConcurrencyConfig());
  std::latch done(1);
  ImportReport report;
  ImportJobId finished_id = oxygen::content::import::kInvalidJobId;

  // Act
  const auto import_start = steady_clock::now();
  const auto job_id = service.SubmitImport(
    std::move(request), [&](ImportJobId id, const ImportReport& completed) {
      finished_id = id;
      report = completed;
      done.count_down();
    });

  ASSERT_NE(job_id, oxygen::content::import::kInvalidJobId);
  done.wait();
  const auto import_end = steady_clock::now();
  const auto import_ms
    = duration_cast<milliseconds>(import_end - import_start).count();
  LOG_F(INFO, "Async FBX import duration: {} ms", import_ms);

  // Assert
  EXPECT_EQ(finished_id, job_id);
  EXPECT_TRUE(report.success);
  EXPECT_EQ(report.materials_written, kExpectedMaterials);
  EXPECT_EQ(report.geometry_written, kExpectedGeometry);
  EXPECT_EQ(report.scenes_written, kExpectedScenes);

  LooseCookedInspection inspection;
  inspection.LoadFromRoot(report.cooked_root);

  EXPECT_EQ(
    CountAssetsOfType(inspection, AssetType::kMaterial), kExpectedMaterials);
  EXPECT_EQ(
    CountAssetsOfType(inspection, AssetType::kGeometry), kExpectedGeometry);
  EXPECT_EQ(CountAssetsOfType(inspection, AssetType::kScene), kExpectedScenes);

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
  EXPECT_GE(scene_desc.nodes.count, kExpectedNodesMin);

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
  EXPECT_EQ(renderables_entry->table.count, kExpectedGeometry);

  EXPECT_FALSE(has_perspective);
  EXPECT_FALSE(has_orthographic);
  EXPECT_FALSE(has_directional);
  EXPECT_FALSE(has_point);
  EXPECT_FALSE(has_spot);

  const auto textures_table_path = report.cooked_root
    / std::filesystem::path(LooseCookedLayout {}.TexturesTableRelPath());
  const auto textures_data_path = report.cooked_root
    / std::filesystem::path(LooseCookedLayout {}.TexturesDataRelPath());

  ASSERT_TRUE(std::filesystem::exists(textures_table_path));
  ASSERT_TRUE(std::filesystem::exists(textures_data_path));

  const auto table_size = std::filesystem::file_size(textures_table_path);
  ASSERT_EQ(table_size % sizeof(TextureResourceDesc), 0u);

  const auto texture_count
    = static_cast<size_t>(table_size / sizeof(TextureResourceDesc));
  EXPECT_EQ(texture_count, kExpectedTextureFiles);

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

  GTEST_LOG_(INFO) << "Cooked root: " << report.cooked_root.string();
}

//! Async import succeeds for Sponza when asset is available.
/*!
 Validates the async FBX importer can handle the external-texture Sponza
 dataset when the source file is present on disk.
*/
NOLINT_TEST_F(AsyncFbxImporterFullTest, AsyncBackend_ImportsSponza)
{
  // Arrange
  const auto source_path = std::filesystem::path(
    "F:\\projects\\main_sponza\\NewSponza_Main_Zup_003.fbx");
  if (!std::filesystem::exists(source_path)) {
    GTEST_SKIP() << "Missing test asset: " << source_path.string();
  }

  const auto temp_dir = MakeTempDir("async_fbx_sponza");
  ImportRequest request {
    .source_path = source_path,
    .cooked_root = temp_dir,
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content = ImportContentFlags::kAll;

  AsyncImportService service(MakeMaxConcurrencyConfig());
  std::latch done(1);
  ImportReport report;
  ImportJobId finished_id = oxygen::content::import::kInvalidJobId;

  // Act
  const auto import_start = steady_clock::now();
  const auto job_id = service.SubmitImport(
    std::move(request), [&](ImportJobId id, const ImportReport& completed) {
      finished_id = id;
      report = completed;
      done.count_down();
    });

  ASSERT_NE(job_id, oxygen::content::import::kInvalidJobId);
  done.wait();
  const auto import_end = steady_clock::now();
  const auto import_ms
    = duration_cast<milliseconds>(import_end - import_start).count();
  LOG_F(INFO, "Async FBX import duration: {} ms", import_ms);

  // Assert
  EXPECT_EQ(finished_id, job_id);
  EXPECT_TRUE(report.success);
  GTEST_LOG_(INFO) << "Cooked root: " << report.cooked_root.string();
}

} // namespace
