//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <filesystem>
#include <fstream>
#include <process.h>
#include <sstream>
#include <system_error>
#include <vector>

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

class PakToolAppTest : public testing::Test {
protected:
  void SetUp() override
  {
    static auto counter = std::atomic_uint64_t { 0 };
    const auto pid = static_cast<unsigned long long>(_getpid());
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    root_ = std::filesystem::temp_directory_path() / "oxygen_paktool_app"
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

  [[nodiscard]] auto MakeBaseOptions() const -> PakToolCliOptions
  {
    auto options = PakToolCliOptions {};
    options.request.content_version = 42;
    options.request.source_key = kSourceKey;
    return options;
  }

  static auto WriteTextFile(
    const std::filesystem::path& path, const std::string_view content) -> void
  {
    std::filesystem::create_directories(path.parent_path());
    auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << path.string();
    out << content;
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
    options.request.output_pak = Root() / "seed" / "seed.pak";
    options.request.catalog_output = Root() / "seed" / "seed.pakcatalog.json";

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
  std::filesystem::path root_;
  std::vector<std::string> storage_ {};
};

NOLINT_TEST_F(
  PakToolAppTest, ParseFailureReturnsUsageExitCodeWithDeterministicCode)
{
  auto argv = MakeArgv({
    "Oxygen.Cooker.PakTool",
    "build",
    "--out",
    (Root() / "release" / "game.pak").string(),
  });

  auto prep_fs = RealRequestPreparationFileSystem {};
  auto artifact_fs = RealArtifactFileSystem {};
  auto out = std::ostringstream {};
  auto err = std::ostringstream {};

  const auto exit_code = RunPakToolApp(argv, out, err, prep_fs, artifact_fs);

  EXPECT_EQ(exit_code, static_cast<int>(PakToolExitCode::kUsageError));
  EXPECT_TRUE(out.str().empty());
  EXPECT_NE(err.str().find("paktool.cli.parse_failed"), std::string::npos);
  EXPECT_EQ(err.str().find("\x1b["), std::string::npos);
}

NOLINT_TEST_F(PakToolAppTest,
  PreparationFailureReturnsPreparationExitCodeWithRequestValidationCode)
{
  auto argv = MakeArgv({
    "Oxygen.Cooker.PakTool",
    "build",
    "--out",
    (Root() / "release" / "game.pak").string(),
    "--catalog-out",
    (Root() / "release" / "game.pakcatalog.json").string(),
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
  EXPECT_NE(
    err.str().find("paktool.prepare.invalid_source_key"), std::string::npos);
  EXPECT_NE(err.str().find("[RequestValidation]"), std::string::npos);
}

NOLINT_TEST_F(PakToolAppTest, WarningWithoutFailOnWarningsReturnsSuccess)
{
  const auto seed_pak = SeedPak();
  auto argv = MakeArgv({
    "Oxygen.Cooker.PakTool",
    "build",
    "--pak-source",
    seed_pak.string(),
    "--out",
    (Root() / "release" / "warning_ok.pak").string(),
    "--catalog-out",
    (Root() / "release" / "warning_ok.pakcatalog.json").string(),
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
  EXPECT_NE(
    err.str().find("pak.plan.pak_source_regions_projected"), std::string::npos);
  EXPECT_NE(out.str().find("paktool.publication"), std::string::npos);
  EXPECT_NE(out.str().find("\x1b["), std::string::npos);
}

NOLINT_TEST_F(
  PakToolAppTest, FailOnWarningsReturnsBuildFailureExitCodeWithErrorDiagnostic)
{
  const auto seed_pak = SeedPak();
  auto argv = MakeArgv({
    "Oxygen.Cooker.PakTool",
    "build",
    "--pak-source",
    seed_pak.string(),
    "--out",
    (Root() / "release" / "warning_fail.pak").string(),
    "--catalog-out",
    (Root() / "release" / "warning_fail.pakcatalog.json").string(),
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

  EXPECT_EQ(exit_code, static_cast<int>(PakToolExitCode::kBuildFailure));
  EXPECT_NE(err.str().find("pak.request.fail_on_warnings"), std::string::npos);
  EXPECT_NE(out.str().find("build=failed"), std::string::npos);
}

NOLINT_TEST_F(PakToolAppTest, QuietSuppressesNonErrorOutput)
{
  auto argv = MakeArgv({
    "Oxygen.Cooker.PakTool",
    "build",
    "--out",
    (Root() / "release" / "quiet.pak").string(),
    "--catalog-out",
    (Root() / "release" / "quiet.pakcatalog.json").string(),
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
    (Root() / "release" / "no_color.pak").string(),
    "--catalog-out",
    (Root() / "release" / "no_color.pakcatalog.json").string(),
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
  EXPECT_EQ(out.str().find("\x1b["), std::string::npos);
  EXPECT_EQ(err.str().find("\x1b["), std::string::npos);
  EXPECT_NE(out.str().find("paktool.result"), std::string::npos);
}

NOLINT_TEST_F(
  PakToolAppTest, PublishFailureReturnsRuntimeExitCodeWithFinalizeCode)
{
  const auto pak_path = Root() / "release" / "publish_fail.pak";
  const auto catalog_path = Root() / "release" / "publish_fail.pakcatalog.json";
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
  EXPECT_NE(err.str().find("paktool.publish.rename_failed"), std::string::npos);
  EXPECT_NE(err.str().find("[Finalize]"), std::string::npos);
}

} // namespace
