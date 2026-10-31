//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/PakTool/PakToolCommands.cpp, Tools/PakTool/CommandExecution.cpp

#include <filesystem>
#include <sstream>
#include <vector>

#include "PakToolOptions.h"
#include "PakToolTestSupport.h"
#include "RequestPreparation.h"

#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Tools/PakTool/CommandExecution.h>
#include <Oxygen/Cooker/Tools/PakTool/PakToolCommands.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

using oxygen::content::cooked::RealCookerStorage;
using oxygen::content::pak::tool::PakToolExitCode;
using oxygen::content::pak::tool::RunPakToolApp;
using oxygen::content::pak::tool::test::WriteGamePak;
using oxygen::content::pak::tool::test::WriteLooseGameRoot;
using oxygen::cooker::test::ReadText;
using oxygen::cooker::test::WriteText;

constexpr auto kSourceKey = "01234567-89ab-7def-8123-456789abcdef";

class PakToolAppTest : public oxygen::testing::TempDirTest {
protected:
  [[nodiscard]] auto MakeArgv(const std::vector<std::string>& args)
    -> std::vector<char*>
  {
    storage_ = args;
    auto argv = std::vector<char*> {};
    argv.reserve(storage_.size());
    for (auto& arg : storage_) {
      argv.push_back(arg.data());
    }
    return argv;
  }

  //! A pak of the mount `Game`, for the tests that build from a pak source.
  auto SeedPak() -> std::filesystem::path
  {
    return WriteGamePak(TempDir() / "seed" / "seed.pak", "Seed");
  }

  //! A loose cooked root of the mount `Game`, for the tests that build from
  //! loose content.
  auto LooseSource() -> std::filesystem::path
  {
    return WriteLooseGameRoot(TempDir() / "loose", "Loose");
  }

private:
  std::vector<std::string> storage_;
};

NOLINT_TEST_F(
  PakToolAppTest, ParseFailureReturnsUsageExitCodeWithDeterministicCode)
{
  auto argv = MakeArgv({
    "Oxygen.Cooker.PakTool",
    "build",
    "--out",
    (TempDir() / "release" / "game.pak").string(),
  });

  auto& storage = RealCookerStorage();
  auto out = std::ostringstream {};
  auto err = std::ostringstream {};

  const auto exit_code = RunPakToolApp(argv, out, err, storage);

  EXPECT_EQ(exit_code, static_cast<int>(PakToolExitCode::kUsageError));
  EXPECT_TRUE(out.str().empty());
  EXPECT_THAT(err.str(), ::testing::HasSubstr("paktool.cli.parse_failed"));
  EXPECT_THAT(err.str(), ::testing::Not(::testing::HasSubstr("\x1b[")));
}

NOLINT_TEST_F(PakToolAppTest,
  PreparationFailureReturnsPreparationExitCodeWithRequestValidationCode)
{
  auto argv = MakeArgv({
    "Oxygen.Cooker.PakTool",
    "build",
    "--out",
    (TempDir() / "release" / "game.pak").string(),
    "--catalog-out",
    (TempDir() / "release" / "game.pakcatalog.json").string(),
    "--content-version",
    "42",
    "--source-key",
    "not-a-uuid",
  });

  auto& storage = RealCookerStorage();
  auto out = std::ostringstream {};
  auto err = std::ostringstream {};

  const auto exit_code = RunPakToolApp(argv, out, err, storage);

  EXPECT_EQ(exit_code, static_cast<int>(PakToolExitCode::kPreparationFailure));
  EXPECT_THAT(
    err.str(), ::testing::HasSubstr("paktool.prepare.invalid_source_key"));
  EXPECT_THAT(err.str(), ::testing::HasSubstr("[RequestValidation]"));
}

NOLINT_TEST_F(PakToolAppTest, CurrentPakInputPublishesWithoutWarnings)
{
  const auto seed_pak = SeedPak();
  auto argv = MakeArgv({
    "Oxygen.Cooker.PakTool",
    "build",
    "--pak-source",
    seed_pak.string(),
    "--out",
    (TempDir() / "release" / "warning_ok.pak").string(),
    "--catalog-out",
    (TempDir() / "release" / "warning_ok.pakcatalog.json").string(),
    "--content-version",
    "42",
    "--source-key",
    kSourceKey,
  });

  auto& storage = RealCookerStorage();
  auto out = std::ostringstream {};
  auto err = std::ostringstream {};

  const auto exit_code = RunPakToolApp(argv, out, err, storage);

  EXPECT_EQ(exit_code, static_cast<int>(PakToolExitCode::kSuccess));
  EXPECT_TRUE(err.str().empty());
  EXPECT_THAT(out.str(), ::testing::HasSubstr("paktool.publication"));
  EXPECT_THAT(out.str(), ::testing::HasSubstr("\x1b["));
}

NOLINT_TEST_F(PakToolAppTest, CurrentPakInputSucceedsWithFailOnWarnings)
{
  const auto seed_pak = SeedPak();
  auto argv = MakeArgv({
    "Oxygen.Cooker.PakTool",
    "build",
    "--pak-source",
    seed_pak.string(),
    "--out",
    (TempDir() / "release" / "warning_fail.pak").string(),
    "--catalog-out",
    (TempDir() / "release" / "warning_fail.pakcatalog.json").string(),
    "--content-version",
    "42",
    "--source-key",
    kSourceKey,
    "--fail-on-warnings",
  });

  auto& storage = RealCookerStorage();
  auto out = std::ostringstream {};
  auto err = std::ostringstream {};

  const auto exit_code = RunPakToolApp(argv, out, err, storage);

  EXPECT_EQ(exit_code, static_cast<int>(PakToolExitCode::kSuccess));
  EXPECT_TRUE(err.str().empty());
  EXPECT_THAT(out.str(), ::testing::HasSubstr("paktool.publication"));
}

NOLINT_TEST_F(PakToolAppTest, QuietSuppressesNonErrorOutput)
{
  auto argv = MakeArgv({
    "Oxygen.Cooker.PakTool",
    "build",
    "--out",
    (TempDir() / "release" / "quiet.pak").string(),
    "--catalog-out",
    (TempDir() / "release" / "quiet.pakcatalog.json").string(),
    "--content-version",
    "42",
    "--source-key",
    kSourceKey,
    "--loose-source",
    LooseSource().string(),
    "--quiet",
  });

  auto& storage = RealCookerStorage();
  auto out = std::ostringstream {};
  auto err = std::ostringstream {};

  const auto exit_code = RunPakToolApp(argv, out, err, storage);

  EXPECT_EQ(exit_code, static_cast<int>(PakToolExitCode::kSuccess));
  EXPECT_TRUE(out.str().empty());
  EXPECT_TRUE(err.str().empty());
}

NOLINT_TEST_F(PakToolAppTest, NoColorRemovesAnsiSequencesFromConsoleOutput)
{
  auto argv = MakeArgv({
    "Oxygen.Cooker.PakTool",
    "build",
    "--out",
    (TempDir() / "release" / "no_color.pak").string(),
    "--catalog-out",
    (TempDir() / "release" / "no_color.pakcatalog.json").string(),
    "--content-version",
    "42",
    "--source-key",
    kSourceKey,
    "--loose-source",
    LooseSource().string(),
    "--no-color",
  });

  auto& storage = RealCookerStorage();
  auto out = std::ostringstream {};
  auto err = std::ostringstream {};

  const auto exit_code = RunPakToolApp(argv, out, err, storage);

  EXPECT_EQ(exit_code, static_cast<int>(PakToolExitCode::kSuccess));
  EXPECT_THAT(out.str(), ::testing::Not(::testing::HasSubstr("\x1b[")));
  EXPECT_THAT(err.str(), ::testing::Not(::testing::HasSubstr("\x1b[")));
  EXPECT_THAT(out.str(), ::testing::HasSubstr("paktool.result"));
}

NOLINT_TEST_F(
  PakToolAppTest, PublishFailureReturnsRuntimeExitCodeWithFinalizeCode)
{
  const auto pak_path = TempDir() / "release" / "publish_fail.pak";
  const auto catalog_path
    = TempDir() / "release" / "publish_fail.pakcatalog.json";
  auto argv = MakeArgv({
    "Oxygen.Cooker.PakTool",
    "build",
    "--out",
    pak_path.string(),
    "--catalog-out",
    catalog_path.string(),
    "--content-version",
    "42",
    "--source-key",
    kSourceKey,
    "--loose-source",
    LooseSource().string(),
  });

  // The earlier catalog cannot be moved aside: a folder holds its backup.
  WriteText(catalog_path, "catalog-old");
  std::filesystem::create_directories(
    catalog_path.parent_path() / "publish_fail.pakcatalog.json.previous");

  auto& storage = RealCookerStorage();
  auto out = std::ostringstream {};
  auto err = std::ostringstream {};

  const auto exit_code = RunPakToolApp(argv, out, err, storage);

  EXPECT_EQ(exit_code, static_cast<int>(PakToolExitCode::kRuntimeFailure));
  EXPECT_THAT(
    err.str(), ::testing::HasSubstr("paktool.publish.backup_rename_failed"));
  EXPECT_THAT(err.str(), ::testing::HasSubstr("[Finalize]"));
  EXPECT_FALSE(std::filesystem::exists(pak_path));
  EXPECT_EQ(ReadText(catalog_path), "catalog-old");
}

} // namespace
