//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/PakTool/App.cpp, Tools/PakTool/CommandExecution.cpp

#include <filesystem>
#include <sstream>
#include <system_error>
#include <vector>

#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Cooker/Tools/PakTool/App.h>
#include <Oxygen/Cooker/Tools/PakTool/CommandExecution.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::pak::BuildMode;
using oxygen::content::pak::tool::ExecutePakToolCommand;
using oxygen::content::pak::tool::IArtifactFileSystem;
using oxygen::content::pak::tool::MakeArtifactPublicationPlan;
using oxygen::content::pak::tool::PakToolCliOptions;
using oxygen::content::pak::tool::PakToolExitCode;
using oxygen::content::pak::tool::RealArtifactFileSystem;
using oxygen::content::pak::tool::RealRequestPreparationFileSystem;
using oxygen::content::pak::tool::RunPakToolApp;

constexpr auto kSourceKey = "01234567-89ab-7def-8123-456789abcdef";
constexpr auto kToolVersion = "0.1";

class FailingPublishFileSystem final : public IArtifactFileSystem {
public:
  explicit FailingPublishFileSystem(std::filesystem::path fail_target)
    : fail_target_(std::move(fail_target))
  {
  }

  [[nodiscard]] auto Exists(const std::filesystem::path& path) const
    -> bool override
  {
    return std::filesystem::exists(path);
  }

  auto CreateDirectories(const std::filesystem::path& path)
    -> std::error_code override
  {
    auto ec = std::error_code {};
    if (!path.empty()) {
      std::filesystem::create_directories(path, ec);
    }
    return ec;
  }

  auto RemoveFile(const std::filesystem::path& path) -> std::error_code override
  {
    auto ec = std::error_code {};
    if (!path.empty()) {
      std::filesystem::remove(path, ec);
    }
    return ec;
  }

  auto Rename(const std::filesystem::path& from,
    const std::filesystem::path& to) -> std::error_code override
  {
    if (to == fail_target_ && from.filename().string().ends_with(".staged")) {
      return std::make_error_code(std::errc::permission_denied);
    }

    auto ec = std::error_code {};
    std::filesystem::rename(from, to, ec);
    return ec;
  }

private:
  std::filesystem::path fail_target_;
};

class PakToolAppTest : public oxygen::cooker::test::TempDirTest {
protected:
  [[nodiscard]] auto MakeBaseOptions() const -> PakToolCliOptions
  {
    auto options = PakToolCliOptions {};
    options.request.content_version = 42;
    options.request.source_key = kSourceKey;
    return options;
  }

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

  auto SeedPak() -> std::filesystem::path
  {
    auto options = MakeBaseOptions();
    options.request.output_pak = TempDir() / "seed" / "seed.pak";
    options.request.catalog_output
      = TempDir() / "seed" / "seed.pakcatalog.json";

    auto prep_fs = RealRequestPreparationFileSystem {};
    auto artifact_fs = RealArtifactFileSystem {};
    const auto result = ExecutePakToolCommand(BuildMode::kFull, "build",
      "Oxygen.Cooker.PakTool build", kToolVersion, options, prep_fs,
      artifact_fs);
    EXPECT_EQ(result.exit_code, PakToolExitCode::kSuccess)
      << result.error_code << ": " << result.error_message;
    return options.request.output_pak;
  }

private:
  std::vector<std::string> storage_ {};
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

  auto prep_fs = RealRequestPreparationFileSystem {};
  auto artifact_fs = RealArtifactFileSystem {};
  auto out = std::ostringstream {};
  auto err = std::ostringstream {};

  const auto exit_code = RunPakToolApp(argv, out, err, prep_fs, artifact_fs);

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

  auto prep_fs = RealRequestPreparationFileSystem {};
  auto artifact_fs = RealArtifactFileSystem {};
  auto out = std::ostringstream {};
  auto err = std::ostringstream {};

  const auto exit_code = RunPakToolApp(argv, out, err, prep_fs, artifact_fs);

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

  auto prep_fs = RealRequestPreparationFileSystem {};
  auto artifact_fs = RealArtifactFileSystem {};
  auto out = std::ostringstream {};
  auto err = std::ostringstream {};

  const auto exit_code = RunPakToolApp(argv, out, err, prep_fs, artifact_fs);

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

  auto prep_fs = RealRequestPreparationFileSystem {};
  auto artifact_fs = RealArtifactFileSystem {};
  auto out = std::ostringstream {};
  auto err = std::ostringstream {};

  const auto exit_code = RunPakToolApp(argv, out, err, prep_fs, artifact_fs);

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
    "--quiet",
  });

  auto prep_fs = RealRequestPreparationFileSystem {};
  auto artifact_fs = RealArtifactFileSystem {};
  auto out = std::ostringstream {};
  auto err = std::ostringstream {};

  const auto exit_code = RunPakToolApp(argv, out, err, prep_fs, artifact_fs);

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
    "--no-color",
  });

  auto prep_fs = RealRequestPreparationFileSystem {};
  auto artifact_fs = RealArtifactFileSystem {};
  auto out = std::ostringstream {};
  auto err = std::ostringstream {};

  const auto exit_code = RunPakToolApp(argv, out, err, prep_fs, artifact_fs);

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
  });

  auto prep_fs = RealRequestPreparationFileSystem {};
  auto artifact_fs = FailingPublishFileSystem(catalog_path);
  auto out = std::ostringstream {};
  auto err = std::ostringstream {};

  const auto exit_code = RunPakToolApp(argv, out, err, prep_fs, artifact_fs);

  EXPECT_EQ(exit_code, static_cast<int>(PakToolExitCode::kRuntimeFailure));
  EXPECT_THAT(err.str(), ::testing::HasSubstr("paktool.publish.rename_failed"));
  EXPECT_THAT(err.str(), ::testing::HasSubstr("[Finalize]"));
}

} // namespace
