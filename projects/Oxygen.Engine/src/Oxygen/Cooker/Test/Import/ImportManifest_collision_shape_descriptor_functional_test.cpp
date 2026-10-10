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

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Cooker/Test/Support/TestValues.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::EffectiveContentHashingEnabled;
using oxygen::content::import::ImportManifest;
using oxygen::cooker::test::kContentHashingDefault;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::WriteText;

NOLINT_TEST(ImportManifestCollisionShapeDescriptorTest,
  BuildsCollisionShapeDescriptorRequestWithDefaultsAndDescriptorOverrides)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "builds_request" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Physics" / "floor_box.shape.json";
  WriteText(descriptor_path,
    R"({
      "name": "floor_box",
      "content_hashing": false,
      "shape_type": "box",
      "material_ref": "/.cooked/Physics/Materials/ground.opmat",
      "half_extents": [25.0, 0.5, 25.0],
      "virtual_path": "/.cooked/Physics/Shapes/floor_box.ocshape"
    })");

  const auto cooked_root = (root / ".cooked").generic_string();
  const auto manifest_json = std::string { R"({
      "version": 1,
      "output": ")" }
    + cooked_root + R"(",
      "defaults": {
        "collision_shape_descriptor": {
          "content_hashing": true,
          "name": "default-shape-name"
        }
      },
      "jobs": [
        {
          "type": "collision-shape-descriptor",
          "source": "Physics/floor_box.shape.json",
          "name": "floor_box_job",
          "content_hashing": true
        }
      ]
    })";
  WriteText(manifest_path, manifest_json);

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request) << request_errors.str();
  ASSERT_HAS_VALUE(request->cooked_root);
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(request->job_name, std::optional<std::string> { "floor_box_job" });
  ASSERT_HAS_VALUE(request->collision_shape_descriptor);

  // Descriptor-level content_hashing overrides defaults/job settings.
  // Release must hash authored content even when the descriptor opts out.
  EXPECT_EQ(request->options.with_content_hashing, kContentHashingDefault);

  const auto normalized = json::parse(
    request->collision_shape_descriptor->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "floor_box");
  EXPECT_EQ(normalized.at("shape_type").get<std::string>(), "box");
}

NOLINT_TEST(ImportManifestCollisionShapeDescriptorTest,
  CollectsAndPropagatesCollisionShapeDescriptorDependencies)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "collects_dependencies" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Physics" / "floor_box.shape.json";
  WriteText(descriptor_path,
    R"({
      "name": "floor_box",
      "shape_type": "box",
      "material_ref": "/.cooked/Physics/Materials/ground.opmat",
      "half_extents": [25.0, 0.5, 25.0],
      "virtual_path": "/.cooked/Physics/Shapes/floor_box.ocshape"
    })");

  WriteText(manifest_path,
    R"({
      "version": 1,
      "output": ")"
      + (manifest_path.parent_path() / "physics-shape-cooked").generic_string()
      + R"(",
      "jobs": [
        {
          "id": "physics.material.ground",
          "type": "physics-material-descriptor",
          "source": "Physics/ground.material.json"
        },
        {
          "id": "physics.shape.floor",
          "type": "collision-shape-descriptor",
          "source": "Physics/floor_box.shape.json",
          "depends_on": ["physics.material.ground"]
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 2U);
  EXPECT_EQ(manifest->jobs.at(1).id, "physics.shape.floor");
  ASSERT_EQ(manifest->jobs.at(1).depends_on.size(), 1U);
  EXPECT_EQ(manifest->jobs.at(1).depends_on.at(0), "physics.material.ground");

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(1).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request) << request_errors.str();
  ASSERT_HAS_VALUE(request->orchestration);
  EXPECT_EQ(request->orchestration->job_id, "physics.shape.floor");
  ASSERT_EQ(request->orchestration->depends_on.size(), 1U);
  EXPECT_EQ(
    request->orchestration->depends_on.at(0), "physics.material.ground");
}

NOLINT_TEST(ImportManifestCollisionShapeDescriptorTest,
  RejectsCollisionShapeDescriptorJobWithDisallowedKeys)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_disallowed_key" / "import_manifest.json";
  WriteText(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "type": "collision-shape-descriptor",
          "source": "Physics/floor_box.shape.json",
          "intent": "albedo"
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("manifest schema validation failed"));
}

NOLINT_TEST(ImportManifestCollisionShapeDescriptorTest,
  RejectsDescriptorPayloadThatViolatesDescriptorSchema)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_bad_descriptor_payload" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Physics" / "bad.shape.json";
  WriteText(descriptor_path,
    R"({
      "name": "bad_shape",
      "shape_type": "sphere",
      "material_ref": "/.cooked/Physics/Materials/ground.opmat",
      "radius": 1.0,
      "unexpected": true
    })");

  WriteText(manifest_path,
    R"({
      "version": 1,
      "output": ")"
      + (manifest_path.parent_path() / "physics-shape-cooked").generic_string()
      + R"(",
      "jobs": [
        {
          "type": "collision-shape-descriptor",
          "source": "Physics/bad.shape.json"
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(request_errors.str(),
    ::testing::HasSubstr("physics.shape.descriptor.schema_validation_failed"));
}

} // namespace
