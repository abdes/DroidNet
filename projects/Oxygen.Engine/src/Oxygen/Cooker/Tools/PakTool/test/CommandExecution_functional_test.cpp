//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/PakTool/CommandExecution.cpp

#include <filesystem>
#include <string_view>

#include "PakToolOptions.h"
#include "PakToolTestSupport.h"
#include <nlohmann/json.hpp>

#include <Oxygen/Content/AssetValidation.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakCatalogIo.h>
#include <Oxygen/Cooker/Tools/PakTool/CommandExecution.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/ScriptBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

using oxygen::content::PakFile;
using oxygen::content::cooked::RealCookerStorage;
using oxygen::content::pak::BuildMode;
using oxygen::content::pak::PakCatalogIo;
using oxygen::content::pak::tool::ExecutePakToolCommand;
using oxygen::content::pak::tool::PakToolCliOptions;
using oxygen::content::pak::tool::PakToolExitCode;
using oxygen::content::pak::tool::test::MakeGameRoot;
using oxygen::content::pak::tool::test::WriteGamePak;
using oxygen::content::pak::tool::test::WriteLooseGameRoot;
using oxygen::cooker::test::ReadText;
using oxygen::cooker::test::WriteText;
using oxygen::cooker::testing::Materialize;
using oxygen::cooker::testing::ScriptBuilder;
using oxygen::data::AssetKey;
using oxygen::data::AssetType;
using oxygen::data::kNoResourceReference;
using oxygen::data::pak::scripting::ScriptAssetDesc;
using oxygen::data::pak::scripting::ScriptAssetFlags;

constexpr auto kSourceKey = "01234567-89ab-7def-8123-456789abcdef";
constexpr auto kToolVersion = "0.1";

class PakToolCommandExecutionTest : public oxygen::testing::TempDirTest {
protected:
  //! Options for a build of a loose cooked root of the mount `Game`.
  [[nodiscard]] auto MakeOptions() const -> PakToolCliOptions
  {
    auto options = MakeSourcelessOptions();
    options.request.sources = { LooseSource("Loose") };
    return options;
  }

  [[nodiscard]] static auto MakeSourcelessOptions() -> PakToolCliOptions
  {
    auto options = PakToolCliOptions {};
    options.request.content_version = 42;
    options.request.source_key = kSourceKey;
    return options;
  }

  [[nodiscard]] auto LooseSource(const std::string_view material) const
    -> oxygen::data::CookedSource
  {
    return {
      .kind = oxygen::data::CookedSourceKind::kLooseCooked,
      .path = WriteLooseGameRoot(TempDir() / "loose" / material, material),
    };
  }

  //! Options for a build of a loose cooked root that holds, beside a
  //! material, the script `spin` whose source is the external file
  //! `/Content/spin.lua`; the mount `Content` is left to each test.
  [[nodiscard]] auto MakeExternalScriptOptions() -> PakToolCliOptions
  {
    WriteText(ContentFolder() / "spin.lua", "return { spin = true }\n");
    auto root = MakeGameRoot("Scripted");
    auto script = ScriptBuilder { "spin" };
    script.Flags(ScriptAssetFlags::kAllowExternalSource)
      .ExternalSource("/Content/spin.lua");
    script_key_ = root.Script("spin.oscript", script);
    const auto folder = TempDir() / "loose" / "Scripted";
    Materialize(root.Files(), folder);

    auto options = MakeSourcelessOptions();
    options.request.sources = {
      oxygen::data::CookedSource {
        .kind = oxygen::data::CookedSourceKind::kLooseCooked,
        .path = folder,
      },
    };
    options.request.output_pak = TempDir() / "release" / "scripted.pak";
    options.request.catalog_output
      = TempDir() / "release" / "scripted.pakcatalog.json";
    options.output.diagnostics_file
      = TempDir() / "release" / "scripted.report.json";
    return options;
  }

  //! The folder the mount `Content` names.
  [[nodiscard]] auto ContentFolder() const -> std::filesystem::path
  {
    return TempDir() / "Content";
  }

  //! The key of the script `MakeExternalScriptOptions` writes.
  [[nodiscard]] auto ScriptKey() const -> const AssetKey&
  {
    return script_key_;
  }

private:
  AssetKey script_key_;
};

NOLINT_TEST_F(
  PakToolCommandExecutionTest, FullBuildPublishesPakCatalogAndRequestedReport)
{
  auto options = MakeOptions();
  options.request.output_pak = TempDir() / "release" / "game_full.pak";
  options.request.catalog_output
    = TempDir() / "release" / "game_full.pakcatalog.json";
  options.output.diagnostics_file
    = TempDir() / "release" / "game_full.report.json";

  auto& storage = RealCookerStorage();
  const auto result = ExecutePakToolCommand(BuildMode::kFull, "build",
    "Oxygen.Cooker.PakTool build", kToolVersion, options, storage);

  EXPECT_EQ(result.exit_code, PakToolExitCode::kSuccess)
    << result.error_code << ": " << result.error_message;
  EXPECT_TRUE(std::filesystem::exists(options.request.output_pak));
  EXPECT_TRUE(std::filesystem::exists(options.request.catalog_output));
  EXPECT_TRUE(std::filesystem::exists(options.output.diagnostics_file));
  EXPECT_FALSE(
    std::filesystem::exists(TempDir() / "release" / "game_full.manifest.json"));

  const auto catalog = PakCatalogIo::Read(options.request.catalog_output);
  ASSERT_HAS_VALUE(catalog);
  EXPECT_EQ(catalog->content_version, options.request.content_version);
  EXPECT_EQ(catalog->source_key, result.build_result.output_catalog.source_key);
}

NOLINT_TEST_F(
  PakToolCommandExecutionTest, FullBuildWithManifestPublishesManifestArtifact)
{
  auto options = MakeOptions();
  options.request.output_pak = TempDir() / "release" / "game_full_manifest.pak";
  options.request.catalog_output
    = TempDir() / "release" / "game_full_manifest.pakcatalog.json";
  options.build.manifest_output
    = TempDir() / "release" / "game_full_manifest.json";

  auto& storage = RealCookerStorage();
  const auto result = ExecutePakToolCommand(BuildMode::kFull, "build",
    "Oxygen.Cooker.PakTool build --manifest-out", kToolVersion, options,
    storage);

  EXPECT_EQ(result.exit_code, PakToolExitCode::kSuccess)
    << result.error_code << ": " << result.error_message;
  EXPECT_TRUE(std::filesystem::exists(options.request.output_pak));
  EXPECT_TRUE(std::filesystem::exists(options.request.catalog_output));
  EXPECT_TRUE(std::filesystem::exists(options.build.manifest_output));
}

NOLINT_TEST_F(
  PakToolCommandExecutionTest, PatchBuildPublishesPakCatalogAndManifest)
{
  const auto base_pak = WriteGamePak(TempDir() / "base" / "base.pak", "Base");

  auto patch_options = MakeOptions();
  patch_options.request.source_key = "01234567-89ab-7def-8123-456789abcdee";
  patch_options.request.output_pak = TempDir() / "patch" / "patch.pak";
  patch_options.request.catalog_output
    = TempDir() / "patch" / "patch.pakcatalog.json";
  patch_options.patch.base_paks = { base_pak };
  patch_options.patch.manifest_output
    = TempDir() / "patch" / "patch.manifest.json";

  auto& storage = RealCookerStorage();
  const auto patch_result = ExecutePakToolCommand(BuildMode::kPatch, "patch",
    "Oxygen.Cooker.PakTool patch", kToolVersion, patch_options, storage);

  EXPECT_EQ(patch_result.exit_code, PakToolExitCode::kSuccess)
    << patch_result.error_code << ": " << patch_result.error_message;
  EXPECT_TRUE(std::filesystem::exists(patch_options.request.output_pak));
  EXPECT_TRUE(std::filesystem::exists(patch_options.request.catalog_output));
  EXPECT_TRUE(std::filesystem::exists(patch_options.patch.manifest_output));
}

//! A failed build publishes nothing: the earlier pak keeps its catalog and
//! manifest, and the report says the build failed.
NOLINT_TEST_F(PakToolCommandExecutionTest, BuildFailureKeepsEarlierReleaseWhole)
{
  // A pak source that cannot be read fails the build.
  const auto seed_pak = TempDir() / "seed" / "corrupt.pak";
  auto options = MakeSourcelessOptions();
  options.request.sources = {
    oxygen::data::CookedSource {
      .kind = oxygen::data::CookedSourceKind::kPak,
      .path = seed_pak,
    },
  };
  options.request.output_pak = TempDir() / "release" / "game_failure.pak";
  options.request.catalog_output
    = TempDir() / "release" / "game_failure.pakcatalog.json";
  options.build.manifest_output
    = TempDir() / "release" / "game_failure.manifest.json";
  options.output.diagnostics_file
    = TempDir() / "release" / "game_failure.report.json";
  WriteText(seed_pak, "corrupt-pak-source");
  WriteText(options.request.output_pak, "pak-old");
  WriteText(options.request.catalog_output, "catalog-old");
  WriteText(options.build.manifest_output, "manifest-old");

  auto& storage = RealCookerStorage();
  const auto result = ExecutePakToolCommand(BuildMode::kFull, "build",
    "Oxygen.Cooker.PakTool build", kToolVersion, options, storage);

  EXPECT_EQ(result.exit_code, PakToolExitCode::kBuildFailure)
    << result.error_code << ": " << result.error_message;
  EXPECT_EQ(ReadText(options.request.output_pak), "pak-old");
  EXPECT_EQ(ReadText(options.request.catalog_output), "catalog-old");
  EXPECT_EQ(ReadText(options.build.manifest_output), "manifest-old");
  const auto report
    = nlohmann::json::parse(ReadText(options.output.diagnostics_file));
  EXPECT_EQ(report.at("exit_code"), 3);
  EXPECT_FALSE(report.at("artifacts").at("pak").at("published"));
  EXPECT_FALSE(report.at("artifacts").at("catalog").at("published"));
}

//! A script whose source is an external file of a bound mount is sealed: in
//! the pak, it embeds its source, allows no external one, records the hash of
//! its rewritten descriptor, and passes the runtime's validation.
NOLINT_TEST_F(PakToolCommandExecutionTest,
  SealedExternalScriptPassesRuntimeValidationWithItsSourceEmbedded)
{
  auto options = MakeExternalScriptOptions();
  options.request.source_mounts = { "Content=" + ContentFolder().string() };

  auto& storage = RealCookerStorage();
  const auto result = ExecutePakToolCommand(BuildMode::kFull, "build",
    "Oxygen.Cooker.PakTool build --mount", kToolVersion, options, storage);

  ASSERT_EQ(result.exit_code, PakToolExitCode::kSuccess)
    << result.error_code << ": " << result.error_message;
  const auto pak = PakFile(options.request.output_pak);
  const auto entry = pak.FindEntry(ScriptKey());
  ASSERT_HAS_VALUE(entry);
  const auto bytes = pak.DescriptorBytes(*entry);
  EXPECT_NO_THROW(oxygen::content::ValidateAssetDescriptor(AssetType::kScript,
    ScriptKey(), bytes, pak.ReadAssetReferences(ScriptKey())));
  const auto descriptor = oxygen::serio::CopyAt<ScriptAssetDesc>(bytes);
  ASSERT_HAS_VALUE(descriptor);
  EXPECT_EQ(descriptor->flags, ScriptAssetFlags::kNone);
  EXPECT_EQ(descriptor->external_source_path_offset, 0U);
  EXPECT_NE(descriptor->source_resource_index, kNoResourceReference);
}

//! A script that cannot be sealed fails the command before it builds; the
//! report says why, and the earlier release stays.
NOLINT_TEST_F(
  PakToolCommandExecutionTest, SealingFailureWritesReportWithItsError)
{
  auto options = MakeExternalScriptOptions();
  WriteText(options.request.output_pak, "pak-old");

  auto& storage = RealCookerStorage();
  const auto result = ExecutePakToolCommand(BuildMode::kFull, "build",
    "Oxygen.Cooker.PakTool build", kToolVersion, options, storage);

  EXPECT_EQ(result.exit_code, PakToolExitCode::kPreparationFailure);
  EXPECT_EQ(ReadText(options.request.output_pak), "pak-old");
  const auto report
    = nlohmann::json::parse(ReadText(options.output.diagnostics_file));
  EXPECT_EQ(report.at("exit_code"), 2);
  EXPECT_EQ(report.at("error").at("code"), result.error_code);
  EXPECT_THAT(result.error_code, ::testing::StartsWith("paktool.script_seal."));
}

} // namespace
