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
using oxygen::content::pak::tool::ArtifactPaths;
using oxygen::content::pak::tool::ArtifactPublicationIntent;
using oxygen::content::pak::tool::ArtifactPublicationPlan;
using oxygen::content::pak::tool::MakeArtifactPublicationPlan;
using oxygen::content::pak::tool::PublishArtifacts;
using oxygen::cooker::testing::MemoryFileSystem;

//! Publishes the staged artifacts of one release folder held in memory.
class PakToolArtifactPublicationTest : public ::testing::Test {
protected:
  //! Plans `game.pak` and its catalog, manifest and report.
  void SetUp() override
  {
    plan_ = MakeArtifactPublicationPlan({
      .pak = Release() / "game.pak",
      .catalog = Release() / "game.catalog.json",
      .manifest = Release() / "game.manifest.json",
      .report = Release() / "game.report.json",
    });
    ASSERT_HAS_VALUE(plan_.manifest);
    manifest_ = *plan_.manifest;
  }

  //! The release folder of the plan.
  [[nodiscard]] static auto Release() -> std::filesystem::path
  {
    return "C:/PakToolRelease";
  }

  //! Every artifact of `plan` requested.
  [[nodiscard]] static auto PublishAll() -> ArtifactPublicationIntent
  {
    return {
      .publish_pak = true,
      .publish_catalog = true,
      .publish_manifest = true,
    };
  }

  //! Stages every artifact of the plan with the text `<kind>-new`.
  auto StageAll() -> void
  {
    storage_.AddFile(plan_.pak.staged_path, "pak-new");
    storage_.AddFile(plan_.catalog.staged_path, "catalog-new");
    storage_.AddFile(manifest_.staged_path, "manifest-new");
  }

  //! Publishes the earlier release: pak, catalog and manifest, `<kind>-old`.
  auto PublishOld() -> void
  {
    storage_.AddFile(plan_.pak.final_path, "pak-old");
    storage_.AddFile(plan_.catalog.final_path, "catalog-old");
    storage_.AddFile(manifest_.final_path, "manifest-old");
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
  auto ExpectNoLeftovers() const -> void
  {
    for (const auto* paths : { &plan_.pak, &plan_.catalog, &manifest_ }) {
      EXPECT_FALSE(IsFile(paths->staged_path)) << paths->staged_path;
      EXPECT_FALSE(IsFile(paths->backup_path)) << paths->backup_path;
    }
  }

  MemoryFileSystem storage_;
  ArtifactPublicationPlan plan_ {};
  //! The plan's manifest, which it always has.
  ArtifactPaths manifest_ {};
};

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  MakeArtifactPublicationPlanUsesDeterministicSiblingPaths)
{
  const auto& plan = plan_;

  EXPECT_EQ(plan.pak.staged_path, Release() / "game.pak.staged");
  EXPECT_EQ(plan.pak.backup_path, Release() / "game.pak.previous");
  EXPECT_EQ(manifest_.staged_path, Release() / "game.manifest.json.staged");
  EXPECT_EQ(manifest_.backup_path, Release() / "game.manifest.json.previous");
  EXPECT_EQ(plan.report_path, Release() / "game.report.json");
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsPromotesAllRequestedOutputsOnSuccess)
{
  const auto& plan = plan_;
  StageAll();

  const auto result = PublishArtifacts(plan, PublishAll(), storage_);

  EXPECT_TRUE(result.success)
    << result.error_code << ": " << result.error_message;
  EXPECT_EQ(Text(plan.pak.final_path), "pak-new");
  EXPECT_EQ(Text(plan.catalog.final_path), "catalog-new");
  EXPECT_EQ(Text(manifest_.final_path), "manifest-new");
  EXPECT_TRUE(result.pak.published);
  EXPECT_TRUE(result.pak.cleaned_staged);
  ExpectNoLeftovers();
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsReplacesEarlierReleaseAndDropsItsBackups)
{
  const auto& plan = plan_;
  PublishOld();
  StageAll();

  const auto result = PublishArtifacts(plan, PublishAll(), storage_);

  EXPECT_TRUE(result.success)
    << result.error_code << ": " << result.error_message;
  EXPECT_EQ(Text(plan.pak.final_path), "pak-new");
  EXPECT_EQ(Text(plan.catalog.final_path), "catalog-new");
  EXPECT_EQ(Text(manifest_.final_path), "manifest-new");
  ExpectNoLeftovers();
}

//! A failed build publishes nothing: the earlier pak keeps its catalog and
//! manifest, and what the build staged goes.
NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsKeepsEarlierReleaseWholeWhenNothingIsPublished)
{
  const auto& plan = plan_;
  PublishOld();
  StageAll();
  constexpr auto kPublishNothing = ArtifactPublicationIntent {
    .publish_pak = false,
    .publish_catalog = false,
    .publish_manifest = false,
  };

  const auto result = PublishArtifacts(plan, kPublishNothing, storage_);

  EXPECT_TRUE(result.success)
    << result.error_code << ": " << result.error_message;
  EXPECT_EQ(Text(plan.pak.final_path), "pak-old");
  EXPECT_EQ(Text(plan.catalog.final_path), "catalog-old");
  EXPECT_EQ(Text(manifest_.final_path), "manifest-old");
  EXPECT_FALSE(result.pak.published);
  ExpectNoLeftovers();
}

//! A later artifact fails and the earlier pak cannot be put back: the new pak
//! stays, and its backup, the only copy left of the earlier pak, stays too.
NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsKeepsBackupOfEarlierPromotionThatCannotBeRestored)
{
  const auto& plan = plan_;
  PublishOld();
  StageAll();
  storage_.FailMoving(manifest_.staged_path);
  storage_.FailMoving(plan.pak.backup_path);

  const auto result = PublishArtifacts(plan, PublishAll(), storage_);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, "paktool.publish.rename_failed");
  EXPECT_EQ(Text(plan.pak.final_path), "pak-new");
  EXPECT_EQ(Text(plan.pak.backup_path), "pak-old");
  EXPECT_TRUE(result.pak.published);
  EXPECT_TRUE(result.pak.kept_backup);
  EXPECT_EQ(Text(plan.catalog.final_path), "catalog-old");
  EXPECT_FALSE(IsFile(plan.catalog.backup_path));
  EXPECT_TRUE(result.catalog.restored_backup);
  ASSERT_EQ(result.issues.size(), 1U);
  EXPECT_EQ(result.issues.front().code, "paktool.publish.restore_failed");
  EXPECT_THAT(result.issues.front().message,
    ::testing::HasSubstr(plan.pak.backup_path.string()));
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsRollsBackEarlierPromotionsOnPublishFailure)
{
  const auto& plan = plan_;
  PublishOld();
  StageAll();
  storage_.FailMoving(
    manifest_.staged_path, std::make_error_code(std::errc::permission_denied));

  const auto result = PublishArtifacts(plan, PublishAll(), storage_);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, "paktool.publish.rename_failed");
  EXPECT_THAT(result.error_message, ::testing::HasSubstr("manifest"));
  EXPECT_EQ(Text(plan.pak.final_path), "pak-old");
  EXPECT_EQ(Text(plan.catalog.final_path), "catalog-old");
  EXPECT_EQ(Text(manifest_.final_path), "manifest-old");
  EXPECT_TRUE(result.pak.restored_backup);
  EXPECT_TRUE(result.catalog.restored_backup);
  EXPECT_FALSE(result.pak.published);
  EXPECT_FALSE(result.catalog.published);
  EXPECT_TRUE(result.issues.empty());
  ExpectNoLeftovers();
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsRemovesFirstPublicationsWhenNothingWasReplaced)
{
  const auto& plan = plan_;
  StageAll();
  storage_.FailMoving(plan.catalog.staged_path);

  const auto result = PublishArtifacts(plan, PublishAll(), storage_);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, "paktool.publish.rename_failed");
  EXPECT_FALSE(IsFile(plan.pak.final_path));
  EXPECT_FALSE(IsFile(plan.catalog.final_path));
  ExpectNoLeftovers();
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsKeepsReleaseWhenItsBackupCannotBeMade)
{
  const auto& plan = plan_;
  PublishOld();
  StageAll();
  storage_.FailMoving(plan.catalog.final_path);

  const auto result = PublishArtifacts(plan, PublishAll(), storage_);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, "paktool.publish.backup_rename_failed");
  EXPECT_EQ(Text(plan.pak.final_path), "pak-old");
  EXPECT_EQ(Text(plan.catalog.final_path), "catalog-old");
  EXPECT_EQ(Text(manifest_.final_path), "manifest-old");
  ExpectNoLeftovers();
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsKeepsBackupThatCannotBeRestored)
{
  const auto& plan = plan_;
  PublishOld();
  StageAll();
  storage_.FailMoving(manifest_.staged_path);
  storage_.FailMoving(manifest_.backup_path);

  const auto result = PublishArtifacts(plan, PublishAll(), storage_);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, "paktool.publish.rename_failed");
  EXPECT_EQ(Text(plan.pak.final_path), "pak-old");
  EXPECT_EQ(Text(plan.catalog.final_path), "catalog-old");
  EXPECT_FALSE(IsFile(manifest_.final_path));
  EXPECT_EQ(Text(manifest_.backup_path), "manifest-old");
  // The failed restore is reported, beside the failure that caused it.
  ASSERT_EQ(result.issues.size(), 1U);
  EXPECT_EQ(result.issues.front().code, "paktool.publish.restore_failed");
  EXPECT_THAT(result.issues.front().message,
    ::testing::HasSubstr(manifest_.backup_path.string()));
}

NOLINT_TEST_F(
  PakToolArtifactPublicationTest, PublishArtifactsRequiresStagedArtifacts)
{
  const auto& plan = plan_;
  PublishOld();
  storage_.AddFile(plan.pak.staged_path, "pak-new");

  const auto result = PublishArtifacts(plan, PublishAll(), storage_);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, "paktool.publish.staged_missing");
  EXPECT_EQ(Text(plan.pak.final_path), "pak-old");
  EXPECT_EQ(Text(plan.catalog.final_path), "catalog-old");
  ExpectNoLeftovers();
}

} // namespace
