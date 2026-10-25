//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/ModelImportRecord.cpp

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Import/ModelImportRecord.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Cooker/test/Support/TempDir.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::Uuid;
using oxygen::content::import::ImportDiagnostic;
using oxygen::content::import::ImportFormat;
using oxygen::content::import::ImportReport;
using oxygen::content::import::ImportSeverity;
using oxygen::content::import::ModelImportRecord;
using oxygen::content::import::ModelImportRecordLocation;
using oxygen::content::import::ModelPublishedNode;
using oxygen::content::import::ModelSourceFingerprint;
using oxygen::data::NodeId;

constexpr std::string_view kVirtualPath = "/.cooked/source.gltf";
constexpr std::string_view kModel
  = R"({"asset":{"version":"2.0"},"nodes":[{"name":"Root"}],"scenes":[{"nodes":[0]}],"scene":0})";

auto Codes(const ModelImportRecordLocation& location)
  -> std::vector<std::string>
{
  auto codes = std::vector<std::string> {};
  std::ranges::transform(
    location.diagnostics, std::back_inserter(codes), &ImportDiagnostic::code);
  return codes;
}

auto Fingerprint(const std::filesystem::path& file) -> ModelSourceFingerprint
{
  return {
    .size = std::filesystem::file_size(file),
    .sha256 = oxygen::base::ComputeFileSha256(file),
    .files = {},
  };
}

class ModelImportRecordTest : public oxygen::cooker::test::TempDirTest {
protected:
  void SetUp() override
  {
    source_ = TempDir() / "Models" / "source.gltf";
    WriteModel(source_);
    identity_ = Uuid::Generate();
    record_ = ModelImportRecord::PathFor(TempDir(), identity_);
    settings_.normals_policy = "generate";
    SaveRecord(record_, source_, kVirtualPath);
  }

  static auto WriteModel(const std::filesystem::path& path,
    const std::string_view text = kModel) -> void
  {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
  }

  auto SaveRecord(const std::filesystem::path& record,
    const std::filesystem::path& source, const std::string_view virtual_path)
    -> void
  {
    ModelImportRecord::Save({
      .record_path = record,
      .content_root = TempDir(),
      .source = source,
      .virtual_path = std::string(virtual_path),
      .settings_json = ModelImportRecord::MakeSettings(settings_),
    });
  }

  auto NewRecord(const std::filesystem::path& source,
    const std::string_view virtual_path) -> std::filesystem::path
  {
    const auto record = ModelImportRecord::PathFor(TempDir(), Uuid::Generate());
    SaveRecord(record, source, virtual_path);
    return record;
  }

  static auto Candidate(const ModelImportRecord& attempt) -> ImportReport
  {
    const auto& request = attempt.Request();
    if (!request.cooked_root || !request.source_key) {
      throw std::runtime_error("Expected an import generation identity");
    }
    const auto cooked_root = *request.cooked_root;
    const auto source_key = *request.source_key;
    oxygen::content::import::LooseCookedWriter writer(cooked_root);
    writer.SetSourceKey(source_key);
    static_cast<void>(writer.Finish());
    auto report = ImportReport {};
    report.cooked_root = cooked_root;
    report.source_key = source_key;
    report.surface_provenance_json
      = attempt.Request().surface_provenance->Serialize();
    report.success = true;
    return report;
  }

  //! Publishes an import that read the record's source as it is now.
  static auto PublishRead(const std::filesystem::path& record) -> ImportReport
  {
    auto attempt = ModelImportRecord::Prepare(record);
    auto report = Candidate(*attempt);
    report.source_fingerprint
      = Fingerprint(ModelImportRecord::Read(record).source);
    attempt->ValidateCandidate(report);
    attempt->Publish(report);
    return report;
  }

  std::filesystem::path source_;
  Uuid identity_;
  std::filesystem::path record_;
  oxygen::content::import::SceneImportSettings settings_;
};

//! A record is named by its source identity, keeps its source relative to
//! itself inside the content root, and reads back what was saved.
NOLINT_TEST_F(ModelImportRecordTest, RecordIsNamedByItsIdentityAndRoundTrips)
{
  const auto content = ModelImportRecord::Read(record_);
  const auto document = json::parse(std::ifstream(record_));

  EXPECT_EQ(record_.filename().string(),
    identity_.ToString() + std::string(ModelImportRecord::kRecordSuffix));
  EXPECT_EQ(content.source_identity, identity_);
  EXPECT_TRUE(std::filesystem::equivalent(content.source, source_));
  EXPECT_EQ(content.virtual_path, kVirtualPath);
  EXPECT_EQ(document.at("schema_version"), ModelImportRecord::kSchemaVersion);
  EXPECT_EQ(document.at("source"), "../Models/source.gltf");
  EXPECT_EQ(document.at("settings").at("normals_policy"), "generate");
  EXPECT_FALSE(document.at("surface_provenance").contains("source_identity"));
  EXPECT_FALSE(document.contains("source_fingerprint"));

  const auto renamed = TempDir() / "imports" / "renamed.import.json";
  std::filesystem::copy_file(record_, renamed);
  EXPECT_THROW(
    static_cast<void>(ModelImportRecord::Read(renamed)), std::invalid_argument);
}

//! A record's settings hold no job structure: the record owns the source and
//! the virtual path, the manifest the output.
NOLINT_TEST_F(ModelImportRecordTest, SettingsHoldNoJobStructure)
{
  for (const auto* key : { "source", "virtual_path", "output", "type" }) {
    EXPECT_THROW(ModelImportRecord::Save({
                   .record_path = record_,
                   .content_root = TempDir(),
                   .source = {},
                   .virtual_path = {},
                   .settings_json = json { { key, "x" } }.dump(),
                 }),
      std::invalid_argument)
      << key;
  }
}

//! One record per virtual path, as there is one per source identity.
NOLINT_TEST_F(ModelImportRecordTest, TwoRecordsWithOneVirtualPathAreRefused)
{
  const auto other = ModelImportRecord::PathFor(TempDir(), Uuid::Generate());
  EXPECT_THROW(SaveRecord(other, source_, kVirtualPath), std::invalid_argument);
  EXPECT_FALSE(std::filesystem::exists(other));
}

NOLINT_TEST_F(ModelImportRecordTest, RecordCannotBeWrittenOutsideItsFolder)
{
  const auto outside = TempDir() / (identity_.ToString() + ".import.json");
  EXPECT_THROW(
    SaveRecord(outside, source_, kVirtualPath), std::invalid_argument);
  EXPECT_FALSE(std::filesystem::exists(outside));
}

NOLINT_TEST_F(
  ModelImportRecordTest, RecipeFileResolvesRelativeSourceAndKeepsTextureProfile)
{
  const auto recipe_path = TempDir() / "Models" / "recipe.json";
  {
    std::ofstream recipe(recipe_path);
    recipe << R"({"version":1,
      "jobs":[{"type":"gltf","source":"source.gltf",
        "virtual_path":"/.cooked/source.gltf","normals_policy":"generate",
        "output_format":"bc7_srgb","data_format":"bc7","mip_policy":"full",
        "mip_filter":"kaiser","bc7_quality":"high"}]})";
  }
  const auto before = ModelImportRecord::Prepare(record_);
  ModelImportRecord::SaveRecipeFile(
    {
      .record_path = record_,
      .content_root = TempDir(),
      .source = {},
      .virtual_path = {},
      .settings_json = std::nullopt,
    },
    recipe_path, ImportFormat::kGltf);
  const auto after = ModelImportRecord::Prepare(record_);
  EXPECT_TRUE(
    std::filesystem::equivalent(after->Request().source_path, source_));
  EXPECT_EQ(after->Request().surface_provenance->SourceIdentity(),
    before->Request().surface_provenance->SourceIdentity());
  EXPECT_TRUE(after->Request().options.texture_tuning.enabled);
  EXPECT_EQ(after->Request().options.texture_tuning.color_output_format,
    oxygen::Format::kBC7UNormSRGB);
  EXPECT_EQ(after->Request().options.texture_tuning.data_output_format,
    oxygen::Format::kBC7UNorm);
}

NOLINT_TEST_F(ModelImportRecordTest, WrongRecipeFormatCannotMutateTheRecord)
{
  const auto recipe_path = TempDir() / "Models" / "recipe.json";
  {
    std::ofstream recipe(recipe_path);
    recipe << R"({"version":1,"jobs":[{"type":"gltf","source":"source.gltf",
      "virtual_path":"/.cooked/source.gltf"}]})";
  }
  const auto before = oxygen::base::ComputeFileSha256(record_);
  EXPECT_THROW(ModelImportRecord::SaveRecipeFile(
                 {
                   .record_path = record_,
                   .content_root = TempDir(),
                   .source = {},
                   .virtual_path = {},
                   .settings_json = std::nullopt,
                 },
                 recipe_path, ImportFormat::kFbx),
    std::invalid_argument);
  EXPECT_EQ(oxygen::base::ComputeFileSha256(record_), before);
}

//! A replay whose source is gone says so, and how to rebind it.
NOLINT_TEST_F(ModelImportRecordTest, MissingSourceOnReplayIsSourceMissing)
{
  std::filesystem::remove(source_);
  try {
    static_cast<void>(ModelImportRecord::Prepare(record_));
    FAIL() << "Expected import.source_missing";
  } catch (const std::runtime_error& error) {
    EXPECT_THAT(error.what(), testing::HasSubstr("import.source_missing"));
  }
}

//! The publication commits the import's provenance, published nodes and
//! fingerprint with the generation, and changes nothing else.
NOLINT_TEST_F(ModelImportRecordTest, PublishCommitsWhatTheImportEstablished)
{
  auto attempt = ModelImportRecord::Prepare(record_);
  auto report = Candidate(*attempt);
  report.published_nodes = {
    ModelPublishedNode {
      .anchor = "Root",
      .node_id = NodeId::FromStableIdentity("Root"),
    },
  };
  report.source_fingerprint = Fingerprint(source_);
  const auto settings_before = ModelImportRecord::Read(record_).settings_json;

  attempt->ValidateCandidate(report);
  attempt->Publish(report);

  const auto content = ModelImportRecord::Read(record_);
  EXPECT_EQ(content.published_nodes, report.published_nodes);
  EXPECT_EQ(content.source_fingerprint, report.source_fingerprint);
  EXPECT_EQ(content.settings_json, settings_before);
  EXPECT_EQ(content.source_identity, identity_);
  EXPECT_TRUE(
    std::filesystem::exists(report.cooked_root.parent_path() / "head.json"));
  EXPECT_FALSE(
    json::parse(std::ifstream(record_)).contains("published_generation"));
}

NOLINT_TEST_F(
  ModelImportRecordTest, SelectsOnlyValidatedGenerationAndRetainsIdentity)
{
  auto attempt = ModelImportRecord::Prepare(record_);
  EXPECT_FALSE(ModelImportRecord::SelectedGeneration(record_));
  auto report = Candidate(*attempt);
  EXPECT_THROW(attempt->Publish(report), std::invalid_argument);
  attempt->ValidateCandidate(report);
  attempt->Publish(report);
  EXPECT_EQ(ModelImportRecord::SelectedGeneration(record_), report.cooked_root);
  EXPECT_EQ(report.model_record_path, record_);
  auto next = ModelImportRecord::Prepare(record_);
  EXPECT_NE(next->Request().cooked_root, report.cooked_root);
  EXPECT_EQ(next->Request().surface_provenance->SourceIdentity(), identity_);
  EXPECT_NE(next->Request().source_key, attempt->Request().source_key);
}

NOLINT_TEST_F(
  ModelImportRecordTest, ConcurrentAttemptCannotOverwriteAcceptedGeneration)
{
  auto first = ModelImportRecord::Prepare(record_);
  auto second = ModelImportRecord::Prepare(record_);
  auto accepted = Candidate(*first);
  auto stale = Candidate(*second);
  first->ValidateCandidate(accepted);
  second->ValidateCandidate(stale);
  first->Publish(accepted);
  EXPECT_THROW(second->Publish(stale), std::runtime_error);
  EXPECT_EQ(
    ModelImportRecord::SelectedGeneration(record_), accepted.cooked_root);
  EXPECT_TRUE(
    std::filesystem::exists(accepted.cooked_root / "container.index.bin"));
}

NOLINT_TEST_F(ModelImportRecordTest, SettingsChangeInvalidatesPendingCandidate)
{
  auto attempt = ModelImportRecord::Prepare(record_);
  auto report = Candidate(*attempt);
  attempt->ValidateCandidate(report);
  settings_.normals_policy = "recalculate";
  SaveRecord(record_, source_, kVirtualPath);
  EXPECT_THROW(attempt->Publish(report), std::runtime_error);
  EXPECT_FALSE(ModelImportRecord::SelectedGeneration(record_));
}

//! A failed or cancelled import leaves the record and the selected
//! generation as they were.
NOLINT_TEST_F(
  ModelImportRecordTest, FailedOrCancelledAttemptLeavesTheRecordUnchanged)
{
  auto first = ModelImportRecord::Prepare(record_);
  auto accepted = Candidate(*first);
  first->ValidateCandidate(accepted);
  first->Publish(accepted);
  const auto before = oxygen::base::ComputeFileSha256(record_);
  auto next = ModelImportRecord::Prepare(record_);
  auto failed = Candidate(*next);
  failed.source_fingerprint = Fingerprint(source_);
  failed.success = false;
  EXPECT_THROW(next->ValidateCandidate(failed), std::invalid_argument);
  failed.success = true;
  next->ValidateCandidate(failed);
  std::stop_source cancel;
  cancel.request_stop();
  EXPECT_THROW(next->Publish(failed, cancel.get_token()), std::runtime_error);
  EXPECT_EQ(
    ModelImportRecord::SelectedGeneration(record_), accepted.cooked_root);
  EXPECT_EQ(oxygen::base::ComputeFileSha256(record_), before);
}

NOLINT_TEST_F(
  ModelImportRecordTest, OrdinarySubmissionCannotOverwriteMarkedGeneration)
{
  auto attempt = ModelImportRecord::Prepare(record_);
  auto selected = Candidate(*attempt);
  attempt->ValidateCandidate(selected);
  attempt->Publish(selected);
  oxygen::content::import::AsyncImportService service(
    oxygen::content::import::AsyncImportService::Config {
      .thread_pool_size = 2,
    });
  std::promise<ImportReport> completed;
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
    ModelImportRecord::SelectedGeneration(record_), selected.cooked_root);
}

NOLINT_TEST_F(ModelImportRecordTest, PreparedGenerationCannotBeSubmittedTwice)
{
  auto attempt = ModelImportRecord::Prepare(record_);
  oxygen::content::import::AsyncImportService service(
    oxygen::content::import::AsyncImportService::Config {
      .thread_pool_size = 2,
    });
  const auto stop
    = oxygen::ScopeGuard([&service] noexcept -> void { service.Stop(); });
  ASSERT_TRUE(service.SubmitModelImport(attempt, nullptr));
  EXPECT_THROW(static_cast<void>(service.SubmitModelImport(attempt, nullptr)),
    std::logic_error);
}

NOLINT_TEST_F(
  ModelImportRecordTest, RemovingDerivedOutputPreservesTheSourceIdentity)
{
  const auto report = PublishRead(record_);
  std::filesystem::remove_all(report.cooked_root.parent_path());
  auto recook = ModelImportRecord::Prepare(record_);
  EXPECT_EQ(recook->Request().surface_provenance->SourceIdentity(), identity_);
}

NOLINT_TEST_F(
  ModelImportRecordTest, ReclamationKeepsSelectedAndLeasedGenerations)
{
  auto first = ModelImportRecord::Prepare(record_);
  auto before = Candidate(*first);
  first->ValidateCandidate(before);
  first->Publish(before);
  auto reader = oxygen::serio::FileLock::TryAcquire(
    before.cooked_root / oxygen::data::loose_cooked::kGenerationLeaseFileName,
    oxygen::serio::FileLockMode::kShared);
  ASSERT_TRUE(reader);
  auto next = ModelImportRecord::Prepare(record_);
  auto after = Candidate(*next);
  next->ValidateCandidate(after);
  next->Publish(after);
  EXPECT_EQ(ModelImportRecord::ReclaimUnusedGenerations(record_), 0U);
  EXPECT_TRUE(std::filesystem::exists(before.cooked_root));
  reader = oxygen::Ok(oxygen::serio::FileLock {});
  EXPECT_EQ(ModelImportRecord::ReclaimUnusedGenerations(record_), 1U);
  EXPECT_FALSE(std::filesystem::exists(before.cooked_root));
  EXPECT_TRUE(std::filesystem::exists(after.cooked_root));
}

NOLINT_TEST_F(ModelImportRecordTest, ReclamationWaitsForActiveAttemptLease)
{
  auto attempt = ModelImportRecord::Prepare(record_);
  const auto& cooked_root = attempt->Request().cooked_root;
  if (!cooked_root) {
    FAIL() << "Expected a generation cooked root";
  }
  const auto abandoned = *cooked_root;
  EXPECT_EQ(ModelImportRecord::ReclaimUnusedGenerations(record_), 0U);
  attempt.reset();
  EXPECT_EQ(ModelImportRecord::ReclaimUnusedGenerations(record_), 1U);
  EXPECT_FALSE(std::filesystem::exists(abandoned));
}

NOLINT_TEST_F(ModelImportRecordTest, ChangedPublishedIndexRequiresRecook)
{
  auto attempt = ModelImportRecord::Prepare(record_);
  auto report = Candidate(*attempt);
  attempt->ValidateCandidate(report);
  attempt->Publish(report);
  {
    std::ofstream index(
      report.cooked_root / "container.index.bin", std::ios::app);
    index << "changed";
  }
  EXPECT_THROW(
    static_cast<void>(ModelImportRecord::SelectedGeneration(record_)),
    std::runtime_error);
}

//! A real import from the record commits the nodes it emitted and the bytes
//! it read.
NOLINT_TEST_F(ModelImportRecordTest, NativeServicePublishesTheCompleteRecord)
{
  auto attempt = ModelImportRecord::Prepare(record_);
  oxygen::content::import::AsyncImportService service(
    oxygen::content::import::AsyncImportService::Config {
      .thread_pool_size = 2,
    });
  std::promise<ImportReport> completed;
  auto future = completed.get_future();
  const auto stop
    = oxygen::ScopeGuard([&service] noexcept -> void { service.Stop(); });
  ASSERT_TRUE(service.SubmitModelImport(
    attempt, [&completed](auto, const auto& report) -> auto {
      completed.set_value(report);
    }));
  ASSERT_EQ(
    future.wait_for(std::chrono::seconds(15)), std::future_status::ready);
  const auto report = future.get();
  ASSERT_TRUE(report.success);
  EXPECT_EQ(ModelImportRecord::SelectedGeneration(record_), report.cooked_root);
  const auto content = ModelImportRecord::Read(record_);
  ASSERT_EQ(content.published_nodes.size(), 1U);
  EXPECT_EQ(content.published_nodes.front().anchor, "Root");
  EXPECT_EQ(content.source_fingerprint, Fingerprint(source_));
}

//! What finding the record decided is part of the import's diagnostics, once
//! in its report.
NOLINT_TEST_F(ModelImportRecordTest, LookupNotesReachTheImportsDiagnostics)
{
  auto attempt = ModelImportRecord::Prepare(record_,
    {
      ImportDiagnostic {
        .severity = ImportSeverity::kWarning,
        .code = "import.model_record_unreadable",
        .message = "Skipped an outdated record",
        .source_path = {},
        .object_path = {},
      },
    });
  oxygen::content::import::AsyncImportService service(
    oxygen::content::import::AsyncImportService::Config {
      .thread_pool_size = 2,
    });
  std::promise<ImportReport> completed;
  auto future = completed.get_future();
  const auto stop
    = oxygen::ScopeGuard([&service] noexcept -> void { service.Stop(); });
  ASSERT_TRUE(service.SubmitModelImport(
    attempt, [&completed](auto, const auto& report) -> auto {
      completed.set_value(report);
    }));
  ASSERT_EQ(
    future.wait_for(std::chrono::seconds(15)), std::future_status::ready);
  const auto report = future.get();

  ASSERT_TRUE(report.success);
  EXPECT_EQ(std::ranges::count(report.diagnostics,
              "import.model_record_unreadable", &ImportDiagnostic::code),
    1);
}

NOLINT_TEST_F(ModelImportRecordTest, CancellingOneImportDoesNotCancelAnother)
{
  auto cancelled_attempt = ModelImportRecord::Prepare(record_);
  const auto other_source = TempDir() / "Models" / "other.gltf";
  WriteModel(other_source);
  const auto other_record = NewRecord(other_source, "/.cooked/other.gltf");
  auto other_attempt = ModelImportRecord::Prepare(other_record);
  oxygen::content::import::AsyncImportService service(
    oxygen::content::import::AsyncImportService::Config {
      .thread_pool_size = 2,
    });
  std::promise<ImportReport> cancelled;
  std::promise<ImportReport> completed;
  auto cancelled_future = cancelled.get_future();
  auto completed_future = completed.get_future();
  std::atomic_bool requested { false };
  const auto stop
    = oxygen::ScopeGuard([&service] noexcept -> void { service.Stop(); });
  ASSERT_TRUE(service.SubmitModelImport(
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
  ASSERT_TRUE(service.SubmitModelImport(
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
  EXPECT_FALSE(ModelImportRecord::SelectedGeneration(record_));
  EXPECT_TRUE(ModelImportRecord::SelectedGeneration(other_record));
}

NOLINT_TEST_F(
  ModelImportRecordTest, NativeGenerationWritersIsolatePendingIoAndCancellation)
{
  namespace co = oxygen::co;
  oxygen::content::import::ImportEventLoop loop;
  auto first = oxygen::content::import::CreateAsyncFileWriter(loop);
  auto second = oxygen::content::import::CreateAsyncFileWriter(loop);
  const std::string data = "generation-bytes";
  const auto bytes = std::as_bytes(std::span(data.data(), data.size()));
  first->WriteAsync(TempDir() / "first.bin", bytes, {}, nullptr);
  ASSERT_GT(first->PendingCount(), 0U);
  first->CancelAll();
  bool other_succeeded = false;
  second->WriteAsync(TempDir() / "second.bin", bytes, {},
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

//=== LocateRecord ===--------------------------------------------------------//

//! The record of the model's own source and virtual path, unchanged.
NOLINT_TEST_F(ModelImportRecordTest, LocateFindsTheModelsRecord)
{
  const auto location = ModelImportRecord::LocateRecord({
    .content_root = TempDir(),
    .source = source_,
    .virtual_path = std::string(kVirtualPath),
  });
  EXPECT_EQ(location.record_path, record_);
  EXPECT_TRUE(location.exists);
  EXPECT_TRUE(location.diagnostics.empty());
}

//! A file in the records folder that is not a valid record, such as one of an
//! older format, is skipped with a warning that names it but never quotes the
//! document; the model's own record is still found and still imports.
NOLINT_TEST_F(ModelImportRecordTest, AnUnreadableRecordIsSkippedWithAWarning)
{
  const auto outdated = record_.parent_path() / "Outdated.import.json";
  {
    std::ofstream output(outdated);
    output
      << R"({"recipe":{"jobs":[]},"published_generation":{"id":"not-quoted"}})";
  }

  const auto location = ModelImportRecord::LocateRecord({
    .content_root = TempDir(),
    .source = source_,
    .virtual_path = std::string(kVirtualPath),
  });

  EXPECT_EQ(location.record_path, record_);
  EXPECT_TRUE(location.exists);
  ASSERT_EQ(location.diagnostics.size(), 1U);
  const auto& skipped = location.diagnostics.front();
  EXPECT_EQ(skipped.code, "import.model_record_unreadable");
  EXPECT_EQ(skipped.severity, ImportSeverity::kWarning);
  EXPECT_NE(skipped.message.find("Outdated.import.json"), std::string::npos);
  EXPECT_EQ(skipped.message.find("not-quoted"), std::string::npos);
  EXPECT_NO_THROW(static_cast<void>(ModelImportRecord::Prepare(record_)));
}

//! A moved source imported with the model's virtual path keeps its identity:
//! the record's source is rebound.
NOLINT_TEST_F(ModelImportRecordTest, AMovedSourceWithItsVirtualPathIsRebound)
{
  const auto moved = TempDir() / "Elsewhere" / "source.gltf";
  std::filesystem::create_directories(moved.parent_path());
  std::filesystem::rename(source_, moved);

  const auto location = ModelImportRecord::LocateRecord({
    .content_root = TempDir(),
    .source = moved,
    .virtual_path = std::string(kVirtualPath),
  });
  SaveRecord(location.record_path, moved, kVirtualPath);

  EXPECT_EQ(location.record_path, record_);
  EXPECT_THAT(Codes(location), testing::ElementsAre("import.source_rebound"));
  const auto content = ModelImportRecord::Read(record_);
  EXPECT_TRUE(std::filesystem::equivalent(content.source, moved));
  EXPECT_EQ(content.source_identity, identity_);
}

//! A source moved with its virtual path changed is recognised by its bytes,
//! once its original is gone.
NOLINT_TEST_F(
  ModelImportRecordTest, AMovedSourceWithIdenticalBytesKeepsItsIdentity)
{
  static_cast<void>(PublishRead(record_));
  const auto moved = TempDir() / "Props" / "renamed.gltf";
  std::filesystem::create_directories(moved.parent_path());
  std::filesystem::rename(source_, moved);

  const auto location = ModelImportRecord::LocateRecord({
    .content_root = TempDir(),
    .source = moved,
    .virtual_path = std::string("/.cooked/renamed.gltf"),
  });
  SaveRecord(location.record_path, moved, "/.cooked/renamed.gltf");

  EXPECT_EQ(location.record_path, record_);
  EXPECT_TRUE(location.exists);
  EXPECT_THAT(Codes(location), testing::ElementsAre("import.source_relocated"));
  const auto content = ModelImportRecord::Read(record_);
  EXPECT_EQ(content.virtual_path, "/.cooked/renamed.gltf");
  EXPECT_EQ(content.source_identity, identity_);
}

//! A copy imported with the original's virtual path becomes its source; the
//! original stays.
NOLINT_TEST_F(
  ModelImportRecordTest, ACopyWithTheOriginalsVirtualPathIsItsSource)
{
  const auto copy = TempDir() / "Models" / "copy.gltf";
  std::filesystem::copy_file(source_, copy);

  const auto location = ModelImportRecord::LocateRecord({
    .content_root = TempDir(),
    .source = copy,
    .virtual_path = std::string(kVirtualPath),
  });

  EXPECT_EQ(location.record_path, record_);
  EXPECT_THAT(Codes(location), testing::ElementsAre("import.source_rebound"));
  EXPECT_TRUE(std::filesystem::exists(source_));
}

//! A copy under a new virtual path is a new model, with a hint naming the
//! original.
NOLINT_TEST_F(ModelImportRecordTest, ACopyUnderANewVirtualPathIsANewModel)
{
  static_cast<void>(PublishRead(record_));
  const auto copy = TempDir() / "Models" / "copy.gltf";
  std::filesystem::copy_file(source_, copy);

  const auto location = ModelImportRecord::LocateRecord({
    .content_root = TempDir(),
    .source = copy,
    .virtual_path = std::string("/.cooked/copy.gltf"),
  });

  EXPECT_FALSE(location.exists);
  EXPECT_NE(location.record_path, record_);
  EXPECT_EQ(location.record_path.parent_path(), record_.parent_path());
  EXPECT_THAT(Codes(location), testing::ElementsAre("import.source_copy"));
}

//! The same file under another virtual path is another model from that file.
NOLINT_TEST_F(
  ModelImportRecordTest, AnotherVirtualPathOfTheSameFileIsAnotherModel)
{
  const auto location = ModelImportRecord::LocateRecord({
    .content_root = TempDir(),
    .source = source_,
    .virtual_path = std::string("/.cooked/second.gltf"),
  });
  SaveRecord(location.record_path, source_, "/.cooked/second.gltf");

  EXPECT_FALSE(location.exists);
  EXPECT_THAT(Codes(location), testing::ElementsAre("import.source_has_model"));
  EXPECT_TRUE(std::filesystem::equivalent(
    ModelImportRecord::Read(location.record_path).source, source_));
}

//! Several records the bytes could belong to: none is taken.
NOLINT_TEST_F(ModelImportRecordTest, AnAmbiguousRelocationTakesNoRecord)
{
  static_cast<void>(PublishRead(record_));
  const auto twin_source = TempDir() / "Models" / "twin.gltf";
  WriteModel(twin_source);
  const auto twin = NewRecord(twin_source, "/.cooked/twin.gltf");
  static_cast<void>(PublishRead(twin));
  const auto moved = TempDir() / "Props" / "moved.gltf";
  std::filesystem::create_directories(moved.parent_path());
  std::filesystem::rename(source_, moved);
  std::filesystem::remove(twin_source);

  const auto location = ModelImportRecord::LocateRecord({
    .content_root = TempDir(),
    .source = moved,
    .virtual_path = std::string("/.cooked/moved.gltf"),
  });

  EXPECT_FALSE(location.exists);
  EXPECT_THAT(
    Codes(location), testing::Contains("import.source_relocation_ambiguous"));
}

//! A record that lost its source to a file of the same name, changed, is
//! named as a candidate, with the command that rebinds it.
NOLINT_TEST_F(ModelImportRecordTest, AChangedMovedSourceIsARelocationCandidate)
{
  static_cast<void>(PublishRead(record_));
  const auto moved = TempDir() / "Props" / "source.gltf";
  WriteModel(moved,
    R"({"asset":{"version":"2.0"},"nodes":[{"name":"Other"}],"scenes":[{"nodes":[0]}],"scene":0})");
  std::filesystem::remove(source_);

  const auto location = ModelImportRecord::LocateRecord({
    .content_root = TempDir(),
    .source = moved,
    .virtual_path = std::string("/.cooked/props.gltf"),
  });

  EXPECT_FALSE(location.exists);
  ASSERT_THAT(Codes(location),
    testing::ElementsAre("import.source_relocation_candidate"));
  EXPECT_THAT(location.diagnostics.front().message,
    testing::HasSubstr("--record " + record_.generic_string()));
}

} // namespace
