//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/PakTool/ArtifactPublication.cpp

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Tools/PakTool/ArtifactPublication.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::cooked::PathKind;
using oxygen::content::pak::tool::ArtifactPublicationIntent;
using oxygen::content::pak::tool::ArtifactPublicationPlan;
using oxygen::content::pak::tool::MakeArtifactPublicationPlan;
using oxygen::content::pak::tool::PublishArtifacts;
using oxygen::cooker::testing::MemoryFileSystem;

//! Publishes the staged artifacts of one release folder held in memory.
class PakToolArtifactPublicationTest : public ::testing::Test {
protected:
  //! The plan of `game.pak` and its catalog, manifest and report.
  [[nodiscard]] static auto Plan() -> ArtifactPublicationPlan
  {
    const auto release = std::filesystem::path("C:/PakToolRelease");
    return MakeArtifactPublicationPlan(release / "game.pak",
      release / "game.catalog.json", release / "game.manifest.json",
      release / "game.report.json");
  }

  //! Every artifact of `plan` requested.
  [[nodiscard]] static auto PublishAll() -> ArtifactPublicationIntent
  {
    return {
      .publish_pak = true,
      .publish_catalog = true,
      .publish_manifest = true,
      .publish_report = true,
    };
  }

  //! Stages every artifact of `plan` with the text `<kind>-new`.
  auto StageAll(const ArtifactPublicationPlan& plan) -> void
  {
    storage_.AddFile(plan.pak.staged_path, "pak-new");
    storage_.AddFile(plan.catalog.staged_path, "catalog-new");
    storage_.AddFile(plan.manifest->staged_path, "manifest-new");
    storage_.AddFile(plan.report->staged_path, "report-new");
  }

  //! Publishes the earlier release: pak, catalog and manifest, `<kind>-old`.
  auto PublishOld(const ArtifactPublicationPlan& plan) -> void
  {
    storage_.AddFile(plan.pak.final_path, "pak-old");
    storage_.AddFile(plan.catalog.final_path, "catalog-old");
    storage_.AddFile(plan.manifest->final_path, "manifest-old");
  }

  //! The text of the file `path`; empty when there is none.
  [[nodiscard]] auto Text(const std::filesystem::path& path) const
    -> std::string
  {
    auto text = std::string {};
    std::ranges::transform(storage_.Bytes(path), std::back_inserter(text),
      [](const std::byte b) -> char { return static_cast<char>(b); });
    return text;
  }

  [[nodiscard]] auto IsFile(const std::filesystem::path& path) const -> bool
  {
    return storage_.KindOf(path) == PathKind::kFile;
  }

  //! Expects no staged file and no backup left by the publication.
  auto ExpectNoLeftovers(const ArtifactPublicationPlan& plan) const -> void
  {
    for (const auto* paths :
      { &plan.pak, &plan.catalog, &*plan.manifest, &*plan.report }) {
      EXPECT_FALSE(IsFile(paths->staged_path)) << paths->staged_path;
      EXPECT_FALSE(IsFile(paths->backup_path)) << paths->backup_path;
    }
  }

  MemoryFileSystem storage_;
};

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  MakeArtifactPublicationPlanUsesDeterministicSiblingPaths)
{
  const auto plan = Plan();

  const auto release = std::filesystem::path("C:/PakToolRelease");
  EXPECT_EQ(plan.pak.staged_path, release / "game.pak.staged");
  EXPECT_EQ(plan.pak.backup_path, release / "game.pak.previous");
  ASSERT_HAS_VALUE(plan.manifest);
  EXPECT_EQ(plan.manifest->staged_path, release / "game.manifest.json.staged");
  ASSERT_HAS_VALUE(plan.report);
  EXPECT_EQ(plan.report->backup_path, release / "game.report.json.previous");
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsPromotesAllRequestedOutputsOnSuccess)
{
  const auto plan = Plan();
  StageAll(plan);

  const auto result = PublishArtifacts(plan, PublishAll(), storage_);

  EXPECT_TRUE(result.success)
    << result.error_code << ": " << result.error_message;
  EXPECT_EQ(Text(plan.pak.final_path), "pak-new");
  EXPECT_EQ(Text(plan.catalog.final_path), "catalog-new");
  EXPECT_EQ(Text(plan.manifest->final_path), "manifest-new");
  EXPECT_EQ(Text(plan.report->final_path), "report-new");
  EXPECT_TRUE(result.pak.published);
  EXPECT_TRUE(result.pak.cleaned_staged);
  ExpectNoLeftovers(plan);
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsReplacesEarlierReleaseAndDropsItsBackups)
{
  const auto plan = Plan();
  PublishOld(plan);
  StageAll(plan);

  const auto result = PublishArtifacts(plan, PublishAll(), storage_);

  EXPECT_TRUE(result.success)
    << result.error_code << ": " << result.error_message;
  EXPECT_EQ(Text(plan.pak.final_path), "pak-new");
  EXPECT_EQ(Text(plan.catalog.final_path), "catalog-new");
  EXPECT_EQ(Text(plan.manifest->final_path), "manifest-new");
  ExpectNoLeftovers(plan);
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsSuppressesStaleAuthoritativeSidecarsOnBuildFailure)
{
  const auto plan = Plan();
  PublishOld(plan);
  StageAll(plan);
  const auto intent = ArtifactPublicationIntent {
    .publish_pak = false,
    .publish_catalog = false,
    .publish_manifest = false,
    .publish_report = true,
    .suppress_stale_catalog_on_skip = true,
    .suppress_stale_manifest_on_skip = true,
  };

  const auto result = PublishArtifacts(plan, intent, storage_);

  EXPECT_TRUE(result.success)
    << result.error_code << ": " << result.error_message;
  EXPECT_EQ(Text(plan.pak.final_path), "pak-old");
  EXPECT_FALSE(IsFile(plan.catalog.final_path));
  EXPECT_FALSE(IsFile(plan.manifest->final_path));
  EXPECT_TRUE(result.catalog.removed_stale_final);
  EXPECT_EQ(Text(plan.report->final_path), "report-new");
  ExpectNoLeftovers(plan);
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsRollsBackEarlierPromotionsOnPublishFailure)
{
  const auto plan = Plan();
  PublishOld(plan);
  StageAll(plan);
  storage_.FailMoving(plan.manifest->staged_path,
    std::make_error_code(std::errc::permission_denied));

  const auto result = PublishArtifacts(plan, PublishAll(), storage_);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, "paktool.publish.rename_failed");
  EXPECT_THAT(result.error_message, ::testing::HasSubstr("manifest"));
  EXPECT_EQ(Text(plan.pak.final_path), "pak-old");
  EXPECT_EQ(Text(plan.catalog.final_path), "catalog-old");
  EXPECT_EQ(Text(plan.manifest->final_path), "manifest-old");
  EXPECT_TRUE(result.pak.restored_backup);
  EXPECT_TRUE(result.catalog.restored_backup);
  EXPECT_TRUE(result.issues.empty());
  EXPECT_FALSE(IsFile(plan.report->final_path));
  ExpectNoLeftovers(plan);
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsRemovesFirstPublicationsWhenNothingWasReplaced)
{
  const auto plan = Plan();
  StageAll(plan);
  storage_.FailMoving(plan.catalog.staged_path);

  const auto result = PublishArtifacts(plan, PublishAll(), storage_);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, "paktool.publish.rename_failed");
  EXPECT_FALSE(IsFile(plan.pak.final_path));
  EXPECT_FALSE(IsFile(plan.catalog.final_path));
  ExpectNoLeftovers(plan);
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsKeepsReleaseWhenItsBackupCannotBeMade)
{
  const auto plan = Plan();
  PublishOld(plan);
  StageAll(plan);
  storage_.FailMoving(plan.catalog.final_path);

  const auto result = PublishArtifacts(plan, PublishAll(), storage_);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, "paktool.publish.backup_rename_failed");
  EXPECT_EQ(Text(plan.pak.final_path), "pak-old");
  EXPECT_EQ(Text(plan.catalog.final_path), "catalog-old");
  EXPECT_EQ(Text(plan.manifest->final_path), "manifest-old");
  ExpectNoLeftovers(plan);
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsKeepsBackupThatCannotBeRestored)
{
  const auto plan = Plan();
  PublishOld(plan);
  StageAll(plan);
  storage_.FailMoving(plan.manifest->staged_path);
  storage_.FailMoving(plan.manifest->backup_path);

  const auto result = PublishArtifacts(plan, PublishAll(), storage_);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, "paktool.publish.rename_failed");
  EXPECT_EQ(Text(plan.pak.final_path), "pak-old");
  EXPECT_EQ(Text(plan.catalog.final_path), "catalog-old");
  EXPECT_FALSE(IsFile(plan.manifest->final_path));
  EXPECT_EQ(Text(plan.manifest->backup_path), "manifest-old");
  // The failed restore is reported, beside the failure that caused it.
  ASSERT_EQ(result.issues.size(), 1U);
  EXPECT_EQ(result.issues.front().code, "paktool.publish.restore_failed");
  EXPECT_THAT(result.issues.front().message,
    ::testing::HasSubstr(plan.manifest->backup_path.string()));
}

NOLINT_TEST_F(
  PakToolArtifactPublicationTest, PublishArtifactsRequiresStagedArtifacts)
{
  const auto plan = Plan();
  PublishOld(plan);
  storage_.AddFile(plan.pak.staged_path, "pak-new");

  const auto result = PublishArtifacts(plan, PublishAll(), storage_);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, "paktool.publish.staged_missing");
  EXPECT_EQ(Text(plan.pak.final_path), "pak-old");
  EXPECT_EQ(Text(plan.catalog.final_path), "catalog-old");
  ExpectNoLeftovers(plan);
}

} // namespace
