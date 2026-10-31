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

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
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
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Serio/Integrity.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

using nlohmann::json;
using oxygen::Uuid;
using oxygen::content::import::ImportDiagnostic;
using oxygen::content::import::ImportFormat;
using oxygen::content::import::ImportReport;
using oxygen::content::import::ImportSeverity;
using oxygen::content::import::ModelImportGenerations;
using oxygen::content::import::ModelImportRecord;
using oxygen::content::import::ModelImportRecordLocation;
using oxygen::content::import::ModelPublishedNode;
using oxygen::content::import::ModelSourceFingerprint;
using oxygen::content::import::MountDeclaration;
using oxygen::content::import::MountDeclarationForm;
using oxygen::content::import::MountKind;
using oxygen::content::import::MountTable;
using oxygen::content::import::MountTableOwner;
using oxygen::content::import::MountTableRequest;
using oxygen::content::import::ResolveMountTable;
using oxygen::cooker::test::ModelImportRecordDocument;
using oxygen::data::NodeId;

//! The model of the fixture's record, beside its source.
constexpr std::string_view kIdentity = "/Content/Models/source.gltf";
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

auto FileDigest(const std::filesystem::path& file) -> oxygen::base::Sha256Digest
{
  const auto digest = oxygen::serio::ComputeFileSha256(file);
  if (!digest) {
    ADD_FAILURE() << digest.error().message();
    return {};
  }
  return *digest;
}

auto Fingerprint(const std::filesystem::path& file) -> ModelSourceFingerprint
{
  return {
    .size = std::filesystem::file_size(file),
    .sha256 = FileDigest(file),
    .files = {},
  };
}

class ModelImportRecordTest : public oxygen::testing::TempDirTest {
protected:
  void SetUp() override
  {
    mounts_ = ResolveMountTable(MountTableRequest {
      .project_root = TempDir(),
      .declarations = { MountDeclaration {
        .name = "Content", .kind = MountKind::kSource, .path = ".", }, },
      .form = MountDeclarationForm::kCommittedFile,
      .declared_in = "test",
      .working_directory = TempDir(),
      .mounts_option = std::nullopt,
      .owner = MountTableOwner::kProject,
    });
    source_ = TempDir() / "Models" / "source.gltf";
    WriteModel(source_);
    record_ = RecordOf(kIdentity);
    settings_.normals_policy = "generate";
    SaveRecord(record_, source_);
    identity_ = ModelImportRecord::Read(record_, mounts_).source_identity;
  }

  //! The record of the model `identity`, which need not exist.
  [[nodiscard]] auto RecordOf(const std::string_view identity) const
    -> std::filesystem::path
  {
    auto findings = std::vector<ImportDiagnostic> {};
    const auto path
      = ModelImportRecord::RecordPathOf(mounts_, identity, findings);
    if (!path.has_value()) {
      throw std::invalid_argument(
        "No record location for " + std::string(identity));
    }
    return *path;
  }

  //! Writes `document`, a hand-shaped record, at its model's record.
  [[nodiscard]] auto WriteRecord(
    const ModelImportRecordDocument& document) const -> std::filesystem::path
  {
    const auto path = RecordOf(document.Identity());
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << document.Text();
    return path;
  }

  static auto WriteModel(const std::filesystem::path& path,
    const std::string_view text = kModel) -> void
  {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
  }

  //! Saves `record` with `source` and the fixture's settings; returns
  //! `record`.
  auto SaveRecord(const std::filesystem::path& record,
    const std::filesystem::path& source) -> std::filesystem::path
  {
    ModelImportRecord::Save({
      .record_path = record,
      .source = source,
      .settings_json = ModelImportRecord::MakeSettings(settings_),
      .mounts = oxygen::make_observer(&mounts_),
    });
    return record;
  }

  //! Saves a new record of the model `identity` imported from `source`.
  auto NewRecord(const std::filesystem::path& source,
    const std::string_view identity) -> std::filesystem::path
  {
    return SaveRecord(RecordOf(identity), source);
  }

  static auto Candidate(const ModelImportRecord& attempt) -> ImportReport
  {
    const auto& request = attempt.Request();
    if (!request.cooked_root || !request.source_key) {
      throw std::runtime_error("Expected an import generation identity");
    }
    const auto cooked_root = *request.cooked_root;
    const auto source_key = *request.source_key;
    oxygen::content::import::LooseCookedWriter writer(
      cooked_root, oxygen::content::cooked::RealCookerStorage());
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
  auto PublishRead(const std::filesystem::path& record) -> ImportReport
  {
    auto attempt = ModelImportRecord::Prepare(record, Generations(), mounts_);
    auto report = Candidate(*attempt);
    report.source_fingerprint
      = Fingerprint(ModelImportRecord::Read(record, mounts_).source);
    attempt->ValidateCandidate(report);
    attempt->Publish(report);
    return report;
  }

  //! The generations folder the test owns, outside the content root's
  //! output folders.
  [[nodiscard]] auto Generations() const -> ModelImportGenerations
  {
    return ModelImportGenerations { .root = TempDir() / "Generations" };
  }

  MountTable mounts_;
  std::filesystem::path source_;
  Uuid identity_;
  std::filesystem::path record_;
  oxygen::content::import::SceneImportSettings settings_;
};

//! A record lives at its model's identity, names its source from itself, and
//! reads back what was saved; a file not named as a record is no record.
NOLINT_TEST_F(ModelImportRecordTest, RecordLivesAtItsIdentityAndRoundTrips)
{
  auto findings = std::vector<ImportDiagnostic> {};
  const auto misnamed = TempDir() / "Models" / "source.json";
  std::filesystem::copy_file(record_, misnamed);

  const auto content = ModelImportRecord::Read(record_, mounts_);
  const auto document = json::parse(std::ifstream(record_));
  const auto identity
    = ModelImportRecord::IdentityOf(mounts_, record_, findings);

  EXPECT_TRUE(std::filesystem::equivalent(record_,
    TempDir() / "Models"
      / ("source.gltf" + std::string(ModelImportRecord::kRecordSuffix))));
  EXPECT_EQ(identity, kIdentity);
  EXPECT_EQ(content.source_identity, identity_);
  EXPECT_TRUE(std::filesystem::equivalent(content.source, source_));
  EXPECT_EQ(content.source_mount_path, "/Content/Models/source.gltf");
  EXPECT_EQ(document.at("schema_version"), ModelImportRecord::kSchemaVersion);
  EXPECT_EQ(document.at("source"), "source.gltf");
  EXPECT_FALSE(document.contains("virtual_path"));
  EXPECT_EQ(document.at("settings").at("normals_policy"), "generate");
  EXPECT_FALSE(document.at("surface_provenance").contains("source_identity"));
  EXPECT_FALSE(document.contains("source_fingerprint"));
  EXPECT_THROW(static_cast<void>(ModelImportRecord::Read(misnamed, mounts_)),
    std::invalid_argument);
}

//! Save names a source elsewhere in the model's mount relative to the record,
//! whatever spelling it was given, and Read locates it back.
NOLINT_TEST_F(
  ModelImportRecordTest, SaveStoresTheSourceAsAReferenceFromTheRecord)
{
  const auto record = NewRecord(source_, "/Content/Props/ship.gltf");

  const auto document = json::parse(std::ifstream(record));
  const auto content = ModelImportRecord::Read(record, mounts_);

  EXPECT_EQ(document.at("source"), "../Models/source.gltf");
  EXPECT_EQ(content.source_mount_path, "/Content/Models/source.gltf");
  EXPECT_TRUE(std::filesystem::equivalent(content.source, source_));
}

//! A committed record never holds an absolute path: the record's schema
//! refuses a machine's path as its `source`.
NOLINT_TEST_F(ModelImportRecordTest, ReadRefusesAnAbsoluteSource)
{
  const auto record
    = WriteRecord(ModelImportRecordDocument(std::string(kIdentity), kIdentity)
        .Set("/source", source_.generic_string()));

  try {
    static_cast<void>(ModelImportRecord::Read(record, mounts_));
    FAIL() << "Expected the absolute source to be refused";
  } catch (const std::invalid_argument& error) {
    EXPECT_THAT(error.what(), testing::HasSubstr("source:"));
  }
}

//! A record's settings hold no job structure: the record owns the source and
//! the identity, the manifest the output.
NOLINT_TEST_F(ModelImportRecordTest, SettingsHoldNoJobStructure)
{
  for (const auto* key : { "source", "virtual_path", "output", "type" }) {
    EXPECT_THROW(ModelImportRecord::Save({
                   .record_path = record_,
                   .source = {},
                   .settings_json = json { { key, "x" } }.dump(),
                   .mounts = oxygen::make_observer(&mounts_),
                 }),
      std::invalid_argument)
      << key;
  }
}

//! One record per source identity: a copy of a record at another identity is
//! refused, and neither record changes.
NOLINT_TEST_F(ModelImportRecordTest, TwoRecordsWithOneSourceIdentityAreRefused)
{
  const auto copy = RecordOf("/Content/Models/other.gltf");
  std::filesystem::copy_file(record_, copy);
  const auto before = FileDigest(record_);

  EXPECT_THROW(SaveRecord(copy, source_), std::invalid_argument);

  EXPECT_EQ(FileDigest(record_), before);
  EXPECT_EQ(FileDigest(copy), before);
}

//! A new record is saved at its model's identity with a fresh UUIDv7 source
//! identity of its own.
NOLINT_TEST_F(
  ModelImportRecordTest, NewRecordIsSavedAtItsPlaceWithAFreshIdentity)
{
  const auto saved = NewRecord(source_, "/Content/Models/fresh.gltf");

  EXPECT_TRUE(std::filesystem::equivalent(saved,
    TempDir() / "Models"
      / ("fresh.gltf" + std::string(ModelImportRecord::kRecordSuffix))));
  const auto content = ModelImportRecord::Read(saved, mounts_);
  EXPECT_NE(content.source_identity, identity_);
  EXPECT_EQ(content.source_identity.ToString().at(14), '7');
  EXPECT_EQ(
    content.surface_provenance->SourceIdentity(), content.source_identity);
}

//! A record moved to another place is the record of the identity there: it
//! keeps its source identity and surface provenance, imports as that model,
//! and Save writes it where it now is.
NOLINT_TEST_F(ModelImportRecordTest, AMovedRecordIsTheRecordOfItsNewIdentity)
{
  const auto before = ModelImportRecord::Read(record_, mounts_);
  const auto moved = RecordOf("/Content/Models/renamed.gltf");
  std::filesystem::rename(record_, moved);

  settings_.normals_policy = "recalculate";
  SaveRecord(moved, source_);

  auto findings = std::vector<ImportDiagnostic> {};
  EXPECT_EQ(ModelImportRecord::IdentityOf(mounts_, moved, findings),
    "/Content/Models/renamed.gltf");
  EXPECT_FALSE(std::filesystem::exists(record_));
  const auto after = ModelImportRecord::Read(moved, mounts_);
  EXPECT_EQ(after.source_identity, identity_);
  EXPECT_EQ(after.surface_provenance->Serialize(),
    before.surface_provenance->Serialize());
  EXPECT_EQ(
    json::parse(after.settings_json).at("normals_policy"), "recalculate");
  EXPECT_EQ(ModelImportRecord::Prepare(moved, Generations(), mounts_)
              ->Request()
              .asset_virtual_path,
    "/Content/Models/renamed.gltf");
}

//! A record's lock exists only while it is held: after a save and an import,
//! the record's folder holds only the source and its record, and no lock file
//! remains anywhere.
NOLINT_TEST_F(ModelImportRecordTest, NoLockFileOutlivesItsLock)
{
  static_cast<void>(PublishRead(record_));

  for (const auto& entry :
    std::filesystem::recursive_directory_iterator(TempDir())) {
    if (entry.is_regular_file() && entry.path().extension() == ".lock") {
      EXPECT_EQ(entry.path().filename().string(),
        std::string(oxygen::data::loose_cooked::kGenerationLeaseFileName))
        << "only a generation's lease marker may remain: " << entry.path();
    }
  }
  for (const auto& entry :
    std::filesystem::directory_iterator(record_.parent_path())) {
    EXPECT_TRUE(std::filesystem::equivalent(entry.path(), source_)
      || std::filesystem::equivalent(entry.path(), record_))
      << entry.path();
  }
}

//! A record of `/Content/...` prepared without any manifest layout publishes
//! its generation with the products under `/Content/...`: the mount comes from
//! the identity.
NOLINT_TEST_F(
  ModelImportRecordTest, ARecordDerivesItsOutputMountFromItsIdentity)
{
  const auto attempt
    = ModelImportRecord::Prepare(record_, Generations(), mounts_);

  EXPECT_EQ(
    attempt->Request().asset_virtual_path, "/Content/Models/source.gltf");
  EXPECT_EQ(attempt->Request().OutputMountRoot(), "/Content");
  auto report = Candidate(*attempt);
  report.source_fingerprint = Fingerprint(source_);
  attempt->ValidateCandidate(report);
  attempt->Publish(report);
  EXPECT_EQ(ModelImportRecord::SelectedGeneration(record_, Generations()),
    report.cooked_root);
}

//! A record lives in its model's mount: Save refuses a record outside every
//! mount and a file not named as a record, and writes neither.
NOLINT_TEST_F(ModelImportRecordTest, RecordCannotBeWrittenOutsideAMount)
{
  const auto outside = TempDir().parent_path()
    / (identity_.ToString() + std::string(ModelImportRecord::kRecordSuffix));
  const auto misnamed = TempDir() / "Models" / "other.gltf.json";

  EXPECT_THROW(SaveRecord(outside, source_), std::invalid_argument);
  EXPECT_THROW(SaveRecord(misnamed, source_), std::invalid_argument);

  EXPECT_FALSE(std::filesystem::exists(outside));
  EXPECT_FALSE(std::filesystem::exists(misnamed));
}

NOLINT_TEST_F(
  ModelImportRecordTest, RecipeFileResolvesRelativeSourceAndKeepsTextureProfile)
{
  const auto recipe_path = TempDir() / "Models" / "recipe.json";
  {
    std::ofstream recipe(recipe_path);
    recipe << R"({"version":1,
      "jobs":[{"type":"gltf","source":"Models/source.gltf",
        "virtual_path":"/Content/Models/source.gltf",
        "normals_policy":"generate",
        "output_format":"bc7_srgb","data_format":"bc7","mip_policy":"full",
        "mip_filter":"kaiser","bc7_quality":"high"}]})";
  }
  const auto before
    = ModelImportRecord::Prepare(record_, Generations(), mounts_);
  ModelImportRecord::SaveRecipeFile(
    {
      .record_path = record_,
      .source = {},
      .settings_json = std::nullopt,
      .mounts = oxygen::make_observer(&mounts_),
    },
    recipe_path, ImportFormat::kGltf);
  const auto after
    = ModelImportRecord::Prepare(record_, Generations(), mounts_);
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
    recipe
      << R"({"version":1,"jobs":[{"type":"gltf","source":"Models/source.gltf",
      "virtual_path":"/Content/Models/source.gltf"}]})";
  }
  const auto before = FileDigest(record_);
  EXPECT_THROW(ModelImportRecord::SaveRecipeFile(
                 {
                   .record_path = record_,
                   .source = {},
                   .settings_json = std::nullopt,
                   .mounts = oxygen::make_observer(&mounts_),
                 },
                 recipe_path, ImportFormat::kFbx),
    std::invalid_argument);
  EXPECT_EQ(FileDigest(record_), before);
}

//! A replay whose source is gone says so, and how to rebind it.
NOLINT_TEST_F(ModelImportRecordTest, MissingSourceOnReplayIsSourceMissing)
{
  std::filesystem::remove(source_);
  try {
    static_cast<void>(
      ModelImportRecord::Prepare(record_, Generations(), mounts_));
    FAIL() << "Expected import.source_missing";
  } catch (const std::runtime_error& error) {
    EXPECT_THAT(error.what(), testing::HasSubstr("import.source_missing"));
  }
}

//! The publication commits the import's provenance, published nodes and
//! fingerprint with the generation, and changes nothing else.
NOLINT_TEST_F(ModelImportRecordTest, PublishCommitsWhatTheImportEstablished)
{
  auto attempt = ModelImportRecord::Prepare(record_, Generations(), mounts_);
  auto report = Candidate(*attempt);
  report.published_nodes = {
    ModelPublishedNode {
      .anchor = "Root",
      .node_id = NodeId::FromStableIdentity("Root"),
    },
  };
  report.new_nodes = report.published_nodes;
  report.vanished_nodes = {
    ModelPublishedNode {
      .anchor = "Body/Door_FL",
      .node_id = NodeId::FromStableIdentity("Body/Door_FL"),
    },
  };
  report.source_fingerprint = Fingerprint(source_);
  const auto settings_before
    = ModelImportRecord::Read(record_, mounts_).settings_json;

  attempt->ValidateCandidate(report);
  attempt->Publish(report);

  const auto content = ModelImportRecord::Read(record_, mounts_);
  EXPECT_EQ(content.published_nodes, report.published_nodes);
  EXPECT_EQ(content.new_nodes, report.new_nodes);
  EXPECT_EQ(content.vanished_nodes, report.vanished_nodes);
  EXPECT_EQ(content.source_fingerprint, report.source_fingerprint);
  EXPECT_EQ(content.settings_json, settings_before);
  EXPECT_EQ(content.source_identity, identity_);
  EXPECT_TRUE(
    std::filesystem::exists(report.cooked_root.parent_path() / "head.json"));
  EXPECT_FALSE(
    json::parse(std::ifstream(record_)).contains("published_generation"));
}

//! Generations live in the caller's generations folder, under the record's
//! source identity; nothing is derived inside the content root.
NOLINT_TEST_F(ModelImportRecordTest, PublishesIntoCallersGenerationsFolder)
{
  const auto report = PublishRead(record_);

  EXPECT_EQ(report.cooked_root.parent_path(),
    Generations().root / identity_.ToString());
  EXPECT_TRUE(std::filesystem::exists(
    Generations().root / identity_.ToString() / "head.json"));
  EXPECT_FALSE(std::filesystem::exists(TempDir() / ".cooked"));
  EXPECT_FALSE(ModelImportRecord::SelectedGeneration(
    record_, ModelImportGenerations { .root = TempDir() / "Elsewhere" }));
  EXPECT_THROW(
    static_cast<void>(ModelImportRecord::Prepare(record_, {}, mounts_)),
    std::invalid_argument);
}

NOLINT_TEST_F(
  ModelImportRecordTest, SelectsOnlyValidatedGenerationAndRetainsIdentity)
{
  auto attempt = ModelImportRecord::Prepare(record_, Generations(), mounts_);
  EXPECT_FALSE(ModelImportRecord::SelectedGeneration(record_, Generations()));
  auto report = Candidate(*attempt);
  EXPECT_THROW(attempt->Publish(report), std::invalid_argument);
  attempt->ValidateCandidate(report);
  attempt->Publish(report);
  EXPECT_EQ(ModelImportRecord::SelectedGeneration(record_, Generations()),
    report.cooked_root);
  EXPECT_EQ(report.model_record_path, record_);
  auto next = ModelImportRecord::Prepare(record_, Generations(), mounts_);
  EXPECT_NE(next->Request().cooked_root, report.cooked_root);
  EXPECT_EQ(next->Request().surface_provenance->SourceIdentity(), identity_);
  EXPECT_NE(next->Request().source_key, attempt->Request().source_key);
}

NOLINT_TEST_F(
  ModelImportRecordTest, ConcurrentAttemptCannotOverwriteAcceptedGeneration)
{
  auto first = ModelImportRecord::Prepare(record_, Generations(), mounts_);
  auto second = ModelImportRecord::Prepare(record_, Generations(), mounts_);
  auto accepted = Candidate(*first);
  auto stale = Candidate(*second);
  first->ValidateCandidate(accepted);
  second->ValidateCandidate(stale);
  first->Publish(accepted);
  EXPECT_THROW(second->Publish(stale), std::runtime_error);
  EXPECT_EQ(ModelImportRecord::SelectedGeneration(record_, Generations()),
    accepted.cooked_root);
  EXPECT_TRUE(
    std::filesystem::exists(accepted.cooked_root / "container.index.bin"));
}

NOLINT_TEST_F(ModelImportRecordTest, SettingsChangeInvalidatesPendingCandidate)
{
  auto attempt = ModelImportRecord::Prepare(record_, Generations(), mounts_);
  auto report = Candidate(*attempt);
  attempt->ValidateCandidate(report);
  settings_.normals_policy = "recalculate";
  SaveRecord(record_, source_);
  EXPECT_THROW(attempt->Publish(report), std::runtime_error);
  EXPECT_FALSE(ModelImportRecord::SelectedGeneration(record_, Generations()));
}

//! A failed or cancelled import leaves the record and the selected
//! generation as they were.
NOLINT_TEST_F(
  ModelImportRecordTest, FailedOrCancelledAttemptLeavesTheRecordUnchanged)
{
  auto first = ModelImportRecord::Prepare(record_, Generations(), mounts_);
  auto accepted = Candidate(*first);
  first->ValidateCandidate(accepted);
  first->Publish(accepted);
  const auto before = FileDigest(record_);
  auto next = ModelImportRecord::Prepare(record_, Generations(), mounts_);
  auto failed = Candidate(*next);
  failed.source_fingerprint = Fingerprint(source_);
  failed.success = false;
  EXPECT_THROW(next->ValidateCandidate(failed), std::invalid_argument);
  failed.success = true;
  next->ValidateCandidate(failed);
  std::stop_source cancel;
  cancel.request_stop();
  EXPECT_THROW(next->Publish(failed, cancel.get_token()), std::runtime_error);
  EXPECT_EQ(ModelImportRecord::SelectedGeneration(record_, Generations()),
    accepted.cooked_root);
  EXPECT_EQ(FileDigest(record_), before);
}

NOLINT_TEST_F(
  ModelImportRecordTest, OrdinarySubmissionCannotOverwriteMarkedGeneration)
{
  auto attempt = ModelImportRecord::Prepare(record_, Generations(), mounts_);
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
  EXPECT_EQ(ModelImportRecord::SelectedGeneration(record_, Generations()),
    selected.cooked_root);
}

NOLINT_TEST_F(ModelImportRecordTest, PreparedGenerationCannotBeSubmittedTwice)
{
  auto attempt = ModelImportRecord::Prepare(record_, Generations(), mounts_);
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
  auto recook = ModelImportRecord::Prepare(record_, Generations(), mounts_);
  EXPECT_EQ(recook->Request().surface_provenance->SourceIdentity(), identity_);
}

NOLINT_TEST_F(
  ModelImportRecordTest, ReclamationKeepsSelectedAndLeasedGenerations)
{
  auto first = ModelImportRecord::Prepare(record_, Generations(), mounts_);
  auto before = Candidate(*first);
  first->ValidateCandidate(before);
  first->Publish(before);
  auto reader = oxygen::serio::FileLock::TryAcquire(
    before.cooked_root / oxygen::data::loose_cooked::kGenerationLeaseFileName,
    oxygen::serio::FileLockMode::kShared);
  ASSERT_TRUE(reader);
  auto next = ModelImportRecord::Prepare(record_, Generations(), mounts_);
  auto after = Candidate(*next);
  next->ValidateCandidate(after);
  next->Publish(after);
  EXPECT_EQ(ModelImportRecord::ReclaimUnusedGenerations(
              record_, Generations(), mounts_),
    0U);
  EXPECT_TRUE(std::filesystem::exists(before.cooked_root));
  reader = oxygen::Ok(oxygen::serio::FileLock {});
  EXPECT_EQ(ModelImportRecord::ReclaimUnusedGenerations(
              record_, Generations(), mounts_),
    1U);
  EXPECT_FALSE(std::filesystem::exists(before.cooked_root));
  EXPECT_TRUE(std::filesystem::exists(after.cooked_root));
}

NOLINT_TEST_F(ModelImportRecordTest, ReclamationWaitsForActiveAttemptLease)
{
  auto attempt = ModelImportRecord::Prepare(record_, Generations(), mounts_);
  const auto& cooked_root = attempt->Request().cooked_root;
  if (!cooked_root) {
    FAIL() << "Expected a generation cooked root";
  }
  const auto abandoned = *cooked_root;
  EXPECT_EQ(ModelImportRecord::ReclaimUnusedGenerations(
              record_, Generations(), mounts_),
    0U);
  attempt.reset();
  EXPECT_EQ(ModelImportRecord::ReclaimUnusedGenerations(
              record_, Generations(), mounts_),
    1U);
  EXPECT_FALSE(std::filesystem::exists(abandoned));
}

NOLINT_TEST_F(ModelImportRecordTest, ChangedPublishedIndexRequiresRecook)
{
  auto attempt = ModelImportRecord::Prepare(record_, Generations(), mounts_);
  auto report = Candidate(*attempt);
  attempt->ValidateCandidate(report);
  attempt->Publish(report);
  {
    std::ofstream index(
      report.cooked_root / "container.index.bin", std::ios::app);
    index << "changed";
  }
  EXPECT_THROW(static_cast<void>(
                 ModelImportRecord::SelectedGeneration(record_, Generations())),
    std::runtime_error);
}

//! A real import from the record commits the nodes it emitted and the bytes
//! it read.
NOLINT_TEST_F(ModelImportRecordTest, NativeServicePublishesTheCompleteRecord)
{
  auto attempt = ModelImportRecord::Prepare(record_, Generations(), mounts_);
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
  EXPECT_EQ(ModelImportRecord::SelectedGeneration(record_, Generations()),
    report.cooked_root);
  const auto content = ModelImportRecord::Read(record_, mounts_);
  ASSERT_EQ(content.published_nodes.size(), 1U);
  EXPECT_EQ(content.published_nodes.front().anchor, "Root");
  EXPECT_EQ(content.source_fingerprint, Fingerprint(source_));
}

//! What finding the record decided is part of the import's diagnostics, once
//! in its report.
NOLINT_TEST_F(ModelImportRecordTest, LookupNotesReachTheImportsDiagnostics)
{
  auto attempt = ModelImportRecord::Prepare(record_, Generations(), mounts_,
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
  auto cancelled_attempt
    = ModelImportRecord::Prepare(record_, Generations(), mounts_);
  const auto other_source = TempDir() / "Models" / "other.gltf";
  WriteModel(other_source);
  const auto other_record
    = NewRecord(other_source, "/Content/Models/other.gltf");
  auto other_attempt
    = ModelImportRecord::Prepare(other_record, Generations(), mounts_);
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
  EXPECT_FALSE(ModelImportRecord::SelectedGeneration(record_, Generations()));
  EXPECT_TRUE(
    ModelImportRecord::SelectedGeneration(other_record, Generations()));
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

//! The record of the model's own source and identity, unchanged.
NOLINT_TEST_F(ModelImportRecordTest, LocateFindsTheModelsRecord)
{
  const auto location = ModelImportRecord::LocateRecord({
    .source = source_,
    .identity = std::string(kIdentity),
    .mounts = oxygen::make_observer(&mounts_),
  });
  EXPECT_EQ(location.record_path, record_);
  EXPECT_TRUE(location.exists);
  EXPECT_TRUE(location.diagnostics.empty());
}

//! A record file of the mounts that is not a valid record, such as one of an
//! older format, is skipped with a warning that names it but never quotes the
//! document; the model's own record is still found and still imports.
NOLINT_TEST_F(ModelImportRecordTest, AnUnreadableRecordIsSkippedWithAWarning)
{
  const auto outdated = WriteRecord(
    ModelImportRecordDocument("/Content/Models/Outdated.gltf", kIdentity)
      .Remove("/schema_version")
      .Set("/published_generation", json { { "id", "not-quoted" } }));

  const auto location = ModelImportRecord::LocateRecord({
    .source = source_,
    .identity = std::string(kIdentity),
    .mounts = oxygen::make_observer(&mounts_),
  });

  EXPECT_EQ(location.record_path, record_);
  EXPECT_TRUE(location.exists);
  ASSERT_EQ(location.diagnostics.size(), 1U);
  const auto& skipped = location.diagnostics.front();
  EXPECT_EQ(skipped.code, "import.model_record_unreadable");
  EXPECT_EQ(skipped.severity, ImportSeverity::kWarning);
  EXPECT_NE(
    skipped.message.find(outdated.filename().string()), std::string::npos);
  EXPECT_EQ(skipped.message.find("not-quoted"), std::string::npos);
  EXPECT_NO_THROW(static_cast<void>(
    ModelImportRecord::Prepare(record_, Generations(), mounts_)));
}

//! A moved source imported with the model's identity keeps its source
//! identity: the record's source is rebound.
NOLINT_TEST_F(ModelImportRecordTest, AMovedSourceWithItsVirtualPathIsRebound)
{
  const auto moved = TempDir() / "Elsewhere" / "source.gltf";
  std::filesystem::create_directories(moved.parent_path());
  std::filesystem::rename(source_, moved);

  const auto location = ModelImportRecord::LocateRecord({
    .source = moved,
    .identity = std::string(kIdentity),
    .mounts = oxygen::make_observer(&mounts_),
  });
  SaveRecord(location.record_path, moved);

  EXPECT_EQ(location.record_path, record_);
  EXPECT_THAT(Codes(location), testing::ElementsAre("import.source_rebound"));
  const auto content = ModelImportRecord::Read(record_, mounts_);
  EXPECT_TRUE(std::filesystem::equivalent(content.source, moved));
  EXPECT_EQ(content.source_identity, identity_);
}

//! A source moved and imported under another identity is recognised by its
//! bytes, once its original is gone: its model keeps its identity, and the
//! record stays where it is, rebound to the moved file.
NOLINT_TEST_F(
  ModelImportRecordTest, AMovedSourceWithIdenticalBytesKeepsItsIdentity)
{
  static_cast<void>(PublishRead(record_));
  const auto moved = TempDir() / "Props" / "renamed.gltf";
  std::filesystem::create_directories(moved.parent_path());
  std::filesystem::rename(source_, moved);

  const auto location = ModelImportRecord::LocateRecord({
    .source = moved,
    .identity = std::string("/Content/Props/renamed.gltf"),
    .mounts = oxygen::make_observer(&mounts_),
  });
  SaveRecord(location.record_path, moved);

  EXPECT_EQ(location.record_path, record_);
  EXPECT_TRUE(location.exists);
  EXPECT_THAT(Codes(location), testing::ElementsAre("import.source_relocated"));
  EXPECT_FALSE(
    std::filesystem::exists(RecordOf("/Content/Props/renamed.gltf")));
  const auto content = ModelImportRecord::Read(record_, mounts_);
  EXPECT_TRUE(std::filesystem::equivalent(content.source, moved));
  EXPECT_EQ(content.source_identity, identity_);
}

//! A copy imported with the original's identity becomes its source; the
//! original stays.
NOLINT_TEST_F(
  ModelImportRecordTest, ACopyWithTheOriginalsVirtualPathIsItsSource)
{
  const auto copy = TempDir() / "Models" / "copy.gltf";
  std::filesystem::copy_file(source_, copy);

  const auto location = ModelImportRecord::LocateRecord({
    .source = copy,
    .identity = std::string(kIdentity),
    .mounts = oxygen::make_observer(&mounts_),
  });

  EXPECT_EQ(location.record_path, record_);
  EXPECT_THAT(Codes(location), testing::ElementsAre("import.source_rebound"));
  EXPECT_TRUE(std::filesystem::exists(source_));
}

//! A copy under a new identity is a new model, with a hint naming the
//! original.
NOLINT_TEST_F(ModelImportRecordTest, ACopyUnderANewVirtualPathIsANewModel)
{
  static_cast<void>(PublishRead(record_));
  const auto copy = TempDir() / "Models" / "copy.gltf";
  std::filesystem::copy_file(source_, copy);

  const auto location = ModelImportRecord::LocateRecord({
    .source = copy,
    .identity = std::string("/Content/Models/copy.gltf"),
    .mounts = oxygen::make_observer(&mounts_),
  });

  EXPECT_FALSE(location.exists);
  EXPECT_EQ(location.record_path, RecordOf("/Content/Models/copy.gltf"));
  EXPECT_THAT(Codes(location), testing::ElementsAre("import.source_copy"));
}

//! The same file under another identity is another model from that file.
NOLINT_TEST_F(
  ModelImportRecordTest, AnotherVirtualPathOfTheSameFileIsAnotherModel)
{
  const auto location = ModelImportRecord::LocateRecord({
    .source = source_,
    .identity = std::string("/Content/Models/second.gltf"),
    .mounts = oxygen::make_observer(&mounts_),
  });
  SaveRecord(location.record_path, source_);

  EXPECT_FALSE(location.exists);
  EXPECT_THAT(Codes(location), testing::ElementsAre("import.source_has_model"));
  EXPECT_TRUE(std::filesystem::equivalent(
    ModelImportRecord::Read(location.record_path, mounts_).source, source_));
}

//! Several records the bytes could belong to: none is taken.
NOLINT_TEST_F(ModelImportRecordTest, AnAmbiguousRelocationTakesNoRecord)
{
  static_cast<void>(PublishRead(record_));
  const auto twin_source = TempDir() / "Models" / "twin.gltf";
  WriteModel(twin_source);
  const auto twin = NewRecord(twin_source, "/Content/Models/twin.gltf");
  static_cast<void>(PublishRead(twin));
  const auto moved = TempDir() / "Props" / "moved.gltf";
  std::filesystem::create_directories(moved.parent_path());
  std::filesystem::rename(source_, moved);
  std::filesystem::remove(twin_source);

  const auto location = ModelImportRecord::LocateRecord({
    .source = moved,
    .identity = std::string("/Content/Props/moved.gltf"),
    .mounts = oxygen::make_observer(&mounts_),
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
    .source = moved,
    .identity = std::string("/Content/Props/props.gltf"),
    .mounts = oxygen::make_observer(&mounts_),
  });

  EXPECT_FALSE(location.exists);
  ASSERT_THAT(Codes(location),
    testing::ElementsAre("import.source_relocation_candidate"));
  EXPECT_THAT(location.diagnostics.front().message,
    testing::HasSubstr("--record " + record_.generic_string()));
}

} // namespace
