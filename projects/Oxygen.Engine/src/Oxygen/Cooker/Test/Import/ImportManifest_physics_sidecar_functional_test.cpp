//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/ImportManifest.cpp

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::ImportManifest;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::WriteText;

auto JsonPath(const std::filesystem::path& path) -> std::string
{
  return path.lexically_normal().generic_string();
}

NOLINT_TEST(
  ImportManifestPhysicsSidecarTest, AcceptsInlineBindingsForPhysicsSidecarJob)
{
  const ScopedTempDir temp;
  const auto cooked_root = temp.Path() / "oxygen_manifest_physics_sidecar_root";
  const auto cooked_root_json = JsonPath(cooked_root);
  const auto manifest_path
    = temp.Path() / "inline_bindings" / "import_manifest.json";

  WriteText(manifest_path,
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
                "shape_ref": "/Physics/Shapes/test_shape.ocshape",
                "material_ref": "/Physics/Materials/default.opmat"
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
  const ScopedTempDir temp;
  const auto cooked_root = temp.Path() / "oxygen_manifest_physics_sidecar_root";
  const auto cooked_root_json = JsonPath(cooked_root);
  const auto manifest_path
    = temp.Path() / "both_source_and_bindings" / "import_manifest.json";

  WriteText(manifest_path,
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
  EXPECT_THAT(errors.str(),
    ::testing::HasSubstr("physics-sidecar job requires exactly one of "
                         "'source' or 'bindings'"));
}

NOLINT_TEST(
  ImportManifestPhysicsSidecarTest, RejectsPhysicsSidecarJobWithDisallowedKeys)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_extra_keys" / "import_manifest.json";

  WriteText(manifest_path,
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
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("physics.manifest.key_not_allowed"));
}

NOLINT_TEST(ImportManifestPhysicsSidecarTest,
  PreservesPhysicsSidecarOrchestrationMetadata)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "orchestration_metadata" / "import_manifest.json";

  WriteText(manifest_path,
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
  const ScopedTempDir temp;
  const auto top_level_output
    = temp.Path() / "oxygen_manifest_output_top_physics";
  const auto defaults_output
    = temp.Path() / "oxygen_manifest_output_defaults_physics";
  const auto manifest_path
    = temp.Path() / "defaults_over_top_level_output" / "import_manifest.json";

  WriteText(manifest_path,
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
