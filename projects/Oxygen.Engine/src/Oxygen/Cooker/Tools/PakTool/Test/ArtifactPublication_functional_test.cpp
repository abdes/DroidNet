//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/PakTool/ArtifactPublication.cpp

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Cooker/Tools/PakTool/ArtifactPublication.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::pak::tool::ArtifactPublicationIntent;
using oxygen::content::pak::tool::IArtifactFileSystem;
using oxygen::content::pak::tool::MakeArtifactPublicationPlan;
using oxygen::content::pak::tool::PublishArtifacts;
using oxygen::content::pak::tool::RealArtifactFileSystem;
using oxygen::cooker::test::ReadText;
using oxygen::cooker::test::WriteText;

class PublishingFileSystem final : public IArtifactFileSystem {
public:
  explicit PublishingFileSystem(
    std::optional<std::filesystem::path> fail_publish_target = std::nullopt)
    : fail_publish_target_(std::move(fail_publish_target))
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
    if (fail_publish_target_.has_value() && to == *fail_publish_target_
      && from.filename().string().ends_with(".staged")
      && std::filesystem::exists(from)) {
      return std::make_error_code(std::errc::permission_denied);
    }

    auto ec = std::error_code {};
    std::filesystem::rename(from, to, ec);
    return ec;
  }

private:
  std::optional<std::filesystem::path> fail_publish_target_;
};

class PakToolArtifactPublicationTest
  : public oxygen::cooker::test::TempDirTest {
protected:
};

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsPromotesAllRequestedOutputsOnSuccess)
{
  const auto plan
    = MakeArtifactPublicationPlan(TempDir() / "release" / "game.pak",
      TempDir() / "release" / "game.catalog.json",
      TempDir() / "release" / "game.manifest.json",
      TempDir() / "release" / "game.report.json");

  WriteText(plan.pak.staged_path, "pak-new");
  WriteText(plan.catalog.staged_path, "catalog-new");
  WriteText(plan.manifest->staged_path, "manifest-new");
  WriteText(plan.report->staged_path, "report-new");

  auto intent = ArtifactPublicationIntent {
    .create_parent_directories = true,
    .publish_pak = true,
    .publish_catalog = true,
    .publish_manifest = true,
    .publish_report = true,
  };
  auto fs = RealArtifactFileSystem {};

  const auto result = PublishArtifacts(plan, intent, fs);

  EXPECT_TRUE(result.success)
    << result.error_code << ": " << result.error_message;
  EXPECT_TRUE(std::filesystem::exists(plan.pak.final_path));
  EXPECT_TRUE(std::filesystem::exists(plan.catalog.final_path));
  EXPECT_TRUE(std::filesystem::exists(plan.manifest->final_path));
  EXPECT_TRUE(std::filesystem::exists(plan.report->final_path));
  EXPECT_EQ(ReadText(plan.pak.final_path), "pak-new");
  EXPECT_EQ(ReadText(plan.catalog.final_path), "catalog-new");
  EXPECT_EQ(ReadText(plan.manifest->final_path), "manifest-new");
  EXPECT_EQ(ReadText(plan.report->final_path), "report-new");
  EXPECT_FALSE(std::filesystem::exists(plan.pak.staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.catalog.staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.manifest->staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.report->staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.pak.backup_path));
  EXPECT_FALSE(std::filesystem::exists(plan.catalog.backup_path));
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsSuppressesStaleAuthoritativeSidecarsOnBuildFailure)
{
  const auto plan
    = MakeArtifactPublicationPlan(TempDir() / "release" / "game.pak",
      TempDir() / "release" / "game.catalog.json",
      TempDir() / "release" / "game.manifest.json",
      TempDir() / "release" / "game.report.json");

  WriteText(plan.pak.final_path, "pak-old");
  WriteText(plan.catalog.final_path, "catalog-old");
  WriteText(plan.manifest->final_path, "manifest-old");

  WriteText(plan.pak.staged_path, "pak-new");
  WriteText(plan.catalog.staged_path, "catalog-new");
  WriteText(plan.manifest->staged_path, "manifest-new");
  WriteText(plan.report->staged_path, "report-failure");

  auto intent = ArtifactPublicationIntent {
    .create_parent_directories = true,
    .publish_pak = false,
    .publish_catalog = false,
    .publish_manifest = false,
    .publish_report = true,
    .suppress_stale_catalog_on_skip = true,
    .suppress_stale_manifest_on_skip = true,
  };
  auto fs = RealArtifactFileSystem {};

  const auto result = PublishArtifacts(plan, intent, fs);

  EXPECT_TRUE(result.success)
    << result.error_code << ": " << result.error_message;
  EXPECT_TRUE(std::filesystem::exists(plan.pak.final_path));
  EXPECT_EQ(ReadText(plan.pak.final_path), "pak-old");
  EXPECT_FALSE(std::filesystem::exists(plan.catalog.final_path));
  EXPECT_FALSE(std::filesystem::exists(plan.manifest->final_path));
  EXPECT_TRUE(std::filesystem::exists(plan.report->final_path));
  EXPECT_EQ(ReadText(plan.report->final_path), "report-failure");
  EXPECT_FALSE(std::filesystem::exists(plan.pak.staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.catalog.staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.manifest->staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.report->staged_path));
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsRollsBackEarlierPromotionsOnPublishFailure)
{
  const auto plan
    = MakeArtifactPublicationPlan(TempDir() / "release" / "game.pak",
      TempDir() / "release" / "game.catalog.json",
      TempDir() / "release" / "game.manifest.json",
      TempDir() / "release" / "game.report.json");

  WriteText(plan.pak.final_path, "pak-old");
  WriteText(plan.catalog.final_path, "catalog-old");
  WriteText(plan.manifest->final_path, "manifest-old");

  WriteText(plan.pak.staged_path, "pak-new");
  WriteText(plan.catalog.staged_path, "catalog-new");
  WriteText(plan.manifest->staged_path, "manifest-new");
  WriteText(plan.report->staged_path, "report-new");

  auto intent = ArtifactPublicationIntent {
    .create_parent_directories = true,
    .publish_pak = true,
    .publish_catalog = true,
    .publish_manifest = true,
    .publish_report = true,
  };
  auto fs = PublishingFileSystem(plan.manifest->final_path);

  const auto result = PublishArtifacts(plan, intent, fs);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, "paktool.publish.rename_failed");
  EXPECT_EQ(ReadText(plan.pak.final_path), "pak-old");
  EXPECT_EQ(ReadText(plan.catalog.final_path), "catalog-old");
  EXPECT_EQ(ReadText(plan.manifest->final_path), "manifest-old");
  EXPECT_FALSE(std::filesystem::exists(plan.report->final_path));
  EXPECT_FALSE(std::filesystem::exists(plan.pak.staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.catalog.staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.manifest->staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.report->staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.pak.backup_path));
  EXPECT_FALSE(std::filesystem::exists(plan.catalog.backup_path));
  EXPECT_FALSE(std::filesystem::exists(plan.manifest->backup_path));
}

} // namespace
