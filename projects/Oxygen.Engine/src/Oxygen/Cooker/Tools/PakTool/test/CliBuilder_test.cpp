//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/PakTool/CliBuilder.cpp, Tools/PakTool/PakToolCommands.cpp

#include <array>
#include <filesystem>

#include "PakToolOptions.h"

#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/PakTool/PakToolCommands.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::clap::CmdLineArgumentsError;
using oxygen::content::cooked::RealCookerStorage;
using oxygen::content::pak::BuildMode;
using oxygen::content::pak::tool::PakToolCliOptions;
using oxygen::content::pak::tool::PakToolCommand;
using oxygen::content::pak::tool::PakToolInfo;
using oxygen::content::toolkit::ToolApp;
using oxygen::content::toolkit::ToolCommand;

constexpr auto kSourceKey = "01234567-89ab-7def-8123-456789abcdef";

//! The PakTool parser over `options`. Parse-only: no command is ever run, so
//! the real file systems are never touched.
class ParseOnlyTool {
public:
  explicit ParseOnlyTool(PakToolCliOptions& options)
    : build_(BuildMode::kFull, options, RealCookerStorage())
    , patch_(BuildMode::kPatch, options, RealCookerStorage())
    , app_(PakToolInfo(), std::array<ToolCommand*, 2> { &build_, &patch_ })
  {
  }

  [[nodiscard]] auto Cli() -> oxygen::clap::Cli& { return app_.Cli(); }

private:
  PakToolCommand build_;
  PakToolCommand patch_;
  ToolApp app_;
};

NOLINT_TEST(PakToolCliTest, BuildCommandParsesCommonAndFullBuildOptions)
{
  auto options = PakToolCliOptions {};
  auto tool = ParseOnlyTool(options);

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
    "--no-crc32",
    "--fail-on-warnings",
    "--diagnostics-file",
    "C:/Build/game.report.json",
  };

  const auto context = tool.Cli().Parse(argv);

  EXPECT_EQ(context.active_command->PathAsString(), "build");
  ASSERT_EQ(options.request.sources.size(), 3U);
  EXPECT_EQ(options.request.sources.at(0).kind,
    oxygen::data::CookedSourceKind::kLooseCooked);
  EXPECT_EQ(options.request.sources.at(0).path,
    std::filesystem::path("C:/Cooked/Base"));
  EXPECT_EQ(options.request.sources.at(1).kind,
    oxygen::data::CookedSourceKind::kLooseCooked);
  EXPECT_EQ(
    options.request.sources.at(1).path, std::filesystem::path("C:/Cooked/Dlc"));
  EXPECT_EQ(
    options.request.sources.at(2).kind, oxygen::data::CookedSourceKind::kPak);
  EXPECT_EQ(options.request.sources.at(2).path,
    std::filesystem::path("C:/Cooked/base.pak"));
  EXPECT_EQ(
    options.request.output_pak, std::filesystem::path("C:/Build/game.pak"));
  EXPECT_EQ(options.request.catalog_output,
    std::filesystem::path("C:/Build/game.pakcatalog.json"));
  EXPECT_EQ(options.request.content_version, 42);
  EXPECT_EQ(options.request.source_key, kSourceKey);
  EXPECT_FALSE(options.request.compute_crc32);
  EXPECT_TRUE(options.request.fail_on_warnings);
  EXPECT_EQ(options.build.manifest_output,
    std::filesystem::path("C:/Build/game.manifest.json"));
  EXPECT_EQ(options.output.diagnostics_file,
    std::filesystem::path("C:/Build/game.report.json"));
}

NOLINT_TEST(PakToolCliTest, PatchCommandPreservesBaseCatalogOrder)
{
  auto options = PakToolCliOptions {};
  auto tool = ParseOnlyTool(options);

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
    "--base-pak",
    "C:/Build/base_1.pakcatalog.json",
    "--base-pak",
    "C:/Build/base_2.pakcatalog.json",
    "--manifest-out",
    "C:/Build/game_patch.manifest.json",
    "--quiet",
    "--no-color",
    "--loose-source",
    "C:/Cooked/Patch",
    "--pak-source",
    "C:/Cooked/base_patch_input.pak",
  };

  const auto context = tool.Cli().Parse(argv);

  EXPECT_EQ(context.active_command->PathAsString(), "patch");
  ASSERT_EQ(options.patch.base_paks.size(), 2U);
  EXPECT_EQ(options.patch.base_paks.at(0),
    std::filesystem::path("C:/Build/base_1.pakcatalog.json"));
  EXPECT_EQ(options.patch.base_paks.at(1),
    std::filesystem::path("C:/Build/base_2.pakcatalog.json"));
  EXPECT_EQ(options.patch.manifest_output,
    std::filesystem::path("C:/Build/game_patch.manifest.json"));
  EXPECT_TRUE(options.output.quiet);
  EXPECT_TRUE(options.output.no_color);
  ASSERT_EQ(options.request.sources.size(), 2U);
  EXPECT_EQ(options.request.sources.at(0).kind,
    oxygen::data::CookedSourceKind::kLooseCooked);
  EXPECT_EQ(options.request.sources.at(0).path,
    std::filesystem::path("C:/Cooked/Patch"));
  EXPECT_EQ(
    options.request.sources.at(1).kind, oxygen::data::CookedSourceKind::kPak);
  EXPECT_EQ(options.request.sources.at(1).path,
    std::filesystem::path("C:/Cooked/base_patch_input.pak"));
}

NOLINT_TEST(PakToolCliTest, BuildCommandRejectsMissingRequiredCommonOptions)
{
  auto options = PakToolCliOptions {};
  auto tool = ParseOnlyTool(options);

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

  NOLINT_EXPECT_THROW(tool.Cli().Parse(argv), CmdLineArgumentsError);
}

NOLINT_TEST(PakToolCliTest, PatchCommandRejectsMissingPatchOnlyRequirements)
{
  auto options = PakToolCliOptions {};
  auto tool = ParseOnlyTool(options);

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

  NOLINT_EXPECT_THROW(tool.Cli().Parse(argv), CmdLineArgumentsError);
}

NOLINT_TEST(PakToolCliTest, BuildCommandRejectsPatchOnlyOptionSurface)
{
  auto options = PakToolCliOptions {};
  auto tool = ParseOnlyTool(options);

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
    "--base-pak",
    "C:/Build/base_1.pakcatalog.json",
    "--manifest-out",
    "C:/Build/game.manifest.json",
    "--loose-source",
    "C:/Cooked/Base",
    "--pak-source",
    "C:/Cooked/base.pak",
    "--quiet",
  };

  NOLINT_EXPECT_THROW(tool.Cli().Parse(argv), CmdLineArgumentsError);
}

//! Both commands take dependency paks, whose assets the pak references.
NOLINT_TEST(PakToolCliTest, BuildCommandAcceptsDependencyPaks)
{
  auto options = PakToolCliOptions {};
  auto tool = ParseOnlyTool(options);

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
    "--loose-source",
    "C:/Cooked/Game",
    "--dependency-pak",
    "C:/Sdk/share/oxygen/engine.pak",
    "--quiet",
  };

  const auto context = tool.Cli().Parse(argv);

  EXPECT_EQ(context.active_command->PathAsString(), "build");
  ASSERT_EQ(options.request.dependency_paks.size(), 1U);
  EXPECT_EQ(options.request.dependency_paks.front(),
    std::filesystem::path("C:/Sdk/share/oxygen/engine.pak"));
}

//! Script sealing takes source mounts as ImportTool does; the script source
//! root option is gone.
NOLINT_TEST(PakToolCliTest, BuildCommandAcceptsSourceMountOptions)
{
  auto options = PakToolCliOptions {};
  auto tool = ParseOnlyTool(options);

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
    "--loose-source",
    "C:/Cooked/Game",
    "--mount",
    "Content=C:/Project/Content",
    "--mount",
    "Media=D:/Media",
    "--mounts",
    "C:/Build/mounts.json",
    "--root",
    "C:/Project",
  };

  static_cast<void>(tool.Cli().Parse(argv));

  ASSERT_EQ(options.request.source_mounts.size(), 2U);
  EXPECT_EQ(options.request.source_mounts.at(0), "Content=C:/Project/Content");
  EXPECT_EQ(options.request.source_mounts.at(1), "Media=D:/Media");
  EXPECT_EQ(
    options.request.mounts_file, std::filesystem::path("C:/Build/mounts.json"));
  EXPECT_EQ(options.request.project_root, std::filesystem::path("C:/Project"));
}

} // namespace
