//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <filesystem>
#include <fstream>
#include <optional>
#include <process.h>

#include <Oxygen/Cooker/Pak/PakCatalogIo.h>
#include <Oxygen/Cooker/Tools/PakTool/RequestPreparation.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::pak::BuildMode;
using oxygen::content::pak::PakCatalogIo;
using oxygen::content::pak::tool::IRequestPreparationFileSystem;
using oxygen::content::pak::tool::PakToolCliOptions;
using oxygen::content::pak::tool::PreparePakToolRequest;
using oxygen::content::pak::tool::RealRequestPreparationFileSystem;

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

class PakToolRequestPreparationTest : public testing::Test {
protected:
  void SetUp() override
  {
    static auto counter = std::atomic_uint64_t { 0 };
    const auto pid = static_cast<unsigned long long>(_getpid());
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    root_ = std::filesystem::temp_directory_path() / "oxygen_paktool_prepare"
      / ("pid-" + std::to_string(pid) + "-case-" + std::to_string(id));
    std::filesystem::remove_all(root_);
    std::filesystem::create_directories(root_);
  }

  void TearDown() override
  {
    std::error_code ec {};
    std::filesystem::remove_all(root_, ec);
  }

  [[nodiscard]] auto Root() const -> const std::filesystem::path&
  {
    return root_;
  }

  static auto WriteTextFile(
    const std::filesystem::path& path, const std::string_view content) -> void
  {
    std::filesystem::create_directories(path.parent_path());
    auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << path.string();
    out << content;
  }

  static auto WriteCatalogFile(
    const std::filesystem::path& path, const uint16_t content_version) -> void
  {
    const auto source_key = oxygen::data::SourceKey::FromString(kSourceKey);
    ASSERT_TRUE(source_key.has_value());

    WriteTextFile(path,
      PakCatalogIo::ToCanonicalJsonString(oxygen::data::PakCatalog {
        .source_key = source_key.value(),
        .content_version = content_version,
        .catalog_digest = {},
        .entries = {},
      }));
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

private:
  std::filesystem::path root_;
};

NOLINT_TEST_F(PakToolRequestPreparationTest,
  PrepareFullBuildRequestPreservesSourceOrderAndStagesOutputs)
{
  auto options = MakeOptions();

  const auto loose_a = Root() / "cook" / "base";
  const auto loose_b = Root() / "cook" / "dlc";
  const auto pak_source = Root() / "cook" / "base.pak";
  std::filesystem::create_directories(loose_a);
  std::filesystem::create_directories(loose_b);
  WriteTextFile(pak_source, "pak");

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
  options.request.output_pak = Root() / "release" / "game.pak";
  options.request.catalog_output = Root() / "release" / "game.pakcatalog.json";
  options.build.manifest_output = Root() / "release" / "game.manifest.json";
  options.output.diagnostics_file = Root() / "release" / "game.report.json";
  options.request.deterministic = false;
  options.request.embed_browse_index = true;
  options.request.compute_crc32 = false;
  options.request.fail_on_warnings = true;

  auto fs = RealRequestPreparationFileSystem {};
  const auto prepared = PreparePakToolRequest(BuildMode::kFull, options, fs);

  ASSERT_TRUE(prepared.has_value())
    << prepared.error().error_code << ": " << prepared.error().error_message;
  ASSERT_EQ(prepared->build_request.sources.size(), 3U);
  EXPECT_EQ(prepared->build_request.sources[0].kind,
    oxygen::data::CookedSourceKind::kLooseCooked);
  EXPECT_EQ(prepared->build_request.sources[1].kind,
    oxygen::data::CookedSourceKind::kPak);
  EXPECT_EQ(prepared->build_request.sources[2].kind,
    oxygen::data::CookedSourceKind::kLooseCooked);
  EXPECT_EQ(prepared->build_request.output_pak_path,
    prepared->publication_plan.pak.staged_path);
  ASSERT_TRUE(prepared->publication_plan.manifest.has_value());
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
  options.request.output_pak = Root() / "release" / "game.pak";
  options.request.catalog_output = Root() / "release" / "game.pakcatalog.json";

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
      .path = Root() / "missing_source",
    },
  };
  options.request.output_pak = Root() / "release" / "game.pak";
  options.request.catalog_output = Root() / "release" / "game.pakcatalog.json";

  auto fs = RealRequestPreparationFileSystem {};
  const auto prepared = PreparePakToolRequest(BuildMode::kFull, options, fs);

  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(prepared.error().error_code, "paktool.prepare.source_missing");
  EXPECT_EQ(prepared.error().path, Root() / "missing_source");
}

NOLINT_TEST_F(PakToolRequestPreparationTest, RejectsInvalidBaseCatalogPayload)
{
  auto options = MakeOptions();
  const auto base_catalog = Root() / "catalogs" / "base.pakcatalog.json";
  WriteTextFile(base_catalog, "{ invalid json }");

  options.request.output_pak = Root() / "release" / "game_patch.pak";
  options.request.catalog_output
    = Root() / "release" / "game_patch.pakcatalog.json";
  options.patch.manifest_output
    = Root() / "release" / "game_patch.manifest.json";
  options.patch.base_catalogs = { base_catalog };

  auto fs = RealRequestPreparationFileSystem {};
  const auto prepared = PreparePakToolRequest(BuildMode::kPatch, options, fs);

  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(
    prepared.error().error_code, "paktool.prepare.base_catalog_invalid");
  EXPECT_EQ(prepared.error().path, base_catalog);
}

NOLINT_TEST_F(
  PakToolRequestPreparationTest, RejectsPatchModeWithoutBaseCatalogs)
{
  auto options = MakeOptions();
  options.request.output_pak = Root() / "release" / "game_patch.pak";
  options.request.catalog_output
    = Root() / "release" / "game_patch.pakcatalog.json";
  options.patch.manifest_output
    = Root() / "release" / "game_patch.manifest.json";

  auto fs = RealRequestPreparationFileSystem {};
  const auto prepared = PreparePakToolRequest(BuildMode::kPatch, options, fs);

  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(
    prepared.error().error_code, "paktool.prepare.base_catalog_required");
}

NOLINT_TEST_F(
  PakToolRequestPreparationTest, PreparePatchRequestLoadsMultipleBaseCatalogs)
{
  auto options = MakeOptions();
  const auto base_a = Root() / "catalogs" / "base_a.pakcatalog.json";
  const auto base_b = Root() / "catalogs" / "base_b.pakcatalog.json";
  WriteCatalogFile(base_a, 10);
  WriteCatalogFile(base_b, 11);

  options.request.output_pak = Root() / "release" / "game_patch.pak";
  options.request.catalog_output
    = Root() / "release" / "game_patch.pakcatalog.json";
  options.patch.manifest_output
    = Root() / "release" / "game_patch.manifest.json";
  options.patch.base_catalogs = { base_a, base_b };
  options.patch.allow_base_set_mismatch = true;
  options.patch.allow_content_version_mismatch = true;
  options.patch.allow_base_source_key_mismatch = true;
  options.patch.allow_catalog_digest_mismatch = true;

  auto fs = RealRequestPreparationFileSystem {};
  const auto prepared = PreparePakToolRequest(BuildMode::kPatch, options, fs);

  ASSERT_TRUE(prepared.has_value())
    << prepared.error().error_code << ": " << prepared.error().error_message;
  ASSERT_EQ(prepared->build_request.base_catalogs.size(), 2U);
  ASSERT_EQ(prepared->request_snapshot.base_catalog_paths.size(), 2U);
  EXPECT_EQ(prepared->request_snapshot.base_catalog_paths[0], base_a);
  EXPECT_EQ(prepared->request_snapshot.base_catalog_paths[1], base_b);
  EXPECT_FALSE(prepared->build_request.patch_compat.require_exact_base_set);
  EXPECT_FALSE(
    prepared->build_request.patch_compat.require_content_version_match);
  EXPECT_FALSE(
    prepared->build_request.patch_compat.require_base_source_key_match);
  EXPECT_FALSE(
    prepared->build_request.patch_compat.require_catalog_digest_match);
}

NOLINT_TEST_F(PakToolRequestPreparationTest, RejectsConflictingToolPaths)
{
  auto options = MakeOptions();
  const auto shared_output = Root() / "release" / "game.pak";
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
  const auto fail_parent = Root() / "blocked";
  options.request.output_pak = fail_parent / "game.pak";
  options.request.catalog_output = Root() / "release" / "game.pakcatalog.json";

  auto fs = FailingCreateDirectoriesFileSystem(fail_parent);
  const auto prepared = PreparePakToolRequest(BuildMode::kFull, options, fs);

  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(prepared.error().error_code,
    "paktool.prepare.output_pak_parent.create_parent_failed");
  EXPECT_EQ(prepared.error().path, fail_parent);
}

} // namespace
