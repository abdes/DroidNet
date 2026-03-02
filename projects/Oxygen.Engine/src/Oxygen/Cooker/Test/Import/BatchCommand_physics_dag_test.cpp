//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/CommandLineContext.h> // used
#include <Oxygen/Clap/Fluent/CliBuilder.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Tools/ImportTool/BatchCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>

namespace {

using oxygen::content::import::AsyncImportService;
using oxygen::content::import::tool::BatchCommand;
using oxygen::content::import::tool::GlobalOptions;
using oxygen::content::import::tool::IMessageWriter;

class CapturingWriter final : public IMessageWriter {
public:
  auto Error(const std::string_view message) -> bool override
  {
    messages_.push_back(std::string(message));
    return true;
  }

  auto Warning(const std::string_view message) -> bool override
  {
    messages_.push_back(std::string(message));
    return true;
  }

  auto Info(const std::string_view message) -> bool override
  {
    messages_.push_back(std::string(message));
    return true;
  }

  auto Report(const std::string_view message) -> bool override
  {
    messages_.push_back(std::string(message));
    return true;
  }

  auto Progress(const std::string_view message) -> bool override
  {
    messages_.push_back(std::string(message));
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
  std::vector<std::string> messages_ {};
};

auto WriteTextFile(
  const std::filesystem::path& path, const std::string_view text) -> void
{
  std::filesystem::create_directories(path.parent_path());
  auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out.is_open());
  out << text;
}

auto MakeScenarioDir(const std::string_view scenario) -> std::filesystem::path
{
  auto dir
    = std::filesystem::temp_directory_path() / "oxygen_batch_command_p6_tests";
  dir /= std::filesystem::path(std::string(scenario));
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  return dir;
}

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

class BatchCommandPhysicsDagTest : public testing::Test {
protected:
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
    = std::nullopt) -> std::expected<void, std::error_code>
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
    const char* argv[]
      = { "tool", "batch", "--manifest", manifest_arg.c_str() };
    (void)cli->Parse(static_cast<int>(std::size(argv)), argv);

    return command.Run();
  }

  [[nodiscard]] auto Messages() const -> std::string
  {
    return writer_->JoinedMessages();
  }

private:
  GlobalOptions options_ {};
  std::unique_ptr<CapturingWriter> writer_ {};
  std::unique_ptr<AsyncImportService> service_ {};
};

NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  PhysicsSidecarUnresolvedInferredRefsEmitDependencyUnresolvedDiagnostic)
{
  const auto root = MakeScenarioDir("physics_unresolved_refs");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";

  WriteTextFile(manifest_path,
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
  EXPECT_NE(
    messages.find("physics.manifest.dependency_unresolved"), std::string::npos);
}

NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  PhysicsSidecarDuplicateUnresolvedRefsEmitSingleDependencyUnresolvedDiagnostic)
{
  const auto root = MakeScenarioDir("physics_unresolved_duplicate_refs");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";

  WriteTextFile(manifest_path,
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

  WriteTextFile(mat_a,
    R"({
      "name": "shared_a",
      "virtual_path": "/.cooked/Physics/Materials/shared.opmat",
      "friction": 0.8,
      "restitution": 0.1,
      "density": 1.0
    })");
  WriteTextFile(mat_b,
    R"({
      "name": "shared_b",
      "virtual_path": "/.cooked/Physics/Materials/shared.opmat",
      "friction": 0.7,
      "restitution": 0.2,
      "density": 1.0
    })");
  WriteTextFile(shape,
    R"({
      "name": "floor_shape",
      "shape_type": "box",
      "half_extents": [10.0, 1.0, 10.0],
      "material_ref": "/.cooked/Physics/Materials/shared.opmat",
      "virtual_path": "/.cooked/Physics/Shapes/floor.ocshape"
    })");

  WriteTextFile(manifest_path,
    std::string { R"({
      "version": 1,
      "output": ")" }
      + cooked_root.generic_string() + R"(",
      "jobs": [
        {
          "id": "physics.material.shared_a",
          "type": "physics-material-descriptor",
          "source": "Physics/shared_a.physics-material.json"
        },
        {
          "id": "physics.material.shared_b",
          "type": "physics-material-descriptor",
          "source": "Physics/shared_b.physics-material.json"
        },
        {
          "id": "physics.shape.floor",
          "type": "collision-shape-descriptor",
          "source": "Physics/floor.physics-shape.json"
        }
      ]
    })");

  const auto result = RunBatch(manifest_path);
  EXPECT_FALSE(result.has_value());

  const auto messages = Messages();
  EXPECT_NE(
    messages.find("physics.manifest.dependency_ambiguous"), std::string::npos);
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

  WriteTextFile(mat_a,
    R"({
      "name": "shared_a",
      "virtual_path": "/.cooked/Physics/Materials/shared.opmat",
      "friction": 0.8,
      "restitution": 0.1,
      "density": 1.0
    })");
  WriteTextFile(mat_b,
    R"({
      "name": "shared_b",
      "virtual_path": "/.cooked/Physics/Materials/shared.opmat",
      "friction": 0.7,
      "restitution": 0.2,
      "density": 1.0
    })");
  WriteTextFile(mat_base,
    R"({
      "name": "base",
      "virtual_path": "/.cooked/Physics/Materials/base.opmat",
      "friction": 0.5,
      "restitution": 0.1,
      "density": 1.0
    })");
  WriteTextFile(shape_a,
    R"({
      "name": "floor_shape_a",
      "shape_type": "box",
      "half_extents": [10.0, 1.0, 10.0],
      "material_ref": "/.cooked/Physics/Materials/base.opmat",
      "virtual_path": "/.cooked/Physics/Shapes/floor.ocshape"
    })");
  WriteTextFile(shape_b,
    R"({
      "name": "floor_shape_b",
      "shape_type": "box",
      "half_extents": [10.0, 1.0, 10.0],
      "material_ref": "/.cooked/Physics/Materials/base.opmat",
      "virtual_path": "/.cooked/Physics/Shapes/floor.ocshape"
    })");

  WriteTextFile(manifest_path,
    std::string { R"({
      "version": 1,
      "output": ")" }
      + cooked_root.generic_string() + R"(",
      "jobs": [
        {
          "id": "physics.material.shared_a",
          "type": "physics-material-descriptor",
          "source": "Physics/shared_a.physics-material.json"
        },
        {
          "id": "physics.material.shared_b",
          "type": "physics-material-descriptor",
          "source": "Physics/shared_b.physics-material.json"
        },
        {
          "id": "physics.material.base",
          "type": "physics-material-descriptor",
          "source": "Physics/base.physics-material.json"
        },
        {
          "id": "physics.shape.floor_a",
          "type": "collision-shape-descriptor",
          "source": "Physics/floor_a.physics-shape.json"
        },
        {
          "id": "physics.shape.floor_b",
          "type": "collision-shape-descriptor",
          "source": "Physics/floor_b.physics-shape.json"
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

  WriteTextFile(mat_a,
    R"({
      "name": "mat_a",
      "virtual_path": "/.cooked/Physics/Materials/mat_a.opmat",
      "friction": 0.5,
      "restitution": 0.1,
      "density": 1.0
    })");
  WriteTextFile(mat_b,
    R"({
      "name": "mat_b",
      "virtual_path": "/.cooked/Physics/Materials/mat_b.opmat",
      "friction": 0.5,
      "restitution": 0.1,
      "density": 1.0
    })");

  WriteTextFile(manifest_path,
    std::string { R"({
      "version": 1,
      "output": ")" }
      + cooked_root.generic_string() + R"(",
      "jobs": [
        {
          "id": "mat.a",
          "depends_on": ["mat.b"],
          "type": "physics-material-descriptor",
          "source": "Physics/mat_a.physics-material.json"
        },
        {
          "id": "mat.b",
          "depends_on": ["mat.a"],
          "type": "physics-material-descriptor",
          "source": "Physics/mat_b.physics-material.json"
        }
      ]
    })");

  const auto result = RunBatch(manifest_path);
  EXPECT_FALSE(result.has_value());

  const auto messages = Messages();
  EXPECT_NE(
    messages.find("physics.manifest.dependency_cycle"), std::string::npos);
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

  WriteTextFile(scene_source, "{}");
  WriteTextFile(input_source, "{}");
  WriteTextFile(sidecar_source, R"({"bindings": {}})");

  WriteTextFile(manifest_path,
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
          "source": "Input/rotate.input.json"
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
  EXPECT_EQ(
    messages.find("physics.manifest.dependency_cycle"), std::string::npos);
}

NOLINT_TEST_F(BatchCommandPhysicsDagTest, NonPhysicsCycleStillUsesLegacyCode)
{
  const auto root = MakeScenarioDir("input_dep_cycle");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";

  WriteTextFile(manifest_path,
    std::string { R"({
      "version": 1,
      "output": ")" }
      + cooked_root.generic_string() + R"(",
      "jobs": [
        {
          "id": "input.a",
          "depends_on": ["input.b"],
          "type": "input",
          "source": "Input/a.input.json"
        },
        {
          "id": "input.b",
          "depends_on": ["input.a"],
          "type": "input",
          "source": "Input/b.input.json"
        }
      ]
    })");

  const auto result = RunBatch(manifest_path);
  EXPECT_FALSE(result.has_value());

  const auto messages = Messages();
  EXPECT_NE(messages.find("input.manifest.dep_cycle"), std::string::npos);
}

NOLINT_TEST_F(BatchCommandPhysicsDagTest,
  DuplicateMissingDependencyIdEmitsSingleMissingTargetDiagnostic)
{
  const auto root = MakeScenarioDir("physics_duplicate_missing_dep");
  const auto cooked_root = root / ".cooked";
  const auto manifest_path = root / "import-manifest.json";
  const auto material_source = root / "Physics" / "mat.physics-material.json";

  WriteTextFile(material_source,
    R"({
      "name": "mat",
      "virtual_path": "/.cooked/Physics/Materials/mat.opmat",
      "friction": 0.6,
      "restitution": 0.1,
      "density": 1.0
    })");

  WriteTextFile(manifest_path,
    std::string { R"({
      "version": 1,
      "output": ")" }
      + cooked_root.generic_string() + R"(",
      "jobs": [
        {
          "id": "physics.material.main",
          "type": "physics-material-descriptor",
          "source": "Physics/mat.physics-material.json",
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

  WriteTextFile(mat_a,
    R"({
      "name": "mat_a",
      "virtual_path": "/.cooked/Physics/Materials/mat_a.opmat",
      "friction": 0.6,
      "restitution": 0.1,
      "density": 1.0
    })");
  WriteTextFile(mat_b,
    R"({
      "name": "mat_b",
      "virtual_path": "/.cooked/Physics/Materials/mat_b.opmat",
      "friction": 0.7,
      "restitution": 0.2,
      "density": 1.0
    })");

  WriteTextFile(manifest_path,
    std::string { R"({
      "version": 1,
      "output": ")" }
      + cooked_root.generic_string() + R"(",
      "jobs": [
        {
          "id": "dup.id",
          "type": "physics-material-descriptor",
          "source": "Physics/mat_a.physics-material.json"
        },
        {
          "id": "dup.id",
          "type": "physics-material-descriptor",
          "source": "Physics/mat_b.physics-material.json"
        }
      ]
    })");

  const auto result = RunBatch(manifest_path);
  EXPECT_FALSE(result.has_value());

  const auto messages = Messages();
  EXPECT_NE(
    messages.find("physics.manifest.job_id_duplicate"), std::string::npos);
  EXPECT_EQ(
    messages.find("input.manifest.job_id_duplicate"), std::string::npos);
}

} // namespace
