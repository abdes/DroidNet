//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/ImportTool/BatchCommand.cpp

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Clap/CommandLineContext.h> // IWYU pragma: keep
#include <Oxygen/Clap/Fluent/CliBuilder.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/Importer.h>
#include <Oxygen/Cooker/Tools/ImportTool/BatchCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/TempDir.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::AsyncImportService;
using oxygen::content::import::tool::BatchCommand;
using oxygen::content::import::tool::GlobalOptions;
using oxygen::content::import::tool::IMessageWriter;
using oxygen::cooker::test::WriteText;

class CapturingWriter final : public IMessageWriter {
public:
  auto Error(const std::string_view message) -> bool override
  {
    messages_.emplace_back(message);
    return true;
  }

  auto Warning(const std::string_view message) -> bool override
  {
    messages_.emplace_back(message);
    return true;
  }

  auto Info(const std::string_view message) -> bool override
  {
    messages_.emplace_back(message);
    return true;
  }

  auto Report(const std::string_view message) -> bool override
  {
    messages_.emplace_back(message);
    return true;
  }

  auto Progress(const std::string_view message) -> bool override
  {
    messages_.emplace_back(message);
    return true;
  }

  [[nodiscard]] auto JoinedMessages() const -> std::string
  {
    auto out = std::ostringstream {};
    for (const auto& msg : messages_) {
      out << msg << "\n";
    }
    return out.str();
  }

private:
  std::vector<std::string> messages_;
};

auto CountOccurrences(const std::string& text, const std::string_view token)
  -> size_t
{
  if (token.empty()) {
    return 0U;
  }
  size_t count = 0U;
  size_t position = 0U;
  while (true) {
    position = text.find(token, position);
    if (position == std::string::npos) {
      break;
    }
    ++count;
    position += token.size();
  }
  return count;
}

class BatchCommandPhysicsDagTest : public oxygen::cooker::test::TempDirTest {
protected:
  //! Creates and returns `TempDir() / scenario`.
  [[nodiscard]] auto MakeScenarioDir(const std::string_view scenario) const
    -> std::filesystem::path
  {
    auto dir = TempPath(scenario);
    std::filesystem::create_directories(dir);
    return dir;
  }

  void SetUp() override
  {
    writer_ = std::make_unique<CapturingWriter>();
    service_ = std::make_unique<AsyncImportService>(AsyncImportService::Config {
      .thread_pool_size = 1U,
      .max_in_flight_jobs = 1U,
    });

    options_.fail_fast = true;
    options_.no_tui = true;
    options_.writer = oxygen::make_observer<IMessageWriter>(writer_.get());
    options_.import_service
      = oxygen::make_observer<AsyncImportService>(service_.get());
  }

  void TearDown() override
  {
    if (service_ != nullptr && !service_->IsStopped()) {
      service_->Stop();
    }
  }

  auto RunBatch(const std::filesystem::path& manifest_path,
    const std::optional<std::filesystem::path>& cooked_root_override
    = std::nullopt,
    const std::optional<std::filesystem::path>& report_path = std::nullopt)
    -> std::expected<void, std::error_code>
  {
    options_.cooked_root = cooked_root_override.has_value()
      ? cooked_root_override->generic_string()
      : std::string {};

    auto command = BatchCommand(&options_);
    const auto subcommand = command.BuildCommand();
    const auto cli = oxygen::clap::CliBuilder()
                       .ProgramName("tool")
                       .WithCommand(subcommand)
                       .Build();

    const auto manifest_arg = manifest_path.generic_string();
    const auto report_arg = report_path.has_value()
      ? report_path->generic_string()
      : std::string {};
    std::vector<const char*> argv {
      "tool",
      "batch",
      "--manifest",
      manifest_arg.c_str(),
    };
    options_.command_line = "tool batch --manifest " + manifest_arg;
    if (report_path.has_value()) {
      argv.push_back("--report");
      argv.push_back(report_arg.c_str());
      options_.command_line += " --report " + report_arg;
    }
    static_cast<void>(cli->Parse(argv));

    return command.Execute();
  }

  [[nodiscard]] auto Messages() const -> std::string
  {
    return writer_->JoinedMessages();
  }

  auto ContinueAfterValidationErrors() -> void { options_.fail_fast = false; }

private:
  GlobalOptions options_ {};
  std::unique_ptr<CapturingWriter> writer_;
  std::unique_ptr<AsyncImportService> service_;
};

NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  RejectedDescriptorFailsBatchEvenWhenValidJobsSucceed)
{
  ContinueAfterValidationErrors();
  const auto root = MakeScenarioDir("rejected_descriptor_with_valid_material");
  const auto manifest_path = root / "import-manifest.json";
  WriteText(root / "Valid.material.json", R"({"name":"Valid"})");
  WriteText(root / "Invalid.geometry.json", R"({"name":"Invalid"})");
  WriteText(manifest_path, R"({
    "version": 1,
    "output": ".cooked",
    "jobs": [
      {"type":"material-descriptor","source":"Valid.material.json",
       "virtual_path":"/.cooked/Materials/Valid.omat"},
      {"type":"geometry-descriptor","source":"Invalid.geometry.json",
       "virtual_path":"/.cooked/Geometry/Invalid.ogeo"}
    ]
  })");

  const auto result = RunBatch(manifest_path);
  ASSERT_FALSE(result.has_value()) << Messages();
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::invalid_argument));
  EXPECT_TRUE(std::filesystem::exists(root / ".cooked/Materials/Valid.omat"));
  EXPECT_THAT(Messages(), ::testing::HasSubstr("validation_errors=1"));
}

NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  InvalidTexturePathWritesFailedReportWithoutAborting)
{
  const auto root = MakeScenarioDir("invalid_texture_report");
  const auto manifest_path = root / "manifest.json";
  const auto report_path = root / "report.json";
  WriteText(root / "texture.json",
    R"({"source":"missing.png","virtual_path":"/Other/Meter.otex"})");
  WriteText(manifest_path, R"({"version":1,"output":"cooked",
    "layout":{"virtual_mount_root":"/Content"},
    "jobs":[{"type":"texture-descriptor","source":"texture.json",
      "virtual_path":"/Other/Meter.otex"}]})");
  EXPECT_FALSE(RunBatch(manifest_path, std::nullopt, report_path).has_value());
  auto stream = std::ifstream(report_path);
  ASSERT_TRUE(stream.is_open());
  const auto report = nlohmann::json::parse(stream);
  const auto& job = report.at("jobs").at(0);
  EXPECT_EQ(job.at("status"), "failed");
  EXPECT_EQ(job.at("stats").at("time_ms_io"), 0.0);
  EXPECT_TRUE(job.at("stats").at("time_ms_total").is_number());
  EXPECT_FALSE(job.at("diagnostics").empty());
  EXPECT_FALSE(std::filesystem::exists(root / "cooked"));
}

//! Each job reports what its own source produced, so a container's products
//! map back to it without scanning the cooked layout.
NOLINT_TEST_F(BatchCommandPhysicsDagTest, ReportListsEachSourcesProducts)
{
  const auto root = MakeScenarioDir("report_products");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";
  const auto report_path = root / "report.json";
  WriteText(root / "Wood.material.json", R"({"name":"Wood"})");
  WriteText(root / "Input" / "Player.input.json", R"({
    "actions":[{"name":"Jump","type":"bool"}],
    "contexts":[{"name":"OnFoot","mappings":[
      {"action":"Jump","slot":"Space","trigger":"pressed"}]}]})");
  WriteText(manifest_path,
    R"({"version":1,"output":")" + cooked_root.generic_string() + R"(",
    "jobs":[{"type":"material-descriptor","source":"Wood.material.json",
      "virtual_path":"/.cooked/Wood.omat"},
      {"id":"input","type":"input","source":"Input/Player.input.json",
        "virtual_path":"/.cooked/Input/Player.input"}]})");

  ASSERT_TRUE(RunBatch(manifest_path, std::nullopt, report_path).has_value())
    << Messages();
  auto stream = std::ifstream(report_path);
  ASSERT_TRUE(stream.is_open());
  const auto report = nlohmann::json::parse(stream);
  EXPECT_EQ(report.at("report_version"), "4");
  const auto& jobs = report.at("jobs");
  ASSERT_EQ(jobs.size(), 2U);
  // Each job names the importer that cooked it, at its revision.
  using oxygen::content::import::Importer;
  using oxygen::content::import::RevisionOf;
  EXPECT_EQ(jobs.at(0).at("importer").at("name"), "material_descriptor");
  EXPECT_EQ(jobs.at(0).at("importer").at("revision"),
    RevisionOf(Importer::kMaterialDescriptor));
  EXPECT_EQ(jobs.at(1).at("importer").at("name"), "input");
  EXPECT_EQ(
    jobs.at(1).at("importer").at("revision"), RevisionOf(Importer::kInput));

  const auto& material = jobs.at(0).at("products");
  ASSERT_EQ(material.size(), 1U);
  EXPECT_EQ(material.at(0).at("kind"), "asset");
  EXPECT_EQ(material.at(0).at("asset_type"),
    static_cast<uint8_t>(oxygen::data::AssetType::kMaterial));
  EXPECT_EQ(material.at(0).at("virtual_path"), "/.cooked/Wood.omat");

  auto input_types = std::vector<uint8_t> {};
  auto input_paths = std::vector<std::string> {};
  for (const auto& product : jobs.at(1).at("products")) {
    EXPECT_FALSE(product.at("asset_key").get<std::string>().empty());
    EXPECT_TRUE(std::filesystem::exists(
      cooked_root / product.at("descriptor_relative_path").get<std::string>()));
    input_types.push_back(product.at("asset_type").get<uint8_t>());
    input_paths.push_back(product.at("virtual_path").get<std::string>());
  }
  EXPECT_THAT(input_types,
    ::testing::UnorderedElementsAre(
      static_cast<uint8_t>(oxygen::data::AssetType::kInputAction),
      static_cast<uint8_t>(oxygen::data::AssetType::kInputMappingContext)));
  EXPECT_THAT(input_paths,
    ::testing::UnorderedElementsAre("/.cooked/Input/Player.input/Jump.oiact",
      "/.cooked/Input/Player.input/OnFoot.oimap"));
}

//! Two input sources in one folder that declare the same action name cook two
//! actions: each product is named under its own source.
NOLINT_TEST_F(BatchCommandPhysicsDagTest, InputSourcesInOneFolderKeepTheirKeys)
{
  const auto root = MakeScenarioDir("input_same_name");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";
  const auto report_path = root / "report.json";
  for (const auto* name : { "Player", "Vehicle" }) {
    WriteText(root / "Input" / (std::string(name) + ".input.json"), R"({
      "actions":[{"name":"Jump","type":"bool"}]})");
  }
  WriteText(manifest_path,
    R"({"version":1,"output":")" + cooked_root.generic_string() + R"(",
    "jobs":[
      {"id":"player","type":"input","source":"Input/Player.input.json",
        "virtual_path":"/.cooked/Input/Player.input"},
      {"id":"vehicle","type":"input","source":"Input/Vehicle.input.json",
        "virtual_path":"/.cooked/Input/Vehicle.input"}]})");

  ASSERT_TRUE(RunBatch(manifest_path, std::nullopt, report_path).has_value())
    << Messages();
  auto stream = std::ifstream(report_path);
  ASSERT_TRUE(stream.is_open());
  const auto report = nlohmann::json::parse(stream);
  auto keys = std::vector<std::string> {};
  auto paths = std::vector<std::string> {};
  for (const auto& job : report.at("jobs")) {
    for (const auto& product : job.at("products")) {
      keys.push_back(product.at("asset_key").get<std::string>());
      paths.push_back(product.at("virtual_path").get<std::string>());
    }
  }
  EXPECT_THAT(paths,
    ::testing::UnorderedElementsAre("/.cooked/Input/Player.input/Jump.oiact",
      "/.cooked/Input/Vehicle.input/Jump.oiact"));
  ASSERT_EQ(keys.size(), 2U);
  EXPECT_NE(keys.at(0), keys.at(1));
}

//! A mapping names an action of another input source by its virtual path; the
//! cooker resolves it like any authored reference.
NOLINT_TEST_F(
  BatchCommandPhysicsDagTest, MappingReferencesAnotherSourcesActionByPath)
{
  const auto root = MakeScenarioDir("input_cross_source");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";
  WriteText(root / "Input" / "Common.input.json", R"({
    "actions":[{"name":"Jump","type":"bool"}]})");
  WriteText(root / "Input" / "Player.input.json", R"({
    "contexts":[{"name":"OnFoot","mappings":[
      {"action":"/.cooked/Input/Common.input/Jump.oiact","slot":"Space"}]}]})");
  WriteText(manifest_path,
    R"({"version":1,"output":")" + cooked_root.generic_string() + R"(",
    "jobs":[
      {"id":"common","type":"input","source":"Input/Common.input.json",
        "virtual_path":"/.cooked/Input/Common.input"},
      {"id":"player","type":"input","source":"Input/Player.input.json",
        "virtual_path":"/.cooked/Input/Player.input",
        "depends_on":["common"]}]})");

  EXPECT_TRUE(RunBatch(manifest_path).has_value()) << Messages();
  EXPECT_TRUE(std::filesystem::exists(
    cooked_root / "Input" / "Player.input" / "OnFoot.oimap"));
}

//! A mapping naming an action that no mounted source declares is refused.
NOLINT_TEST_F(BatchCommandPhysicsDagTest, MappingToAMissingActionIsRefused)
{
  const auto root = MakeScenarioDir("input_missing_action");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";
  WriteText(root / "Input" / "Player.input.json", R"({
    "contexts":[{"name":"OnFoot","mappings":[
      {"action":"/.cooked/Input/Common.input/Jump.oiact","slot":"Space"},
      {"action":"Dash","slot":"LeftShift"}]}]})");
  WriteText(manifest_path,
    R"({"version":1,"output":")" + cooked_root.generic_string() + R"(",
    "jobs":[
      {"id":"player","type":"input","source":"Input/Player.input.json",
        "virtual_path":"/.cooked/Input/Player.input"}]})");

  EXPECT_FALSE(RunBatch(manifest_path).has_value());
  EXPECT_THAT(
    Messages(), ::testing::HasSubstr("input.context.action_unresolved"));
}

//! A single-asset job without `virtual_path` has no identity: the manifest is
//! refused before anything is imported.
NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  SingleAssetJobWithoutVirtualPathIsRefusedBeforeImport)
{
  const auto root = MakeScenarioDir("missing_virtual_path");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";
  WriteText(root / "Wood.material.json", R"({"name":"Wood"})");
  WriteText(manifest_path,
    R"({"version":1,"output":")" + cooked_root.generic_string() + R"(",
    "jobs":[{"type":"material-descriptor","source":"Wood.material.json"}]})");

  EXPECT_FALSE(RunBatch(manifest_path).has_value());
  EXPECT_THAT(
    Messages(), ::testing::HasSubstr("import.manifest.virtual_path_missing"));
  EXPECT_FALSE(std::filesystem::exists(cooked_root));
}

NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  OutputOverrideRejectsConflictingRootIdentitiesBeforeImport)
{
  ContinueAfterValidationErrors();
  const auto root = MakeScenarioDir("conflicting_output_identities");
  const auto manifest_path = root / "import-manifest.json";
  std::ostringstream manifest;
  manifest << R"({"version":1,"jobs":[
    {"id":"first","type":"texture","source":"first.png","virtual_path":"/.cooked/first.otex","output":"first","source_key":")"
           << oxygen::Uuid::Generate().ToString() << R"("},
    {"id":"second","type":"texture","source":"second.png","virtual_path":"/.cooked/second.otex","output":"second","depends_on":["first"],"source_key":")"
           << oxygen::Uuid::Generate().ToString() << R"("}]})";
  WriteText(manifest_path, manifest.str());
  const auto shared_root = root / "shared";
  EXPECT_FALSE(RunBatch(manifest_path, shared_root).has_value());
  EXPECT_THAT(Messages(), ::testing::HasSubstr("import.source_key_conflict"));
  EXPECT_FALSE(std::filesystem::exists(shared_root));
}

#ifdef _WIN32
NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  CaseAliasedOutputsRejectConflictingRootIdentitiesBeforeImport)
{
  const auto root = MakeScenarioDir("case_alias_output_identities");
  const auto manifest_path = root / "import-manifest.json";
  std::ostringstream manifest;
  manifest << R"({"version":1,"jobs":[
    {"type":"texture","source":"first.png","virtual_path":"/.cooked/first.otex","output":"Cooked","source_key":")"
           << oxygen::Uuid::Generate().ToString() << R"("},
    {"type":"texture","source":"second.png","virtual_path":"/.cooked/second.otex","output":"cooked","source_key":")"
           << oxygen::Uuid::Generate().ToString() << R"("}]})";
  WriteText(manifest_path, manifest.str());
  EXPECT_FALSE(RunBatch(manifest_path).has_value());
  EXPECT_THAT(Messages(), ::testing::HasSubstr("import.source_key_conflict"));
  EXPECT_FALSE(std::filesystem::exists(root / "Cooked"));
}
#endif

//! A batch larger than the admission capacity defers saturated submissions
//! and emits every independent asset.
NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  IndependentMaterialBatchRespectsAdmissionBackpressure)
{
  const auto root = MakeScenarioDir("material_admission_backpressure");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";
  std::ostringstream manifest;
  // One admitted job at a time: every submission after the first meets a
  // saturated service and must be deferred, not dropped.
  manifest << R"({"version":1,"output":")" << cooked_root.generic_string()
           << R"(","max_in_flight_jobs":1,"job_admission_capacity":1,"jobs":[)";
  constexpr size_t kMaterialCount = 8;
  for (size_t index = 0; index < kMaterialCount; ++index) {
    const auto name = std::string("M") + std::to_string(index);
    const auto source = std::string("Materials/") + name + ".material.json";
    WriteText(root / source, std::string(R"({"name":")") + name + R"("})");
    if (index != 0U) {
      manifest << ',';
    }
    manifest << R"({"type":"material-descriptor","source":")" << source
             << R"(","virtual_path":"/.cooked/Materials/)" << name
             << R"(.omat"})";
  }
  manifest << "]}";
  WriteText(manifest_path, manifest.str());

  const auto result = RunBatch(manifest_path);
  EXPECT_TRUE(result.has_value()) << Messages();
  EXPECT_THAT(
    Messages(), ::testing::Not(::testing::HasSubstr("import.queue_full")));
  size_t materials = 0;
  for (const auto& entry :
    std::filesystem::recursive_directory_iterator(cooked_root)) {
    if (entry.path().extension() == ".omat") {
      ++materials;
    }
  }
  EXPECT_EQ(materials, kMaterialCount);
}

NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  PhysicsSidecarUnresolvedInferredRefsEmitDependencyUnresolvedDiagnostic)
{
  const auto root = MakeScenarioDir("physics_unresolved_refs");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";

  WriteText(manifest_path,
    std::string { R"({
      "version": 1,
      "output": ")" }
      + cooked_root.generic_string() + R"(",
      "jobs": [
        {
          "id": "physics.sidecar.main",
          "type": "physics-sidecar",
          "target_scene_virtual_path": "/.cooked/Scenes/level.oscene",
          "bindings": {
            "rigid_bodies": [
              {
                "node_index": 0,
                "shape_ref": "/.cooked/Physics/Shapes/floor.ocshape",
                "material_ref": "/.cooked/Physics/Materials/floor.opmat"
              }
            ]
          }
        }
      ]
    })");

  const auto result = RunBatch(manifest_path);
  EXPECT_FALSE(result.has_value());

  const auto messages = Messages();
  EXPECT_THAT(
    messages, ::testing::HasSubstr("physics.manifest.dependency_unresolved"));
}

NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  PhysicsSidecarDuplicateUnresolvedRefsEmitSingleDependencyUnresolvedDiagnostic)
{
  const auto root = MakeScenarioDir("physics_unresolved_duplicate_refs");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";

  WriteText(manifest_path,
    std::string { R"({
      "version": 1,
      "output": ")" }
      + cooked_root.generic_string() + R"(",
      "jobs": [
        {
          "id": "physics.sidecar.main",
          "type": "physics-sidecar",
          "target_scene_virtual_path": "/.cooked/Scenes/level.oscene",
          "bindings": {
            "rigid_bodies": [
              {
                "node_index": 0,
                "shape_ref": "/.cooked/Physics/Shapes/floor.ocshape",
                "material_ref": "/.cooked/Physics/Materials/floor.opmat"
              },
              {
                "node_index": 1,
                "shape_ref": "/.cooked/Physics/Shapes/floor.ocshape",
                "material_ref": "/.cooked/Physics/Materials/floor.opmat"
              }
            ]
          }
        }
      ]
    })");

  const auto result = RunBatch(manifest_path);
  EXPECT_FALSE(result.has_value());

  const auto messages = Messages();
  EXPECT_EQ(
    CountOccurrences(messages, "physics.manifest.dependency_unresolved"), 3U);
}

NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  CollisionShapeRefWithMultipleProducersEmitsDependencyAmbiguousDiagnostic)
{
  const auto root = MakeScenarioDir("physics_ambiguous_ref");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";
  const auto descriptors_dir = root / "Physics";
  const auto mat_a = descriptors_dir / "shared_a.physics-material.json";
  const auto mat_b = descriptors_dir / "shared_b.physics-material.json";
  const auto shape = descriptors_dir / "floor.physics-shape.json";

  WriteText(mat_a,
    R"({
      "name": "shared_a",
      "virtual_path": "/.cooked/Physics/Materials/shared.opmat",
      "static_friction": 0.8,
      "dynamic_friction": 0.8,
      "restitution": 0.1,
      "density": 1.0
    })");
  WriteText(mat_b,
    R"({
      "name": "shared_b",
      "virtual_path": "/.cooked/Physics/Materials/shared.opmat",
      "static_friction": 0.7,
      "dynamic_friction": 0.7,
      "restitution": 0.2,
      "density": 1.0
    })");
  WriteText(shape,
    R"({
      "name": "floor_shape",
      "shape_type": "box",
      "half_extents": [10.0, 1.0, 10.0],
      "material_ref": "/.cooked/Physics/Materials/shared.opmat",
      "virtual_path": "/.cooked/Physics/Shapes/floor.ocshape"
    })");

  WriteText(manifest_path,
    std::string { R"({
      "version": 1,
      "output": ")" }
      + cooked_root.generic_string() + R"(",
      "jobs": [
        {
          "id": "physics.material.shared_a",
          "type": "physics-material-descriptor",
          "source": "Physics/shared_a.physics-material.json",
          "virtual_path": "/.cooked/Physics/Materials/shared.opmat"
        },
        {
          "id": "physics.material.shared_b",
          "type": "physics-material-descriptor",
          "source": "Physics/shared_b.physics-material.json",
          "virtual_path": "/.cooked/Physics/Materials/shared.opmat"
        },
        {
          "id": "physics.shape.floor",
          "type": "collision-shape-descriptor",
          "source": "Physics/floor.physics-shape.json",
          "virtual_path": "/.cooked/Physics/Shapes/floor.ocshape"
        }
      ]
    })");

  const auto result = RunBatch(manifest_path);
  EXPECT_FALSE(result.has_value());

  const auto messages = Messages();
  EXPECT_THAT(
    messages, ::testing::HasSubstr("physics.manifest.dependency_ambiguous"));
}

NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  PhysicsSidecarDuplicateAmbiguousRefsEmitSingleDependencyAmbiguousDiagnostic)
{
  const auto root = MakeScenarioDir("physics_ambiguous_duplicate_refs");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";
  const auto descriptors_dir = root / "Physics";
  const auto mat_a = descriptors_dir / "shared_a.physics-material.json";
  const auto mat_b = descriptors_dir / "shared_b.physics-material.json";
  const auto mat_base = descriptors_dir / "base.physics-material.json";
  const auto shape_a = descriptors_dir / "floor_a.physics-shape.json";
  const auto shape_b = descriptors_dir / "floor_b.physics-shape.json";

  WriteText(mat_a,
    R"({
      "name": "shared_a",
      "virtual_path": "/.cooked/Physics/Materials/shared.opmat",
      "static_friction": 0.8,
      "dynamic_friction": 0.8,
      "restitution": 0.1,
      "density": 1.0
    })");
  WriteText(mat_b,
    R"({
      "name": "shared_b",
      "virtual_path": "/.cooked/Physics/Materials/shared.opmat",
      "static_friction": 0.7,
      "dynamic_friction": 0.7,
      "restitution": 0.2,
      "density": 1.0
    })");
  WriteText(mat_base,
    R"({
      "name": "base",
      "virtual_path": "/.cooked/Physics/Materials/base.opmat",
      "static_friction": 0.5,
      "dynamic_friction": 0.5,
      "restitution": 0.1,
      "density": 1.0
    })");
  WriteText(shape_a,
    R"({
      "name": "floor_shape_a",
      "shape_type": "box",
      "half_extents": [10.0, 1.0, 10.0],
      "material_ref": "/.cooked/Physics/Materials/base.opmat",
      "virtual_path": "/.cooked/Physics/Shapes/floor.ocshape"
    })");
  WriteText(shape_b,
    R"({
      "name": "floor_shape_b",
      "shape_type": "box",
      "half_extents": [10.0, 1.0, 10.0],
      "material_ref": "/.cooked/Physics/Materials/base.opmat",
      "virtual_path": "/.cooked/Physics/Shapes/floor.ocshape"
    })");

  WriteText(manifest_path,
    std::string { R"({
      "version": 1,
      "output": ")" }
      + cooked_root.generic_string() + R"(",
      "jobs": [
        {
          "id": "physics.material.shared_a",
          "type": "physics-material-descriptor",
          "source": "Physics/shared_a.physics-material.json",
          "virtual_path": "/.cooked/Physics/Materials/shared.opmat"
        },
        {
          "id": "physics.material.shared_b",
          "type": "physics-material-descriptor",
          "source": "Physics/shared_b.physics-material.json",
          "virtual_path": "/.cooked/Physics/Materials/shared.opmat"
        },
        {
          "id": "physics.material.base",
          "type": "physics-material-descriptor",
          "source": "Physics/base.physics-material.json",
          "virtual_path": "/.cooked/Physics/Materials/base.opmat"
        },
        {
          "id": "physics.shape.floor_a",
          "type": "collision-shape-descriptor",
          "source": "Physics/floor_a.physics-shape.json",
          "virtual_path": "/.cooked/Physics/Shapes/floor.ocshape"
        },
        {
          "id": "physics.shape.floor_b",
          "type": "collision-shape-descriptor",
          "source": "Physics/floor_b.physics-shape.json",
          "virtual_path": "/.cooked/Physics/Shapes/floor.ocshape"
        },
        {
          "id": "physics.sidecar.main",
          "type": "physics-sidecar",
          "target_scene_virtual_path": "/.cooked/Scenes/level.oscene",
          "bindings": {
            "rigid_bodies": [
              {
                "node_index": 0,
                "shape_ref": "/.cooked/Physics/Shapes/floor.ocshape",
                "material_ref": "/.cooked/Physics/Materials/shared.opmat"
              },
              {
                "node_index": 1,
                "shape_ref": "/.cooked/Physics/Shapes/floor.ocshape",
                "material_ref": "/.cooked/Physics/Materials/shared.opmat"
              }
            ]
          }
        }
      ]
    })");

  const auto result = RunBatch(manifest_path);
  EXPECT_FALSE(result.has_value());

  const auto messages = Messages();
  EXPECT_EQ(
    CountOccurrences(messages, "physics.manifest.dependency_ambiguous"), 2U);
}

NOLINT_TEST_F(
  BatchCommandPhysicsDagTest, PhysicsCycleUsesPhysicsCycleDiagnostic)
{
  const auto root = MakeScenarioDir("physics_dep_cycle");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";
  const auto descriptors_dir = root / "Physics";
  const auto mat_a = descriptors_dir / "mat_a.physics-material.json";
  const auto mat_b = descriptors_dir / "mat_b.physics-material.json";

  WriteText(mat_a,
    R"({
      "name": "mat_a",
      "virtual_path": "/.cooked/Physics/Materials/mat_a.opmat",
      "static_friction": 0.5,
      "dynamic_friction": 0.5,
      "restitution": 0.1,
      "density": 1.0
    })");
  WriteText(mat_b,
    R"({
      "name": "mat_b",
      "virtual_path": "/.cooked/Physics/Materials/mat_b.opmat",
      "static_friction": 0.5,
      "dynamic_friction": 0.5,
      "restitution": 0.1,
      "density": 1.0
    })");

  WriteText(manifest_path,
    std::string { R"({
      "version": 1,
      "output": ")" }
      + cooked_root.generic_string() + R"(",
      "jobs": [
        {
          "id": "mat.a",
          "depends_on": ["mat.b"],
          "type": "physics-material-descriptor",
          "source": "Physics/mat_a.physics-material.json",
          "virtual_path": "/.cooked/Physics/Materials/mat_a.opmat"
        },
        {
          "id": "mat.b",
          "depends_on": ["mat.a"],
          "type": "physics-material-descriptor",
          "source": "Physics/mat_b.physics-material.json",
          "virtual_path": "/.cooked/Physics/Materials/mat_b.opmat"
        }
      ]
    })");

  const auto result = RunBatch(manifest_path);
  EXPECT_FALSE(result.has_value());

  const auto messages = Messages();
  EXPECT_THAT(
    messages, ::testing::HasSubstr("physics.manifest.dependency_cycle"));
}

NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  PhysicsSidecarDependsOnSameRootAssetProducersToStabilizeIndices)
{
  const auto root = MakeScenarioDir("physics_sidecar_same_root_fence");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";
  const auto scene_source = root / "Scenes" / "showcase_scene.gltf";
  const auto input_source = root / "Input" / "rotate.input.json";
  const auto sidecar_source = root / "Scenes" / "showcase_scene.physics.json";

  WriteText(scene_source, "{}");
  WriteText(input_source, "{}");
  WriteText(sidecar_source, R"({"bindings": {}})");

  WriteText(manifest_path,
    std::string { R"({
      "version": 1,
      "output": ")" }
      + cooked_root.generic_string() + R"(",
      "jobs": [
        {
          "id": "scene.base",
          "type": "gltf",
          "source": "Scenes/showcase_scene.gltf"
        },
        {
          "id": "input.rotate",
          "depends_on": ["physics.sidecar.main"],
          "type": "input",
          "source": "Input/rotate.input.json",
          "virtual_path": "/.cooked/Input/rotate.input"
        },
        {
          "id": "physics.sidecar.main",
          "type": "physics-sidecar",
          "source": "Scenes/showcase_scene.physics.json",
          "target_scene_virtual_path": "/.cooked/Scenes/showcase_scene.oscene",
          "bindings": {}
        }
      ]
    })");

  const auto result = RunBatch(manifest_path);
  EXPECT_FALSE(result.has_value());

  const auto messages = Messages();
  EXPECT_THAT(messages,
    ::testing::Not(::testing::HasSubstr("physics.manifest.dependency_cycle")));
}

NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  PhysicsSidecarSourceWithoutTopLevelBindingsIsRejectedDuringInference)
{
  const auto root = MakeScenarioDir("physics_sidecar_missing_bindings_root");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";
  const auto sidecar_source
    = root / "Scenes" / "missing_bindings.physics-sidecar.json";

  WriteText(sidecar_source, R"({"rigid_bodies": []})");

  WriteText(manifest_path,
    std::string { R"({
      "version": 1,
      "output": ")" }
      + cooked_root.generic_string() + R"(",
      "jobs": [
        {
          "id": "physics.sidecar.main",
          "type": "physics-sidecar",
          "source": "Scenes/missing_bindings.physics-sidecar.json",
          "target_scene_virtual_path": "/.cooked/Scenes/level.oscene"
        }
      ]
    })");

  const auto result = RunBatch(manifest_path);
  EXPECT_FALSE(result.has_value());

  const auto messages = Messages();
  EXPECT_THAT(messages,
    ::testing::HasSubstr("physics.manifest.dependency_inference_failed"));
  EXPECT_THAT(messages, ::testing::HasSubstr("top-level 'bindings'"));
}

NOLINT_TEST_F(BatchCommandPhysicsDagTest, NonPhysicsCycleStillUsesLegacyCode)
{
  const auto root = MakeScenarioDir("input_dep_cycle");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";

  WriteText(manifest_path,
    std::string { R"({
      "version": 1,
      "output": ")" }
      + cooked_root.generic_string() + R"(",
      "jobs": [
        {
          "id": "input.a",
          "depends_on": ["input.b"],
          "type": "input",
          "source": "Input/a.input.json",
          "virtual_path": "/.cooked/Input/a.input"
        },
        {
          "id": "input.b",
          "depends_on": ["input.a"],
          "type": "input",
          "source": "Input/b.input.json",
          "virtual_path": "/.cooked/Input/b.input"
        }
      ]
    })");

  const auto result = RunBatch(manifest_path);
  EXPECT_FALSE(result.has_value());

  const auto messages = Messages();
  EXPECT_THAT(messages, ::testing::HasSubstr("input.manifest.dep_cycle"));
}

NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  SameCookedRootScriptSidecarsInjectSerializationFence)
{
  const auto root = MakeScenarioDir("script_sidecar_same_root_fence");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";

  WriteText(manifest_path,
    std::string { R"({
      "version": 1,
      "output": ")" }
      + cooked_root.generic_string() + R"(",
      "jobs": [
        {
          "id": "script.sidecar.a",
          "depends_on": ["script.sidecar.b"],
          "type": "script-sidecar",
          "source": "Scenes/a.script-sidecar.json",
          "target_scene_virtual_path": "/.cooked/Scenes/a.oscene"
        },
        {
          "id": "script.sidecar.b",
          "type": "script-sidecar",
          "source": "Scenes/b.script-sidecar.json",
          "target_scene_virtual_path": "/.cooked/Scenes/b.oscene"
        }
      ]
    })");

  const auto result = RunBatch(manifest_path);
  EXPECT_FALSE(result.has_value());

  const auto messages = Messages();
  EXPECT_THAT(messages, ::testing::HasSubstr("input.manifest.dep_cycle"));
}

NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  DuplicateMissingDependencyIdEmitsSingleMissingTargetDiagnostic)
{
  const auto root = MakeScenarioDir("physics_duplicate_missing_dep");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";
  const auto material_source = root / "Physics" / "mat.physics-material.json";

  WriteText(material_source,
    R"({
      "name": "mat",
      "virtual_path": "/.cooked/Physics/Materials/mat.opmat",
      "static_friction": 0.6,
      "dynamic_friction": 0.6,
      "restitution": 0.1,
      "density": 1.0
    })");

  WriteText(manifest_path,
    std::string { R"({
      "version": 1,
      "output": ")" }
      + cooked_root.generic_string() + R"(",
      "jobs": [
        {
          "id": "physics.material.main",
          "type": "physics-material-descriptor",
          "source": "Physics/mat.physics-material.json",
          "virtual_path": "/.cooked/Physics/Materials/mat.opmat",
          "depends_on": ["missing.id", "missing.id"]
        }
      ]
    })");

  const auto result = RunBatch(manifest_path);
  EXPECT_FALSE(result.has_value());

  const auto messages = Messages();
  EXPECT_EQ(
    CountOccurrences(messages, "physics.manifest.dependency_missing_target"),
    1U);
}

NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  PhysicsDuplicateJobIdsUsePhysicsManifestDiagnosticNamespace)
{
  const auto root = MakeScenarioDir("physics_duplicate_job_ids");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";
  const auto mat_a = root / "Physics" / "mat_a.physics-material.json";
  const auto mat_b = root / "Physics" / "mat_b.physics-material.json";

  WriteText(mat_a,
    R"({
      "name": "mat_a",
      "virtual_path": "/.cooked/Physics/Materials/mat_a.opmat",
      "static_friction": 0.6,
      "dynamic_friction": 0.6,
      "restitution": 0.1,
      "density": 1.0
    })");
  WriteText(mat_b,
    R"({
      "name": "mat_b",
      "virtual_path": "/.cooked/Physics/Materials/mat_b.opmat",
      "static_friction": 0.7,
      "dynamic_friction": 0.7,
      "restitution": 0.2,
      "density": 1.0
    })");

  WriteText(manifest_path,
    std::string { R"({
      "version": 1,
      "output": ")" }
      + cooked_root.generic_string() + R"(",
      "jobs": [
        {
          "id": "dup.id",
          "type": "physics-material-descriptor",
          "source": "Physics/mat_a.physics-material.json",
          "virtual_path": "/.cooked/Physics/Materials/mat_a.opmat"
        },
        {
          "id": "dup.id",
          "type": "physics-material-descriptor",
          "source": "Physics/mat_b.physics-material.json",
          "virtual_path": "/.cooked/Physics/Materials/mat_b.opmat"
        }
      ]
    })");

  const auto result = RunBatch(manifest_path);
  EXPECT_FALSE(result.has_value());

  const auto messages = Messages();
  EXPECT_THAT(
    messages, ::testing::HasSubstr("physics.manifest.job_id_duplicate"));
  EXPECT_THAT(messages,
    ::testing::Not(::testing::HasSubstr("input.manifest.job_id_duplicate")));
}

} // namespace
