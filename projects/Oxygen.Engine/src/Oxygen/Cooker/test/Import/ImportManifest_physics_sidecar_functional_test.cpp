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
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Core/Meta/Physics/Backend.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {

using oxygen::content::import::ImportManifest;
using oxygen::cooker::test::WriteText;
using oxygen::testing::ScopedTempDir;

NOLINT_TEST(
  ImportManifestPhysicsSidecarTest, AcceptsInlineBindingsForPhysicsSidecarJob)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "inline_bindings" / "import_manifest.json";

  WriteText(manifest_path,
    R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "jobs": [
        {
          "type": "physics-sidecar",
          "output": "oxygen_manifest_physics_sidecar_root",
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
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  EXPECT_TRUE(manifest->jobs.at(0).physics_sidecar.source_path.empty());
  EXPECT_FALSE(
    manifest->jobs.at(0).physics_sidecar.inline_bindings_json.empty());

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request) << request_errors.str();
  ASSERT_HAS_VALUE(request->physics);
  EXPECT_FALSE(request->physics->inline_bindings_json.empty());
}

//! The job chooses the backend it cooks for, and the defaults can too.
NOLINT_TEST(ImportManifestPhysicsSidecarTest, ReadsThePhysicsBackend)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "physics_backend" / "import_manifest.json";

  WriteText(manifest_path,
    R"({
      "version": 1,
      "output": ".cooked",
      "mounts": [{ "name": "Scenes", "path": "." }],
      "defaults": { "physics_sidecar": { "physics_backend": "none" } },
      "jobs": [
        {
          "type": "physics-sidecar",
          "physics_backend": "physx",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene",
          "bindings": {}
        },
        {
          "type": "physics-sidecar",
          "target_scene_virtual_path": "/Scenes/Other.oscene",
          "bindings": {}
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 2U);

  using oxygen::core::meta::physics::PhysicsBackend;
  EXPECT_EQ(manifest->jobs.at(0).physics_sidecar.physics_backend,
    PhysicsBackend::kPhysX);
  EXPECT_EQ(manifest->jobs.at(1).physics_sidecar.physics_backend,
    PhysicsBackend::kNone);
  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request) << request_errors.str();
  EXPECT_EQ(request->options.physics.backend, PhysicsBackend::kPhysX);
}

NOLINT_TEST(ImportManifestPhysicsSidecarTest,
  RejectsPhysicsSidecarJobWhenSourceAndBindingsBothSpecified)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "both_source_and_bindings" / "import_manifest.json";

  WriteText(manifest_path,
    R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "jobs": [
        {
          "type": "physics-sidecar",
          "source": "scene.physics-sidecar.json",
          "output": "oxygen_manifest_physics_sidecar_root",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene",
          "bindings": {
            "rigid_bodies": []
          }
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("manifest.schema_validation_failed"));
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
      "mounts": [{ "name": "Content", "path": "." }],
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
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("manifest.schema_validation_failed"));
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
      "mounts": [{ "name": "Content", "path": "." }],
      "jobs": [
        {
          "id": "physics.main",
          "depends_on": ["scene.main"],
          "type": "physics-sidecar",
          "source": "scene.physics-sidecar.json",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene"
        },
        {
          "id": "scene.main",
          "type": "scene-descriptor",
          "source": "scene.oscene.json",
          "virtual_path": "/Scenes/TestScene.oscene"
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 2U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request) << request_errors.str();
  ASSERT_HAS_VALUE(request->orchestration);
  EXPECT_EQ(request->orchestration->job_id, "physics.main");
  ASSERT_EQ(request->orchestration->depends_on.size(), 1U);
  EXPECT_EQ(request->orchestration->depends_on.at(0), "scene.main");
}

NOLINT_TEST(ImportManifestPhysicsSidecarTest,
  DefaultsOutputOverridesTopLevelOutputForPhysicsSidecarJobs)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "defaults_over_top_level_output" / "import_manifest.json";
  const auto defaults_output
    = manifest_path.parent_path() / "oxygen_manifest_output_defaults_physics";

  WriteText(manifest_path,
    R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "output": "oxygen_manifest_output_top_physics",
      "defaults": {
        "physics_sidecar": {
          "output": "oxygen_manifest_output_defaults_physics"
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
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request) << request_errors.str();
  ASSERT_HAS_VALUE(request->cooked_root);
  EXPECT_EQ(request->cooked_root->lexically_normal(),
    defaults_output.lexically_normal());
}

} // namespace
