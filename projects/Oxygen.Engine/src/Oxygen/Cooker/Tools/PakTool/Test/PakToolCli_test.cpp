//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <filesystem>

#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Tools/PakTool/CliBuilder.h>
#include <Oxygen/Data/CookedSource.h>

namespace {

using oxygen::clap::CmdLineArgumentsError;
using oxygen::content::pak::tool::BuildCli;
using oxygen::content::pak::tool::PakToolCliOptions;

constexpr auto kSourceKey = "01234567-89ab-7def-8123-456789abcdef";

NOLINT_TEST(PakToolCliTest, BuildCommandParsesCommonAndFullBuildOptions)
{
  auto options = PakToolCliOptions {};
  const auto cli = BuildCli(options);

  constexpr auto argc = 24;
  const auto argv = std::array {
    "Oxygen.Cooker.PakTool",
    "build",
    "--loose-source",
    "C:/Cooked/Base",
    "--loose-source",
    "C:/Cooked/Dlc",
    "--pak-source",
    "C:/Cooked/base.pak",
    "--out",
    "C:/Build/game.pak",
    "--catalog-out",
    "C:/Build/game.pakcatalog.json",
    "--content-version",
    "42",
    "--source-key",
    kSourceKey,
    "--manifest-out",
    "C:/Build/game.manifest.json",
    "--non-deterministic",
    "--embed-browse-index",
    "--no-crc32",
    "--fail-on-warnings",
    "--diagnostics-file",
    "C:/Build/game.report.json",
  };

  const auto context = cli->Parse(argc, const_cast<const char**>(argv.data()));

  EXPECT_EQ(context.active_command->PathAsString(), "build");
  ASSERT_EQ(options.request.sources.size(), 3U);
  EXPECT_EQ(options.request.sources[0].kind,
    oxygen::data::CookedSourceKind::kLooseCooked);
  EXPECT_EQ(
    options.request.sources[0].path, std::filesystem::path("C:/Cooked/Base"));
  EXPECT_EQ(options.request.sources[1].kind,
    oxygen::data::CookedSourceKind::kLooseCooked);
  EXPECT_EQ(
    options.request.sources[1].path, std::filesystem::path("C:/Cooked/Dlc"));
  EXPECT_EQ(
    options.request.sources[2].kind, oxygen::data::CookedSourceKind::kPak);
  EXPECT_EQ(options.request.sources[2].path,
    std::filesystem::path("C:/Cooked/base.pak"));
  EXPECT_EQ(
    options.request.output_pak, std::filesystem::path("C:/Build/game.pak"));
  EXPECT_EQ(options.request.catalog_output,
    std::filesystem::path("C:/Build/game.pakcatalog.json"));
  EXPECT_EQ(options.request.content_version, 42);
  EXPECT_EQ(options.request.source_key, kSourceKey);
  EXPECT_FALSE(options.request.deterministic);
  EXPECT_TRUE(options.request.embed_browse_index);
  EXPECT_FALSE(options.request.compute_crc32);
  EXPECT_TRUE(options.request.fail_on_warnings);
  EXPECT_EQ(options.build.manifest_output,
    std::filesystem::path("C:/Build/game.manifest.json"));
  EXPECT_EQ(options.output.diagnostics_file,
    std::filesystem::path("C:/Build/game.report.json"));
}

NOLINT_TEST(PakToolCliTest, PatchCommandParsesBaseCatalogsAndRelaxationFlags)
{
  auto options = PakToolCliOptions {};
  const auto cli = BuildCli(options);

  constexpr auto argc = 26;
  const auto argv = std::array {
    "Oxygen.Cooker.PakTool",
    "patch",
    "--out",
    "C:/Build/game_patch.pak",
    "--catalog-out",
    "C:/Build/game_patch.pakcatalog.json",
    "--content-version",
    "77",
    "--source-key",
    kSourceKey,
    "--base-catalog",
    "C:/Build/base_1.pakcatalog.json",
    "--base-catalog",
    "C:/Build/base_2.pakcatalog.json",
    "--manifest-out",
    "C:/Build/game_patch.manifest.json",
    "--allow-base-set-mismatch",
    "--allow-content-version-mismatch",
    "--allow-base-source-key-mismatch",
    "--allow-catalog-digest-mismatch",
    "--quiet",
    "--no-color",
    "--loose-source",
    "C:/Cooked/Patch",
    "--pak-source",
    "C:/Cooked/base_patch_input.pak",
  };

  const auto context = cli->Parse(argc, const_cast<const char**>(argv.data()));

  EXPECT_EQ(context.active_command->PathAsString(), "patch");
  ASSERT_EQ(options.patch.base_catalogs.size(), 2U);
  EXPECT_EQ(options.patch.base_catalogs[0],
    std::filesystem::path("C:/Build/base_1.pakcatalog.json"));
  EXPECT_EQ(options.patch.base_catalogs[1],
    std::filesystem::path("C:/Build/base_2.pakcatalog.json"));
  EXPECT_EQ(options.patch.manifest_output,
    std::filesystem::path("C:/Build/game_patch.manifest.json"));
  EXPECT_TRUE(options.patch.allow_base_set_mismatch);
  EXPECT_TRUE(options.patch.allow_content_version_mismatch);
  EXPECT_TRUE(options.patch.allow_base_source_key_mismatch);
  EXPECT_TRUE(options.patch.allow_catalog_digest_mismatch);
  EXPECT_TRUE(options.output.quiet);
  EXPECT_TRUE(options.output.no_color);
  ASSERT_EQ(options.request.sources.size(), 2U);
  EXPECT_EQ(options.request.sources[0].kind,
    oxygen::data::CookedSourceKind::kLooseCooked);
  EXPECT_EQ(
    options.request.sources[0].path, std::filesystem::path("C:/Cooked/Patch"));
  EXPECT_EQ(
    options.request.sources[1].kind, oxygen::data::CookedSourceKind::kPak);
  EXPECT_EQ(options.request.sources[1].path,
    std::filesystem::path("C:/Cooked/base_patch_input.pak"));
}

NOLINT_TEST(PakToolCliTest, BuildCommandRejectsMissingRequiredCommonOptions)
{
  auto options = PakToolCliOptions {};
  const auto cli = BuildCli(options);

  constexpr auto argc = 8;
  const auto argv = std::array {
    "Oxygen.Cooker.PakTool",
    "build",
    "--out",
    "C:/Build/game.pak",
    "--content-version",
    "42",
    "--source-key",
    kSourceKey,
  };

  NOLINT_EXPECT_THROW(cli->Parse(argc, const_cast<const char**>(argv.data())),
    CmdLineArgumentsError);
}

NOLINT_TEST(PakToolCliTest, PatchCommandRejectsMissingPatchOnlyRequirements)
{
  auto options = PakToolCliOptions {};
  const auto cli = BuildCli(options);

  constexpr auto argc = 10;
  const auto argv = std::array {
    "Oxygen.Cooker.PakTool",
    "patch",
    "--out",
    "C:/Build/game_patch.pak",
    "--catalog-out",
    "C:/Build/game_patch.pakcatalog.json",
    "--content-version",
    "77",
    "--source-key",
    kSourceKey,
  };

  NOLINT_EXPECT_THROW(cli->Parse(argc, const_cast<const char**>(argv.data())),
    CmdLineArgumentsError);
}

NOLINT_TEST(PakToolCliTest, BuildCommandRejectsPatchOnlyOptionSurface)
{
  auto options = PakToolCliOptions {};
  const auto cli = BuildCli(options);

  constexpr auto argc = 19;
  const auto argv = std::array {
    "Oxygen.Cooker.PakTool",
    "build",
    "--out",
    "C:/Build/game.pak",
    "--catalog-out",
    "C:/Build/game.pakcatalog.json",
    "--content-version",
    "42",
    "--source-key",
    kSourceKey,
    "--base-catalog",
    "C:/Build/base_1.pakcatalog.json",
    "--manifest-out",
    "C:/Build/game.manifest.json",
    "--loose-source",
    "C:/Cooked/Base",
    "--pak-source",
    "C:/Cooked/base.pak",
    "--quiet",
  };

  NOLINT_EXPECT_THROW(cli->Parse(argc, const_cast<const char**>(argv.data())),
    CmdLineArgumentsError);
}

} // namespace
