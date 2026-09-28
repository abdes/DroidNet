//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <ios>
#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <system_error>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Import/MaterialSlotProvenance.h>
#include <Oxygen/Cooker/Import/RetainedModelImport.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::RetainedModelImport;

class RetainedModelImportTest : public testing::Test {
protected:
  void SetUp() override
  {
    root_ = std::filesystem::temp_directory_path()
      / ("oxygen-retained-" + oxygen::Uuid::Generate().ToString());
    std::filesystem::create_directory(root_);
    settings_.source_path = (root_ / "source.gltf").string();
    std::ofstream source(settings_.source_path);
    source
      << R"({"asset":{"version":"2.0"},"nodes":[{"name":"Root"}],"scenes":[{"nodes":[0]}],"scene":0})";
    record_ = root_ / "imports" / "source.import.json";
    SaveRecipe();
  }
  void TearDown() override
  {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }
  void SaveRecipe()
  {
    RetainedModelImport::SaveRecipe(
      record_, root_, RetainedModelImport::MakeRecipe(settings_, {}));
  }
  static auto Candidate(const RetainedModelImport& attempt)
    -> oxygen::content::import::ImportReport
  {
    const auto& request = attempt.Request();
    if (!request.cooked_root || !request.source_key) {
      throw std::runtime_error("Expected retained generation identity");
    }
    const auto cooked_root = *request.cooked_root;
    const auto source_key = *request.source_key;
    oxygen::content::import::LooseCookedWriter writer(cooked_root);
    writer.SetSourceKey(source_key);
    static_cast<void>(writer.Finish());
    auto report = oxygen::content::import::ImportReport {};
    report.cooked_root = cooked_root;
    report.source_key = source_key;
    report.material_slot_provenance_json
      = attempt.Request().material_slot_provenance->Serialize();
    report.success = true;
    return report;
  }
  std::filesystem::path root_;
  std::filesystem::path record_;
  oxygen::content::import::SceneImportSettings settings_;
};

NOLINT_TEST_F(RetainedModelImportTest,
  RecipeFileResolvesRelativeSourceAndKeepsTextureProfile)
{
  const auto recipe_path = root_ / "recipe.json";
  {
    std::ofstream recipe(recipe_path);
    recipe << R"({"version":1,"defaults":{"texture":{
      "output_format":"bc7_srgb","data_format":"bc7","mip_policy":"full",
      "mip_filter":"kaiser","bc7_quality":"high"}},
      "jobs":[{"type":"gltf","source":"source.gltf","normals_policy":"generate"}]})";
  }
  const auto before = RetainedModelImport::Prepare(record_);
  RetainedModelImport::SaveRecipeFile(
    record_, root_, recipe_path, oxygen::content::import::ImportFormat::kGltf);
  const auto after = RetainedModelImport::Prepare(record_);
  EXPECT_EQ(
    after->Request().source_path, std::filesystem::path(settings_.source_path));
  EXPECT_EQ(after->Request().material_slot_provenance->SourceIdentity(),
    before->Request().material_slot_provenance->SourceIdentity());
  EXPECT_TRUE(after->Request().options.texture_tuning.enabled);
  EXPECT_EQ(after->Request().options.texture_tuning.color_output_format,
    oxygen::Format::kBC7UNormSRGB);
  EXPECT_EQ(after->Request().options.texture_tuning.data_output_format,
    oxygen::Format::kBC7UNorm);
}

NOLINT_TEST_F(
  RetainedModelImportTest, WrongRecipeFormatCannotMutateRetainedRecord)
{
  const auto recipe_path = root_ / "recipe.json";
  {
    std::ofstream recipe(recipe_path);
    recipe << RetainedModelImport::MakeRecipe(settings_, {});
  }
  const auto before = oxygen::base::ComputeFileSha256(record_);
  EXPECT_THROW(RetainedModelImport::SaveRecipeFile(record_, root_, recipe_path,
                 oxygen::content::import::ImportFormat::kFbx),
    std::invalid_argument);
  EXPECT_EQ(oxygen::base::ComputeFileSha256(record_), before);
}

NOLINT_TEST_F(
  RetainedModelImportTest, SelectsOnlyValidatedGenerationAndRetainsIdentity)
{
  auto attempt = RetainedModelImport::Prepare(record_);
  EXPECT_FALSE(RetainedModelImport::SelectedGeneration(record_));
  auto report = Candidate(*attempt);
  EXPECT_THROW(attempt->Publish(report), std::invalid_argument);
  attempt->ValidateCandidate(report);
  attempt->Publish(report);
  EXPECT_EQ(
    RetainedModelImport::SelectedGeneration(record_), report.cooked_root);
  EXPECT_EQ(report.retained_record_path, record_);
  auto next = RetainedModelImport::Prepare(record_);
  EXPECT_NE(next->Request().cooked_root, report.cooked_root);
  EXPECT_EQ(next->Request().material_slot_provenance->SourceIdentity(),
    attempt->Request().material_slot_provenance->SourceIdentity());
  EXPECT_NE(next->Request().source_key, attempt->Request().source_key);
}

NOLINT_TEST_F(
  RetainedModelImportTest, ConcurrentAttemptCannotOverwriteAcceptedGeneration)
{
  auto first = RetainedModelImport::Prepare(record_);
  auto second = RetainedModelImport::Prepare(record_);
  auto accepted = Candidate(*first);
  auto stale = Candidate(*second);
  first->ValidateCandidate(accepted);
  second->ValidateCandidate(stale);
  first->Publish(accepted);
  EXPECT_THROW(second->Publish(stale), std::runtime_error);
  EXPECT_EQ(
    RetainedModelImport::SelectedGeneration(record_), accepted.cooked_root);
  EXPECT_TRUE(
    std::filesystem::exists(accepted.cooked_root / "container.index.bin"));
}

NOLINT_TEST_F(
  RetainedModelImportTest, AuthoredSettingsChangeInvalidatesPendingCandidate)
{
  auto attempt = RetainedModelImport::Prepare(record_);
  auto report = Candidate(*attempt);
  attempt->ValidateCandidate(report);
  settings_.normals_policy = "recalculate";
  SaveRecipe();
  EXPECT_THROW(attempt->Publish(report), std::runtime_error);
  EXPECT_FALSE(RetainedModelImport::SelectedGeneration(record_));
}

NOLINT_TEST_F(
  RetainedModelImportTest, FailedOrCancelledAttemptKeepsSelectedGeneration)
{
  auto first = RetainedModelImport::Prepare(record_);
  auto accepted = Candidate(*first);
  first->ValidateCandidate(accepted);
  first->Publish(accepted);
  auto next = RetainedModelImport::Prepare(record_);
  auto failed = Candidate(*next);
  failed.success = false;
  EXPECT_THROW(next->ValidateCandidate(failed), std::invalid_argument);
  failed.success = true;
  next->ValidateCandidate(failed);
  std::stop_source cancel;
  cancel.request_stop();
  EXPECT_THROW(next->Publish(failed, cancel.get_token()), std::runtime_error);
  EXPECT_EQ(
    RetainedModelImport::SelectedGeneration(record_), accepted.cooked_root);
}

NOLINT_TEST_F(RetainedModelImportTest, NamedRecordIsReusedBySourceDiscovery)
{
  EXPECT_EQ(
    RetainedModelImport::RecordPath(root_, settings_.source_path), record_);
}

NOLINT_TEST_F(
  RetainedModelImportTest, RecordCannotBeWrittenBesideExternalSource)
{
  const auto outside = root_.parent_path() / "outside.import.json";
  EXPECT_THROW(RetainedModelImport::SaveRecipe(outside, root_,
                 RetainedModelImport::MakeRecipe(settings_, {})),
    std::invalid_argument);
  EXPECT_FALSE(std::filesystem::exists(outside));
}

NOLINT_TEST_F(
  RetainedModelImportTest, OrdinarySubmissionCannotOverwriteMarkedGeneration)
{
  auto attempt = RetainedModelImport::Prepare(record_);
  auto selected = Candidate(*attempt);
  attempt->ValidateCandidate(selected);
  attempt->Publish(selected);
  oxygen::content::import::AsyncImportService service(
    oxygen::content::import::AsyncImportService::Config {
      .thread_pool_size = 2,
    });
  std::promise<oxygen::content::import::ImportReport> completed;
  auto future = completed.get_future();
  const auto stop
    = oxygen::ScopeGuard([&service] noexcept -> void { service.Stop(); });
  ASSERT_TRUE(service.SubmitImport(
    attempt->Request(), [&completed](auto, const auto& report) -> auto {
      completed.set_value(report);
    }));
  ASSERT_EQ(
    future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  EXPECT_FALSE(future.get().success);
  EXPECT_EQ(
    RetainedModelImport::SelectedGeneration(record_), selected.cooked_root);
}

NOLINT_TEST_F(RetainedModelImportTest, PreparedGenerationCannotBeSubmittedTwice)
{
  auto attempt = RetainedModelImport::Prepare(record_);
  oxygen::content::import::AsyncImportService service(
    oxygen::content::import::AsyncImportService::Config {
      .thread_pool_size = 2,
    });
  const auto stop
    = oxygen::ScopeGuard([&service] noexcept -> void { service.Stop(); });
  ASSERT_TRUE(service.SubmitRetainedImport(attempt, nullptr));
  EXPECT_THROW(
    static_cast<void>(service.SubmitRetainedImport(attempt, nullptr)),
    std::logic_error);
}

NOLINT_TEST_F(
  RetainedModelImportTest, RemovingDerivedOutputPreservesRetainedSourceIdentity)
{
  auto attempt = RetainedModelImport::Prepare(record_);
  auto report = Candidate(*attempt);
  attempt->ValidateCandidate(report);
  attempt->Publish(report);
  const auto identity
    = attempt->Request().material_slot_provenance->SourceIdentity();
  std::filesystem::remove_all(report.cooked_root);
  auto recook = RetainedModelImport::Prepare(record_);
  EXPECT_EQ(
    recook->Request().material_slot_provenance->SourceIdentity(), identity);
  EXPECT_NE(recook->Request().source_key, attempt->Request().source_key);
}

NOLINT_TEST_F(
  RetainedModelImportTest, ReclamationKeepsSelectedAndLeasedGenerations)
{
  auto first = RetainedModelImport::Prepare(record_);
  auto before = Candidate(*first);
  first->ValidateCandidate(before);
  first->Publish(before);
  auto reader = oxygen::serio::FileLock::TryAcquire(
    before.cooked_root / oxygen::data::loose_cooked::kGenerationLeaseFileName,
    oxygen::serio::FileLockMode::kShared);
  ASSERT_TRUE(reader);
  auto next = RetainedModelImport::Prepare(record_);
  auto after = Candidate(*next);
  next->ValidateCandidate(after);
  next->Publish(after);
  EXPECT_EQ(RetainedModelImport::ReclaimUnusedGenerations(record_), 0U);
  EXPECT_TRUE(std::filesystem::exists(before.cooked_root));
  reader = oxygen::Ok(oxygen::serio::FileLock {});
  EXPECT_EQ(RetainedModelImport::ReclaimUnusedGenerations(record_), 1U);
  EXPECT_FALSE(std::filesystem::exists(before.cooked_root));
  EXPECT_TRUE(std::filesystem::exists(after.cooked_root));
}

NOLINT_TEST_F(RetainedModelImportTest, ReclamationWaitsForActiveAttemptLease)
{
  auto attempt = RetainedModelImport::Prepare(record_);
  const auto& cooked_root = attempt->Request().cooked_root;
  if (!cooked_root) {
    FAIL() << "Expected retained cooked root";
  }
  const auto abandoned = *cooked_root;
  EXPECT_EQ(RetainedModelImport::ReclaimUnusedGenerations(record_), 0U);
  attempt.reset();
  EXPECT_EQ(RetainedModelImport::ReclaimUnusedGenerations(record_), 1U);
  EXPECT_FALSE(std::filesystem::exists(abandoned));
}

NOLINT_TEST_F(RetainedModelImportTest, ChangedPublishedIndexRequiresRecook)
{
  auto attempt = RetainedModelImport::Prepare(record_);
  auto report = Candidate(*attempt);
  attempt->ValidateCandidate(report);
  attempt->Publish(report);
  {
    std::ofstream index(report.cooked_root / "container.index.bin",
      std::ios::app | std::ios::binary);
    index << "changed";
  }
  EXPECT_THROW(
    static_cast<void>(RetainedModelImport::SelectedGeneration(record_)),
    std::runtime_error);
}

NOLINT_TEST_F(
  RetainedModelImportTest, NativeServicePublishesCompleteRetainedRecord)
{
  auto attempt = RetainedModelImport::Prepare(record_);
  oxygen::content::import::AsyncImportService service(
    oxygen::content::import::AsyncImportService::Config {
      .thread_pool_size = 2,
    });
  std::promise<oxygen::content::import::ImportReport> completed;
  auto future = completed.get_future();
  const auto stop
    = oxygen::ScopeGuard([&service] noexcept -> void { service.Stop(); });
  ASSERT_TRUE(service.SubmitRetainedImport(
    attempt, [&completed](auto, const auto& report) -> auto {
      completed.set_value(report);
    }));
  ASSERT_EQ(
    future.wait_for(std::chrono::seconds(15)), std::future_status::ready);
  const auto report = future.get();
  ASSERT_TRUE(report.success);
  EXPECT_EQ(
    RetainedModelImport::SelectedGeneration(record_), report.cooked_root);
  EXPECT_FALSE(report.material_slot_provenance_json.empty());
}

NOLINT_TEST_F(
  RetainedModelImportTest, CancellingOneRetainedImportDoesNotCancelAnother)
{
  auto cancelled_attempt = RetainedModelImport::Prepare(record_);
  const auto other_record = root_ / "imports" / "other.import.json";
  RetainedModelImport::SaveRecipe(
    other_record, root_, RetainedModelImport::MakeRecipe(settings_, {}));
  auto other_attempt = RetainedModelImport::Prepare(other_record);
  oxygen::content::import::AsyncImportService service(
    oxygen::content::import::AsyncImportService::Config {
      .thread_pool_size = 2,
    });
  std::promise<oxygen::content::import::ImportReport> cancelled;
  std::promise<oxygen::content::import::ImportReport> completed;
  auto cancelled_future = cancelled.get_future();
  auto completed_future = completed.get_future();
  std::atomic_bool requested { false };
  const auto stop
    = oxygen::ScopeGuard([&service] noexcept -> void { service.Stop(); });
  ASSERT_TRUE(service.SubmitRetainedImport(
    cancelled_attempt,
    [&cancelled](
      auto, const auto& report) -> auto { cancelled.set_value(report); },
    [&service, &requested](const auto& progress) -> auto {
      if (progress.header.phase
          == oxygen::content::import::ImportPhase::kFinalizing
        && !requested.exchange(true)) {
        static_cast<void>(service.CancelJob(progress.header.job_id));
      }
    }));
  ASSERT_TRUE(service.SubmitRetainedImport(
    other_attempt, [&completed](auto, const auto& report) -> auto {
      completed.set_value(report);
    }));
  ASSERT_EQ(cancelled_future.wait_for(std::chrono::seconds(15)),
    std::future_status::ready);
  ASSERT_EQ(completed_future.wait_for(std::chrono::seconds(15)),
    std::future_status::ready);
  EXPECT_TRUE(requested);
  EXPECT_FALSE(cancelled_future.get().success);
  EXPECT_TRUE(completed_future.get().success);
  EXPECT_FALSE(RetainedModelImport::SelectedGeneration(record_));
  EXPECT_TRUE(RetainedModelImport::SelectedGeneration(other_record));
}

NOLINT_TEST_F(RetainedModelImportTest,
  NativeGenerationWritersIsolatePendingIoAndCancellation)
{
  namespace co = oxygen::co;
  oxygen::content::import::ImportEventLoop loop;
  auto first = oxygen::content::import::CreateAsyncFileWriter(loop);
  auto second = oxygen::content::import::CreateAsyncFileWriter(loop);
  const std::string data = "generation-bytes";
  const auto bytes = std::as_bytes(std::span(data.data(), data.size()));
  first->WriteAsync(root_ / "first.bin", bytes, {}, nullptr);
  ASSERT_GT(first->PendingCount(), 0U);
  first->CancelAll();
  bool other_succeeded = false;
  second->WriteAsync(root_ / "second.bin", bytes, {},
    [&other_succeeded](
      const auto& error, auto) -> auto { other_succeeded = !error.IsError(); });
  const auto results
    = co::Run(loop, co::AllOf(first->Flush(), second->Flush()));
  EXPECT_TRUE(std::get<0>(results));
  EXPECT_TRUE(std::get<1>(results));
  EXPECT_TRUE(other_succeeded);
  EXPECT_EQ(first->PendingCount(), 0U);
  EXPECT_EQ(second->PendingCount(), 0U);
}

} // namespace
