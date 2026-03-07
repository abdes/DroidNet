//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <filesystem>
#include <fstream>
#include <process.h>
#include <string_view>

#include <Oxygen/Cooker/Pak/PakCatalogIo.h>
#include <Oxygen/Cooker/Tools/PakTool/CommandExecution.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::pak::BuildMode;
using oxygen::content::pak::PakCatalogIo;
using oxygen::content::pak::tool::ExecutePakToolCommand;
using oxygen::content::pak::tool::PakToolCliOptions;
using oxygen::content::pak::tool::PakToolExitCode;
using oxygen::content::pak::tool::RealArtifactFileSystem;
using oxygen::content::pak::tool::RealRequestPreparationFileSystem;

constexpr auto kSourceKey = "01234567-89ab-7def-8123-456789abcdef";
constexpr auto kToolVersion = "0.1";

class PakToolCommandExecutionTest : public testing::Test {
protected:
  void SetUp() override
  {
    static auto counter = std::atomic_uint64_t { 0 };
    const auto pid = static_cast<unsigned long long>(_getpid());
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    root_ = std::filesystem::temp_directory_path() / "oxygen_paktool_exec"
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

  [[nodiscard]] static auto ReadTextFile(const std::filesystem::path& path)
    -> std::string
  {
    auto in = std::ifstream(path, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << path.string();
    return std::string(
      std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }

  [[nodiscard]] static auto MakeOptions() -> PakToolCliOptions
  {
    auto options = PakToolCliOptions {};
    options.request.content_version = 42;
    options.request.source_key = kSourceKey;
    return options;
  }

private:
  std::filesystem::path root_;
};

NOLINT_TEST_F(
  PakToolCommandExecutionTest, FullBuildPublishesPakCatalogAndRequestedReport)
{
  auto options = MakeOptions();
  options.request.output_pak = Root() / "release" / "game_full.pak";
  options.request.catalog_output
    = Root() / "release" / "game_full.pakcatalog.json";
  options.output.diagnostics_file
    = Root() / "release" / "game_full.report.json";

  auto prep_fs = RealRequestPreparationFileSystem {};
  auto artifact_fs = RealArtifactFileSystem {};
  const auto result = ExecutePakToolCommand(BuildMode::kFull, "build",
    "Oxygen.Cooker.PakTool build", kToolVersion, options, prep_fs, artifact_fs);

  EXPECT_EQ(result.exit_code, PakToolExitCode::kSuccess)
    << result.error_code << ": " << result.error_message;
  EXPECT_TRUE(std::filesystem::exists(options.request.output_pak));
  EXPECT_TRUE(std::filesystem::exists(options.request.catalog_output));
  EXPECT_TRUE(std::filesystem::exists(options.output.diagnostics_file));
  EXPECT_FALSE(
    std::filesystem::exists(Root() / "release" / "game_full.manifest.json"));

  const auto catalog = PakCatalogIo::Read(options.request.catalog_output);
  ASSERT_TRUE(catalog.has_value());
  EXPECT_EQ(catalog->content_version, options.request.content_version);
  EXPECT_EQ(catalog->source_key, result.build_result.output_catalog.source_key);
}

NOLINT_TEST_F(
  PakToolCommandExecutionTest, FullBuildWithManifestPublishesManifestArtifact)
{
  auto options = MakeOptions();
  options.request.output_pak = Root() / "release" / "game_full_manifest.pak";
  options.request.catalog_output
    = Root() / "release" / "game_full_manifest.pakcatalog.json";
  options.build.manifest_output
    = Root() / "release" / "game_full_manifest.json";

  auto prep_fs = RealRequestPreparationFileSystem {};
  auto artifact_fs = RealArtifactFileSystem {};
  const auto result = ExecutePakToolCommand(BuildMode::kFull, "build",
    "Oxygen.Cooker.PakTool build --manifest-out", kToolVersion, options,
    prep_fs, artifact_fs);

  EXPECT_EQ(result.exit_code, PakToolExitCode::kSuccess)
    << result.error_code << ": " << result.error_message;
  EXPECT_TRUE(std::filesystem::exists(options.request.output_pak));
  EXPECT_TRUE(std::filesystem::exists(options.request.catalog_output));
  EXPECT_TRUE(std::filesystem::exists(options.build.manifest_output));
}

NOLINT_TEST_F(
  PakToolCommandExecutionTest, PatchBuildPublishesPakCatalogAndManifest)
{
  auto base_options = MakeOptions();
  base_options.request.output_pak = Root() / "base" / "base.pak";
  base_options.request.catalog_output
    = Root() / "base" / "base.pakcatalog.json";

  auto prep_fs = RealRequestPreparationFileSystem {};
  auto artifact_fs = RealArtifactFileSystem {};
  const auto base_result = ExecutePakToolCommand(BuildMode::kFull, "build",
    "Oxygen.Cooker.PakTool build", kToolVersion, base_options, prep_fs,
    artifact_fs);
  ASSERT_EQ(base_result.exit_code, PakToolExitCode::kSuccess)
    << base_result.error_code << ": " << base_result.error_message;

  auto patch_options = MakeOptions();
  patch_options.request.output_pak = Root() / "patch" / "patch.pak";
  patch_options.request.catalog_output
    = Root() / "patch" / "patch.pakcatalog.json";
  patch_options.patch.base_catalogs = { base_options.request.catalog_output };
  patch_options.patch.manifest_output
    = Root() / "patch" / "patch.manifest.json";

  const auto patch_result = ExecutePakToolCommand(BuildMode::kPatch, "patch",
    "Oxygen.Cooker.PakTool patch", kToolVersion, patch_options, prep_fs,
    artifact_fs);

  EXPECT_EQ(patch_result.exit_code, PakToolExitCode::kSuccess)
    << patch_result.error_code << ": " << patch_result.error_message;
  EXPECT_TRUE(std::filesystem::exists(patch_options.request.output_pak));
  EXPECT_TRUE(std::filesystem::exists(patch_options.request.catalog_output));
  EXPECT_TRUE(std::filesystem::exists(patch_options.patch.manifest_output));
}

NOLINT_TEST_F(PakToolCommandExecutionTest,
  BuildFailureSuppressesFinalCatalogAndManifestSidecars)
{
  auto seed_options = MakeOptions();
  seed_options.request.output_pak = Root() / "seed" / "seed.pak";
  seed_options.request.catalog_output
    = Root() / "seed" / "seed.pakcatalog.json";

  auto prep_fs = RealRequestPreparationFileSystem {};
  auto artifact_fs = RealArtifactFileSystem {};
  const auto seed_result = ExecutePakToolCommand(BuildMode::kFull, "build",
    "Oxygen.Cooker.PakTool build", kToolVersion, seed_options, prep_fs,
    artifact_fs);
  ASSERT_EQ(seed_result.exit_code, PakToolExitCode::kSuccess)
    << seed_result.error_code << ": " << seed_result.error_message;

  auto options = MakeOptions();
  options.request.sources = {
    oxygen::data::CookedSource {
      .kind = oxygen::data::CookedSourceKind::kPak,
      .path = seed_options.request.output_pak,
    },
  };
  options.request.output_pak = Root() / "release" / "game_failure.pak";
  options.request.catalog_output
    = Root() / "release" / "game_failure.pakcatalog.json";
  options.build.manifest_output
    = Root() / "release" / "game_failure.manifest.json";
  options.output.diagnostics_file
    = Root() / "release" / "game_failure.report.json";
  options.request.fail_on_warnings = true;

  WriteTextFile(options.request.output_pak, "pak-old");
  WriteTextFile(options.request.catalog_output, "catalog-old");
  WriteTextFile(options.build.manifest_output, "manifest-old");

  const auto result = ExecutePakToolCommand(BuildMode::kFull, "build",
    "Oxygen.Cooker.PakTool build --fail-on-warnings", kToolVersion, options,
    prep_fs, artifact_fs);

  EXPECT_EQ(result.exit_code, PakToolExitCode::kBuildFailure)
    << result.error_code << ": " << result.error_message;
  EXPECT_EQ(ReadTextFile(options.request.output_pak), "pak-old");
  EXPECT_FALSE(std::filesystem::exists(options.request.catalog_output));
  EXPECT_FALSE(std::filesystem::exists(options.build.manifest_output));
  EXPECT_TRUE(std::filesystem::exists(options.output.diagnostics_file));
}

} // namespace
