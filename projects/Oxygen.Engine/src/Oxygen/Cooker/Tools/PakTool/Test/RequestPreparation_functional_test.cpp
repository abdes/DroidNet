//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/PakTool/RequestPreparation.cpp

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <system_error>
#include <utility>

#include "PakToolOptions.h"

#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakBuilder.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Cooker/Tools/PakTool/RequestPreparation.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::pak::BuildMode;
using oxygen::content::pak::tool::IRequestPreparationFileSystem;
using oxygen::content::pak::tool::PakToolCliOptions;
using oxygen::content::pak::tool::PreparePakToolRequest;
using oxygen::content::pak::tool::RealRequestPreparationFileSystem;
using oxygen::cooker::test::WriteText;

constexpr auto kSourceKey = "01234567-89ab-7def-8123-456789abcdef";

class FailingCreateDirectoriesFileSystem final
  : public IRequestPreparationFileSystem {
public:
  explicit FailingCreateDirectoriesFileSystem(std::filesystem::path fail_path)
    : fail_path_(std::move(fail_path))
  {
  }

  [[nodiscard]] auto Exists(const std::filesystem::path& path) const
    -> bool override
  {
    return std::filesystem::exists(path);
  }

  [[nodiscard]] auto IsDirectory(const std::filesystem::path& path) const
    -> bool override
  {
    auto ec = std::error_code {};
    return std::filesystem::is_directory(path, ec) && !ec;
  }

  [[nodiscard]] auto IsRegularFile(const std::filesystem::path& path) const
    -> bool override
  {
    auto ec = std::error_code {};
    return std::filesystem::is_regular_file(path, ec) && !ec;
  }

  auto CreateDirectories(const std::filesystem::path& path)
    -> std::error_code override
  {
    if (path == fail_path_) {
      return std::make_error_code(std::errc::permission_denied);
    }

    auto ec = std::error_code {};
    std::filesystem::create_directories(path, ec);
    return ec;
  }

private:
  std::filesystem::path fail_path_;
};

class PakToolRequestPreparationTest : public oxygen::cooker::test::TempDirTest {
protected:
  static auto WriteBasePak(
    const std::filesystem::path& path, const uint16_t content_version) -> void
  {
    std::filesystem::create_directories(path.parent_path());
    std::array<uint8_t, oxygen::data::SourceKey::kSizeBytes> identity {};
    identity.at(0) = static_cast<uint8_t>(content_version);
    identity.at(6) = 0x70U;
    identity.at(8) = 0x80U;
    const auto key = oxygen::data::SourceKey::FromBytes(identity);
    ASSERT_HAS_VALUE(key);
    const auto result = oxygen::content::pak::PakBuilder {}.Build({
      .mode = BuildMode::kFull,
      .sources = {},
      .output_pak_path = path,
      .content_version = content_version,
      .source_key = *key,
    });
    ASSERT_HAS_VALUE(result);
    ASSERT_EQ(result->summary.diagnostics_error, 0U);
  }

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
  options.request.embed_browse_index = true;
  options.request.compute_crc32 = false;
  options.request.fail_on_warnings = true;

  auto fs = RealRequestPreparationFileSystem {};
  const auto prepared = PreparePakToolRequest(BuildMode::kFull, options, fs);

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
  EXPECT_TRUE(prepared->build_request.options.embed_browse_index);
  EXPECT_FALSE(prepared->build_request.options.compute_crc32);
  EXPECT_TRUE(prepared->build_request.options.fail_on_warnings);
  EXPECT_TRUE(
    std::filesystem::exists(options.request.output_pak.parent_path()));
  EXPECT_TRUE(
    std::filesystem::exists(options.output.diagnostics_file.parent_path()));
}

NOLINT_TEST_F(PakToolRequestPreparationTest, RejectsInvalidSourceKeyText)
{
  auto options = MakeOptions();
  options.request.source_key = "not-a-uuid";
  options.request.output_pak = TempDir() / "release" / "game.pak";
  options.request.catalog_output
    = TempDir() / "release" / "game.pakcatalog.json";

  auto fs = RealRequestPreparationFileSystem {};
  const auto prepared = PreparePakToolRequest(BuildMode::kFull, options, fs);

  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(prepared.error().error_code, "paktool.prepare.invalid_source_key");
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

  auto fs = RealRequestPreparationFileSystem {};
  const auto prepared = PreparePakToolRequest(BuildMode::kFull, options, fs);

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

  auto fs = RealRequestPreparationFileSystem {};
  const auto prepared = PreparePakToolRequest(BuildMode::kPatch, options, fs);

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

  auto fs = RealRequestPreparationFileSystem {};
  const auto prepared = PreparePakToolRequest(BuildMode::kPatch, options, fs);

  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(prepared.error().error_code, "paktool.prepare.base_pak_required");
}

NOLINT_TEST_F(
  PakToolRequestPreparationTest, PreparePatchRequestLoadsOrderedBasePaks)
{
  auto options = MakeOptions();
  const auto base_a = TempDir() / "bases" / "base_a.pak";
  const auto base_b = TempDir() / "bases" / "base_b.pak";
  WriteBasePak(base_a, 10);
  WriteBasePak(base_b, 11);

  options.request.output_pak = TempDir() / "release" / "game_patch.pak";
  options.request.catalog_output
    = TempDir() / "release" / "game_patch.pakcatalog.json";
  options.patch.manifest_output
    = TempDir() / "release" / "game_patch.manifest.json";
  options.patch.base_paks = { base_a, base_b };

  auto fs = RealRequestPreparationFileSystem {};
  const auto prepared = PreparePakToolRequest(BuildMode::kPatch, options, fs);

  ASSERT_HAS_VALUE(prepared)
    << prepared.error().error_code << ": " << prepared.error().error_message;
  ASSERT_EQ(prepared->build_request.base_catalogs.size(), 2U);
  ASSERT_EQ(prepared->request_snapshot.base_pak_paths.size(), 2U);
  EXPECT_EQ(prepared->request_snapshot.base_pak_paths.at(0), base_a);
  EXPECT_EQ(prepared->request_snapshot.base_pak_paths.at(1), base_b);
}

NOLINT_TEST_F(PakToolRequestPreparationTest, RejectsConflictingToolPaths)
{
  auto options = MakeOptions();
  const auto shared_output = TempDir() / "release" / "game.pak";
  options.request.output_pak = shared_output;
  options.request.catalog_output = shared_output;

  auto fs = RealRequestPreparationFileSystem {};
  const auto prepared = PreparePakToolRequest(BuildMode::kFull, options, fs);

  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(prepared.error().error_code, "paktool.prepare.path_conflict");
  EXPECT_EQ(prepared.error().path, shared_output);
}

NOLINT_TEST_F(
  PakToolRequestPreparationTest, RejectsOutputParentDirectoryCreationFailure)
{
  auto options = MakeOptions();
  const auto fail_parent = TempDir() / "blocked";
  options.request.output_pak = fail_parent / "game.pak";
  options.request.catalog_output
    = TempDir() / "release" / "game.pakcatalog.json";

  auto fs = FailingCreateDirectoriesFileSystem(fail_parent);
  const auto prepared = PreparePakToolRequest(BuildMode::kFull, options, fs);

  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(prepared.error().error_code,
    "paktool.prepare.output_pak_parent.create_parent_failed");
  EXPECT_EQ(prepared.error().path, fail_parent);
}

} // namespace
