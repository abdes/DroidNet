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

#include <nlohmann/json.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>

namespace {

using nlohmann::json;
using oxygen::content::import::EffectiveContentHashingEnabled;
using oxygen::content::import::ImportManifest;

auto MakeManifestPath(const std::string_view stem) -> std::filesystem::path
{
  auto dir = std::filesystem::temp_directory_path()
    / "oxygen_manifest_physics_material_descriptor";
  dir /= std::filesystem::path { std::string { stem } };
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  return dir / "import_manifest.json";
}

auto WriteTextFile(
  const std::filesystem::path& path, const std::string_view text) -> void
{
  std::filesystem::create_directories(path.parent_path());
  auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out.is_open());
  out << text;
}

NOLINT_TEST(ImportManifestPhysicsMaterialDescriptorTest,
  BuildsPhysicsMaterialDescriptorRequestWithDefaultsAndDescriptorOverrides)
{
  const auto manifest_path = MakeManifestPath("builds_request");
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Physics" / "ground.material.json";
  WriteTextFile(descriptor_path,
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
  WriteTextFile(manifest_path, manifest_json);

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
  EXPECT_FALSE(
    EffectiveContentHashingEnabled(request->options.with_content_hashing));

  const auto normalized = json::parse(
    request->physics_material_descriptor->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "ground");
  EXPECT_EQ(normalized.at("density").get<double>(), 1500.0);
}

NOLINT_TEST(ImportManifestPhysicsMaterialDescriptorTest,
  CollectsAndPropagatesPhysicsMaterialDescriptorDependencies)
{
  const auto manifest_path = MakeManifestPath("collects_dependencies");
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Physics" / "ground.material.json";
  WriteTextFile(descriptor_path,
    R"({
      "name": "ground",
      "static_friction": 0.95,
      "dynamic_friction": 0.70,
      "restitution": 0.05,
      "density": 1500.0,
      "virtual_path": "/.cooked/Physics/Materials/ground.opmat"
    })");

  WriteTextFile(manifest_path,
    R"({
      "version": 1,
      "output": "C:/tmp/physics-material-cooked",
      "jobs": [
        {
          "id": "physics.resource.ground_payload",
          "type": "physics-resource-descriptor",
          "source": "Physics/ground.resource.json"
        },
        {
          "id": "physics.material.ground",
          "type": "physics-material-descriptor",
          "source": "Physics/ground.material.json",
          "depends_on": ["physics.resource.ground_payload"]
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
  EXPECT_EQ(manifest->jobs[1].depends_on[0], "physics.resource.ground_payload");

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs[1].BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  ASSERT_TRUE(request->orchestration.has_value());
  EXPECT_EQ(request->orchestration->job_id, "physics.material.ground");
  ASSERT_EQ(request->orchestration->depends_on.size(), 1U);
  EXPECT_EQ(
    request->orchestration->depends_on[0], "physics.resource.ground_payload");
}

NOLINT_TEST(ImportManifestPhysicsMaterialDescriptorTest,
  RejectsPhysicsMaterialDescriptorJobWithDisallowedKeys)
{
  const auto manifest_path = MakeManifestPath("rejects_disallowed_key");
  WriteTextFile(manifest_path,
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
  EXPECT_TRUE(errors.str().find("manifest schema validation failed")
    != std::string::npos);
}

NOLINT_TEST(ImportManifestPhysicsMaterialDescriptorTest,
  RejectsDescriptorPayloadThatViolatesDescriptorSchema)
{
  const auto manifest_path = MakeManifestPath("rejects_bad_descriptor_payload");
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Physics" / "bad.material.json";
  WriteTextFile(descriptor_path,
    R"({
      "name": "bad",
      "static_friction": 0.8,
      "unexpected": true
    })");

  WriteTextFile(manifest_path,
    R"({
      "version": 1,
      "output": "C:/tmp/physics-material-cooked",
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
  EXPECT_TRUE(request_errors.str().find(
                "physics.material.descriptor.schema_validation_failed")
    != std::string::npos);
}

} // namespace
