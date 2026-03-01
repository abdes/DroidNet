//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/ImportManifest.h>

namespace {

using oxygen::content::import::ImportManifest;

auto JsonPath(const std::filesystem::path& path) -> std::string
{
  return path.lexically_normal().generic_string();
}

auto MakeManifestPath(const std::string_view stem) -> std::filesystem::path
{
  auto dir = std::filesystem::temp_directory_path()
    / "oxygen_manifest_physics_sidecar";
  dir /= std::filesystem::path { std::string { stem } };
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  return dir / "import_manifest.json";
}

auto WriteManifestFile(
  const std::filesystem::path& path, const std::string& json_text) -> void
{
  auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out.is_open());
  out << json_text;
}

NOLINT_TEST(
  ImportManifestPhysicsSidecarTest, AcceptsInlineBindingsForPhysicsSidecarJob)
{
  const auto cooked_root = std::filesystem::temp_directory_path()
    / "oxygen_manifest_physics_sidecar_root";
  const auto cooked_root_json = JsonPath(cooked_root);
  const auto manifest_path = MakeManifestPath("inline_bindings");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "type": "physics-sidecar",
          "output": ")"
      + cooked_root_json + R"(",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene",
          "bindings": {
            "rigid_bodies": [
              {
                "node_index": 0,
                "shape_virtual_path": "/PhysicsShapes/test_shape.ocshape",
                "material_virtual_path": "/PhysicsMaterials/default.opmat"
              }
            ]
          }
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  EXPECT_TRUE(manifest->jobs[0].physics_sidecar.source_path.empty());
  EXPECT_FALSE(manifest->jobs[0].physics_sidecar.inline_bindings_json.empty());

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs[0].BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  ASSERT_TRUE(request->physics.has_value());
  EXPECT_FALSE(request->physics->inline_bindings_json.empty());
}

NOLINT_TEST(ImportManifestPhysicsSidecarTest,
  RejectsPhysicsSidecarJobWhenSourceAndBindingsBothSpecified)
{
  const auto cooked_root = std::filesystem::temp_directory_path()
    / "oxygen_manifest_physics_sidecar_root";
  const auto cooked_root_json = JsonPath(cooked_root);
  const auto manifest_path = MakeManifestPath("both_source_and_bindings");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "type": "physics-sidecar",
          "source": "scene.physics-sidecar.json",
          "output": ")"
      + cooked_root_json + R"(",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene",
          "bindings": {
            "rigid_bodies": []
          }
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_TRUE(errors.str().find("physics-sidecar job requires exactly one of "
                                "'source' or 'bindings'")
    != std::string::npos);
}

NOLINT_TEST(
  ImportManifestPhysicsSidecarTest, RejectsPhysicsSidecarJobWithDisallowedKeys)
{
  const auto manifest_path = MakeManifestPath("rejects_extra_keys");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "type": "physics-sidecar",
          "source": "scene.physics-sidecar.json",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene",
          "compile": true
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_TRUE(
    errors.str().find("physics.manifest.key_not_allowed") != std::string::npos);
}

NOLINT_TEST(ImportManifestPhysicsSidecarTest,
  PreservesPhysicsSidecarOrchestrationMetadata)
{
  const auto manifest_path = MakeManifestPath("orchestration_metadata");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "id": "physics.main",
          "depends_on": ["scene.main"],
          "type": "physics-sidecar",
          "source": "scene.physics-sidecar.json",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene"
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs[0].BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  ASSERT_TRUE(request->orchestration.has_value());
  EXPECT_EQ(request->orchestration->job_id, "physics.main");
  ASSERT_EQ(request->orchestration->depends_on.size(), 1U);
  EXPECT_EQ(request->orchestration->depends_on[0], "scene.main");
}

NOLINT_TEST(ImportManifestPhysicsSidecarTest,
  DefaultsOutputOverridesTopLevelOutputForPhysicsSidecarJobs)
{
  const auto top_level_output = std::filesystem::temp_directory_path()
    / "oxygen_manifest_output_top_physics";
  const auto defaults_output = std::filesystem::temp_directory_path()
    / "oxygen_manifest_output_defaults_physics";
  const auto manifest_path = MakeManifestPath("defaults_over_top_level_output");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "output": ")"
      + JsonPath(top_level_output) + R"(",
      "defaults": {
        "physics_sidecar": {
          "output": ")"
      + JsonPath(defaults_output) + R"("
        }
      },
      "jobs": [
        {
          "type": "physics-sidecar",
          "source": "scene.physics-sidecar.json",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene"
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs[0].BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_EQ(request->cooked_root->lexically_normal(),
    defaults_output.lexically_normal());
}

} // namespace
