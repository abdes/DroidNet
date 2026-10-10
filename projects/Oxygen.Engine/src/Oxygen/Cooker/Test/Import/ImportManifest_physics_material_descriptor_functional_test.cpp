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

NOLINT_TEST(ImportManifestPhysicsMaterialDescriptorTest,
  BuildsPhysicsMaterialDescriptorRequestWithDefaultsAndDescriptorOverrides)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "builds_request" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Physics" / "ground.material.json";
  WriteText(descriptor_path,
    R"({
      "name": "ground",
      "content_hashing": false,
      "static_friction": 0.95,
      "dynamic_friction": 0.70,
      "restitution": 0.05,
      "density": 1500.0,
      "combine_mode_friction": "max",
      "combine_mode_restitution": "average",
      "virtual_path": "/.cooked/Physics/Materials/ground.opmat"
    })");

  const auto cooked_root = (root / ".cooked").generic_string();
  const auto manifest_json = std::string { R"({
      "version": 1,
      "output": ")" }
    + cooked_root + R"(",
      "defaults": {
        "physics_material_descriptor": {
          "content_hashing": true,
          "name": "default-material-name"
        }
      },
      "jobs": [
        {
          "type": "physics-material-descriptor",
          "source": "Physics/ground.material.json",
          "name": "ground_material_job",
          "content_hashing": true
        }
      ]
    })";
  WriteText(manifest_path, manifest_json);

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs[0].BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(
    request->job_name, std::optional<std::string> { "ground_material_job" });
  ASSERT_TRUE(request->physics_material_descriptor.has_value());

  // Descriptor-level content_hashing overrides manifest defaults/job settings.
  // Release must hash authored content even when the descriptor opts out.
  EXPECT_EQ(request->options.with_content_hashing, kContentHashingDefault);

  const auto normalized = json::parse(
    request->physics_material_descriptor->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "ground");
  EXPECT_EQ(normalized.at("density").get<double>(), 1500.0);
}

NOLINT_TEST(ImportManifestPhysicsMaterialDescriptorTest,
  CollectsAndPropagatesPhysicsMaterialDescriptorDependencies)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "collects_dependencies" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Physics" / "ground.material.json";
  WriteText(descriptor_path,
    R"({
      "name": "ground",
      "static_friction": 0.95,
      "dynamic_friction": 0.70,
      "restitution": 0.05,
      "density": 1500.0,
      "virtual_path": "/.cooked/Physics/Materials/ground.opmat"
    })");

  WriteText(manifest_path,
    R"({
      "version": 1,
      "output": ")"
      + (manifest_path.parent_path() / "physics-material-cooked")
        .generic_string()
      + R"(",
      "jobs": [
        {
          "id": "buffers.shared",
          "type": "buffer-container",
          "source": "Buffers/shared.buffers.json"
        },
        {
          "id": "physics.material.ground",
          "type": "physics-material-descriptor",
          "source": "Physics/ground.material.json",
          "depends_on": ["buffers.shared"]
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 2U);
  EXPECT_EQ(manifest->jobs[1].id, "physics.material.ground");
  ASSERT_EQ(manifest->jobs[1].depends_on.size(), 1U);
  EXPECT_EQ(manifest->jobs[1].depends_on[0], "buffers.shared");

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs[1].BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  ASSERT_TRUE(request->orchestration.has_value());
  EXPECT_EQ(request->orchestration->job_id, "physics.material.ground");
  ASSERT_EQ(request->orchestration->depends_on.size(), 1U);
  EXPECT_EQ(request->orchestration->depends_on[0], "buffers.shared");
}

NOLINT_TEST(ImportManifestPhysicsMaterialDescriptorTest,
  RejectsPhysicsMaterialDescriptorJobWithDisallowedKeys)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_disallowed_key" / "import_manifest.json";
  WriteText(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "type": "physics-material-descriptor",
          "source": "Physics/ground.material.json",
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

NOLINT_TEST(ImportManifestPhysicsMaterialDescriptorTest,
  RejectsDescriptorPayloadThatViolatesDescriptorSchema)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_bad_descriptor_payload" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Physics" / "bad.material.json";
  WriteText(descriptor_path,
    R"({
      "name": "bad",
      "static_friction": 0.8,
      "unexpected": true
    })");

  WriteText(manifest_path,
    R"({
      "version": 1,
      "output": ")"
      + (manifest_path.parent_path() / "physics-material-cooked")
        .generic_string()
      + R"(",
      "jobs": [
        {
          "type": "physics-material-descriptor",
          "source": "Physics/bad.material.json"
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
  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(request_errors.str(),
    ::testing::HasSubstr(
      "physics.material.descriptor.schema_validation_failed"));
}

NOLINT_TEST(ImportManifestPhysicsMaterialDescriptorTest,
  RejectsLegacyPhysicsResourceDescriptorJobType)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_legacy_job_type" / "import_manifest.json";
  WriteText(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "type": "physics-resource-descriptor",
          "source": "Physics/legacy.resource.json"
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

} // namespace
