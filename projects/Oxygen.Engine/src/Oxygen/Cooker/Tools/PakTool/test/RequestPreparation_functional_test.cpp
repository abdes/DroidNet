//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/PakTool/RequestPreparation.cpp

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "PakToolOptions.h"
#include "PakToolTestSupport.h"

#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Tools/PakTool/RequestPreparation.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/testing/CookedPak.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

using oxygen::content::cooked::RealCookerStorage;
using oxygen::content::pak::BuildMode;
using oxygen::content::pak::tool::PakToolCliOptions;
using oxygen::content::pak::tool::PreparePakToolRequest;
using oxygen::content::pak::tool::test::WriteGamePak;
using oxygen::cooker::test::WriteText;
using oxygen::cooker::testing::CookedPak;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::Materialize;
using oxygen::cooker::testing::SeedBytes;

constexpr auto kSourceKey = "01234567-89ab-7def-8123-456789abcdef";

class PakToolRequestPreparationTest : public oxygen::testing::TempDirTest {
protected:
  [[nodiscard]] static auto MakeOptions() -> PakToolCliOptions
  {
    auto options = PakToolCliOptions {};
    options.request.output_pak = "C:/unused/out.pak";
    options.request.catalog_output = "C:/unused/out.pakcatalog.json";
    options.request.content_version = 42;
    options.request.source_key = kSourceKey;
    return options;
  }
};

NOLINT_TEST_F(PakToolRequestPreparationTest,
  PrepareFullBuildRequestPreservesSourceOrderAndStagesOutputs)
{
  auto options = MakeOptions();

  const auto loose_a = TempDir() / "cook" / "base";
  const auto loose_b = TempDir() / "cook" / "dlc";
  const auto pak_source = TempDir() / "cook" / "base.pak";
  std::filesystem::create_directories(loose_a);
  std::filesystem::create_directories(loose_b);
  WriteText(pak_source, "pak");

  options.request.sources = {
    oxygen::data::CookedSource {
      .kind = oxygen::data::CookedSourceKind::kLooseCooked,
      .path = loose_a,
    },
    oxygen::data::CookedSource {
      .kind = oxygen::data::CookedSourceKind::kPak,
      .path = pak_source,
    },
    oxygen::data::CookedSource {
      .kind = oxygen::data::CookedSourceKind::kLooseCooked,
      .path = loose_b,
    },
  };
  options.request.output_pak = TempDir() / "release" / "game.pak";
  options.request.catalog_output
    = TempDir() / "release" / "game.pakcatalog.json";
  options.build.manifest_output = TempDir() / "release" / "game.manifest.json";
  options.output.diagnostics_file = TempDir() / "release" / "game.report.json";
  options.request.deterministic = false;
  options.request.compute_crc32 = false;
  options.request.fail_on_warnings = true;

  const auto& storage = RealCookerStorage();
  const auto prepared
    = PreparePakToolRequest(BuildMode::kFull, options, storage);

  ASSERT_HAS_VALUE(prepared)
    << prepared.error().error_code << ": " << prepared.error().error_message;
  ASSERT_EQ(prepared->build_request.sources.size(), 3U);
  EXPECT_EQ(prepared->build_request.sources.at(0).kind,
    oxygen::data::CookedSourceKind::kLooseCooked);
  EXPECT_EQ(prepared->build_request.sources.at(1).kind,
    oxygen::data::CookedSourceKind::kPak);
  EXPECT_EQ(prepared->build_request.sources.at(2).kind,
    oxygen::data::CookedSourceKind::kLooseCooked);
  EXPECT_EQ(prepared->build_request.output_pak_path,
    prepared->publication_plan.pak.staged_path);
  ASSERT_HAS_VALUE(prepared->publication_plan.manifest);
  EXPECT_EQ(prepared->build_request.output_manifest_path,
    prepared->publication_plan.manifest->staged_path);
  EXPECT_TRUE(prepared->build_request.options.emit_manifest_in_full);
  EXPECT_FALSE(prepared->build_request.options.deterministic);
  EXPECT_FALSE(prepared->build_request.options.compute_crc32);
  EXPECT_TRUE(prepared->build_request.options.fail_on_warnings);
  // Preparation only plans: the outputs' folders are created when the
  // artifacts are published.
  EXPECT_FALSE(
    std::filesystem::exists(options.request.output_pak.parent_path()));
}

NOLINT_TEST_F(PakToolRequestPreparationTest, RejectsInvalidSourceKeyText)
{
  auto options = MakeOptions();
  options.request.source_key = "not-a-uuid";
  options.request.output_pak = TempDir() / "release" / "game.pak";
  options.request.catalog_output
    = TempDir() / "release" / "game.pakcatalog.json";

  const auto& storage = RealCookerStorage();
  const auto prepared
    = PreparePakToolRequest(BuildMode::kFull, options, storage);

  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(prepared.error().error_code, "paktool.prepare.invalid_source_key");
}

//! Source mounts for script sealing come from `--mount` and the project's
//! `.mounts.json`, as ImportTool resolves them.
NOLINT_TEST_F(PakToolRequestPreparationTest, ResolvesSourceMountsForSealing)
{
  const auto project = TempDir() / "project";
  const auto content = project / "Content";
  const auto media = TempDir() / "media";
  std::filesystem::create_directories(content);
  std::filesystem::create_directories(media);
  WriteText(project / ".mounts.json",
    oxygen::cooker::test::MountsText(
      R"({ "root": true, "mounts": { "Media": ")")
      + media.generic_string() + R"(" } })");
  auto options = MakeOptions();
  options.request.output_pak = TempDir() / "release" / "game.pak";
  options.request.catalog_output
    = TempDir() / "release" / "game.pakcatalog.json";
  options.request.project_root = project;
  options.request.source_mounts = { "Content=" + content.string() };

  const auto& storage = RealCookerStorage();
  const auto prepared
    = PreparePakToolRequest(BuildMode::kFull, options, storage);

  ASSERT_HAS_VALUE(prepared)
    << prepared.error().error_code << ": " << prepared.error().error_message;
  const auto& mounts = prepared->request_snapshot.source_mounts;
  ASSERT_EQ(mounts.size(), 2U);
  ASSERT_TRUE(mounts.contains("Content"));
  ASSERT_TRUE(mounts.contains("Media"));
  EXPECT_TRUE(std::filesystem::equivalent(mounts.at("Content"), content));
  EXPECT_TRUE(std::filesystem::equivalent(mounts.at("Media"), media));
}

NOLINT_TEST_F(PakToolRequestPreparationTest, NoMountOptionsMeanNoSourceMounts)
{
  auto options = MakeOptions();
  options.request.output_pak = TempDir() / "release" / "game.pak";
  options.request.catalog_output
    = TempDir() / "release" / "game.pakcatalog.json";

  const auto& storage = RealCookerStorage();
  const auto prepared
    = PreparePakToolRequest(BuildMode::kFull, options, storage);

  ASSERT_HAS_VALUE(prepared)
    << prepared.error().error_code << ": " << prepared.error().error_message;
  EXPECT_TRUE(prepared->request_snapshot.source_mounts.empty());
}

NOLINT_TEST_F(PakToolRequestPreparationTest, RejectsMalformedMountOption)
{
  auto options = MakeOptions();
  options.request.output_pak = TempDir() / "release" / "game.pak";
  options.request.catalog_output
    = TempDir() / "release" / "game.pakcatalog.json";
  options.request.source_mounts = { "Content" };

  const auto& storage = RealCookerStorage();
  const auto prepared
    = PreparePakToolRequest(BuildMode::kFull, options, storage);

  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(
    prepared.error().error_code, "paktool.prepare.source_mount_invalid");
}

//! An invalid mount table names each mount diagnostic it failed with.
NOLINT_TEST_F(PakToolRequestPreparationTest, RejectsInvalidSourceMounts)
{
  const auto project = TempDir() / "project";
  std::filesystem::create_directories(project / "Content");
  WriteText(project / ".mounts.json",
    oxygen::cooker::test::MountsText(R"({ "root": true })"));
  auto options = MakeOptions();
  options.request.output_pak = TempDir() / "release" / "game.pak";
  options.request.catalog_output
    = TempDir() / "release" / "game.pakcatalog.json";
  options.request.project_root = project;
  options.request.source_mounts
    = { "Bad Name=" + (project / "Content").string() };

  const auto& storage = RealCookerStorage();
  const auto prepared
    = PreparePakToolRequest(BuildMode::kFull, options, storage);

  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(
    prepared.error().error_code, "paktool.prepare.source_mounts_invalid");
  EXPECT_NE(prepared.error().error_message.find("mount.name_invalid"),
    std::string::npos);
}

NOLINT_TEST_F(PakToolRequestPreparationTest, RejectsMissingSourcePath)
{
  auto options = MakeOptions();
  options.request.sources = {
    oxygen::data::CookedSource {
      .kind = oxygen::data::CookedSourceKind::kLooseCooked,
      .path = TempDir() / "missing_source",
    },
  };
  options.request.output_pak = TempDir() / "release" / "game.pak";
  options.request.catalog_output
    = TempDir() / "release" / "game.pakcatalog.json";

  const auto& storage = RealCookerStorage();
  const auto prepared
    = PreparePakToolRequest(BuildMode::kFull, options, storage);

  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(prepared.error().error_code, "paktool.prepare.source_missing");
  EXPECT_EQ(prepared.error().path, TempDir() / "missing_source");
}

NOLINT_TEST_F(PakToolRequestPreparationTest, RejectsInvalidBasePakPayload)
{
  auto options = MakeOptions();
  const auto base_catalog = TempDir() / "catalogs" / "base.pakcatalog.json";
  WriteText(base_catalog, "{ invalid json }");

  options.request.output_pak = TempDir() / "release" / "game_patch.pak";
  options.request.catalog_output
    = TempDir() / "release" / "game_patch.pakcatalog.json";
  options.patch.manifest_output
    = TempDir() / "release" / "game_patch.manifest.json";
  options.patch.base_paks = { base_catalog };

  const auto& storage = RealCookerStorage();
  const auto prepared
    = PreparePakToolRequest(BuildMode::kPatch, options, storage);

  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(prepared.error().error_code, "paktool.prepare.base_pak_invalid");
  EXPECT_EQ(prepared.error().path, base_catalog);
}

NOLINT_TEST_F(PakToolRequestPreparationTest, RejectsPatchModeWithoutBasePaks)
{
  auto options = MakeOptions();
  options.request.output_pak = TempDir() / "release" / "game_patch.pak";
  options.request.catalog_output
    = TempDir() / "release" / "game_patch.pakcatalog.json";
  options.patch.manifest_output
    = TempDir() / "release" / "game_patch.manifest.json";

  const auto& storage = RealCookerStorage();
  const auto prepared
    = PreparePakToolRequest(BuildMode::kPatch, options, storage);

  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(prepared.error().error_code, "paktool.prepare.base_pak_required");
}

NOLINT_TEST_F(
  PakToolRequestPreparationTest, PreparePatchRequestLoadsOrderedBasePaks)
{
  auto options = MakeOptions();
  const auto base_a = TempDir() / "bases" / "base_a.pak";
  const auto base_b = TempDir() / "bases" / "base_b.pak";
  WriteGamePak(base_a, "BaseA", 0x70U);
  WriteGamePak(base_b, "BaseB", 0x71U);

  options.request.output_pak = TempDir() / "release" / "game_patch.pak";
  options.request.catalog_output
    = TempDir() / "release" / "game_patch.pakcatalog.json";
  options.patch.manifest_output
    = TempDir() / "release" / "game_patch.manifest.json";
  options.patch.base_paks = { base_a, base_b };

  const auto& storage = RealCookerStorage();
  const auto prepared
    = PreparePakToolRequest(BuildMode::kPatch, options, storage);

  ASSERT_HAS_VALUE(prepared)
    << prepared.error().error_code << ": " << prepared.error().error_message;
  ASSERT_EQ(prepared->build_request.base_catalogs.size(), 2U);
  ASSERT_EQ(prepared->request_snapshot.base_pak_paths.size(), 2U);
  EXPECT_EQ(prepared->request_snapshot.base_pak_paths.at(0), base_a);
  EXPECT_EQ(prepared->request_snapshot.base_pak_paths.at(1), base_b);
}

//! The textures a dependency pak holds are offered to references by key.
NOLINT_TEST_F(
  PakToolRequestPreparationTest, PrepareRequestCollectsDependencyTextureKeys)
{
  // Arrange
  auto options = MakeOptions();
  auto engine = LooseCookedRoot { "Engine" };
  const auto grid_row = engine.AddTexture(SeedBytes(1U));
  const auto grid = engine.DescribeTexture("Textures/Grid.otex", grid_row);
  // A pak holds at least one asset: a material that uses the texture.
  auto grid_material = oxygen::cooker::testing::MaterialBuilder { "Grid" };
  grid_material.Texture(
    &oxygen::data::pak::render::MaterialAssetDesc::base_color_texture,
    grid_row);
  engine.Material("Materials/Grid.omat", grid_material);
  const auto engine_pak = TempDir() / "engine.pak";
  Materialize(CookedPak { std::move(engine) }.Encode(), engine_pak);
  options.request.dependency_paks = { engine_pak };
  const auto& storage = RealCookerStorage();

  // Act
  const auto prepared
    = PreparePakToolRequest(BuildMode::kFull, options, storage);

  // Assert
  ASSERT_HAS_VALUE(prepared)
    << prepared.error().error_code << ": " << prepared.error().error_message;
  ASSERT_EQ(prepared->build_request.dependencies.size(), 1U);
  const auto& dependency = prepared->build_request.dependencies.front();
  EXPECT_EQ(dependency.catalog.mount, "Engine");
  EXPECT_TRUE(dependency.texture_keys.contains(
    oxygen::data::TextureKeyFromVirtualPath(grid)));
}

NOLINT_TEST_F(PakToolRequestPreparationTest, RejectsConflictingToolPaths)
{
  auto options = MakeOptions();
  const auto shared_output = TempDir() / "release" / "game.pak";
  options.request.output_pak = shared_output;
  options.request.catalog_output = shared_output;

  const auto& storage = RealCookerStorage();
  const auto prepared
    = PreparePakToolRequest(BuildMode::kFull, options, storage);

  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(prepared.error().error_code, "paktool.prepare.path_conflict");
  EXPECT_EQ(prepared.error().path, shared_output);
}

//! An output whose parent is a file is refused before anything is built; a
//! missing parent is created when the artifact is published.
NOLINT_TEST_F(PakToolRequestPreparationTest, RejectsOutputParentThatIsAFile)
{
  auto options = MakeOptions();
  const auto blocked = TempDir() / "blocked";
  WriteText(blocked, "a file, not a folder");
  options.request.output_pak = blocked / "game.pak";
  options.request.catalog_output
    = TempDir() / "release" / "game.pakcatalog.json";

  const auto prepared
    = PreparePakToolRequest(BuildMode::kFull, options, RealCookerStorage());

  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(prepared.error().error_code,
    "paktool.prepare.output_pak_parent.parent_not_directory");
  EXPECT_EQ(prepared.error().path, blocked);
}

} // namespace
