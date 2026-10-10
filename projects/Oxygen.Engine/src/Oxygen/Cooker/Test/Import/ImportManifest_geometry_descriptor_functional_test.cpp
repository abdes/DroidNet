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
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::EffectiveContentHashingEnabled;
using oxygen::content::import::ImportManifest;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::WriteText;

NOLINT_TEST(ImportManifestGeometryDescriptorTest,
  BuildsGeometryDescriptorRequestWithDefaultsAndDescriptorOverrides)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "builds_geometry_descriptor" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Geometry" / "cube.geometry.json";
  WriteText(descriptor_path,
    R"({
      "name": "ProcCube",
      "content_hashing": false,
      "bounds": { "min": [-0.5, -0.5, -0.5], "max": [0.5, 0.5, 0.5] },
      "lods": [
        {
          "name": "LOD0",
          "mesh_type": "procedural",
          "bounds": { "min": [-0.5, -0.5, -0.5], "max": [0.5, 0.5, 0.5] },
          "procedural": { "generator": "Cube", "mesh_name": "CubeMesh" },
          "submeshes": [
            {
              "slot_id": "018f8f8f-1111-7111-8111-111111111111",
              "material_ref": "/.cooked/Materials/default.omat",
              "views": [ { "view_ref": "__all__" } ]
            }
          ]
        }
      ]
    })");

  const auto cooked_root = (root / ".cooked").generic_string();
  const auto manifest_json = std::string { R"({
      "version": 1,
      "output": ")" }
    + cooked_root + R"(",
      "defaults": {
        "geometry_descriptor": {
          "content_hashing": true,
          "cooked_context_roots": ["Libraries/Materials"],
          "name": "default-geometry-name"
        }
      },
      "jobs": [
        {
          "type": "geometry-descriptor",
          "source": "Geometry/cube.geometry.json",
          "name": "cube-job",
          "content_hashing": true
        }
      ]
    })";
  WriteText(manifest_path, manifest_json);

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value())
    << "Expected manifest to contain a value" << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value())
    << "Expected request to contain a value" << request_errors.str();
  ASSERT_TRUE(request->cooked_root.has_value())
    << "Expected request->cooked_root to contain a value";
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(request->job_name, std::optional<std::string> { "cube-job" });
  ASSERT_TRUE(request->geometry_descriptor.has_value())
    << "Expected request->geometry_descriptor to contain a value";
  ASSERT_EQ(request->cooked_context_roots.size(), 1U);
  EXPECT_EQ(request->cooked_context_roots.at(0), root / "Libraries/Materials");

  EXPECT_EQ(request->options.with_content_hashing,
    EffectiveContentHashingEnabled(false));

  const auto normalized
    = json::parse(request->geometry_descriptor->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "ProcCube");
}

NOLINT_TEST(ImportManifestGeometryDescriptorTest,
  CollectsAndPropagatesGeometryDescriptorDependencies)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "collects_geometry_dependencies" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Geometry" / "cube.geometry.json";
  WriteText(descriptor_path,
    R"({
      "name": "ProcCube",
      "bounds": { "min": [-0.5, -0.5, -0.5], "max": [0.5, 0.5, 0.5] },
      "lods": [
        {
          "name": "LOD0",
          "mesh_type": "procedural",
          "bounds": { "min": [-0.5, -0.5, -0.5], "max": [0.5, 0.5, 0.5] },
          "procedural": { "generator": "Cube", "mesh_name": "CubeMesh" },
          "submeshes": [
            {
              "slot_id": "018f8f8f-1111-7111-8111-111111111111",
              "material_ref": "/.cooked/Materials/default.omat",
              "views": [ { "view_ref": "__all__" } ]
            }
          ]
        }
      ]
    })");

  WriteText(manifest_path,
    R"({
      "version": 1,
      "output": ")"
      + (manifest_path.parent_path() / "geometry-cooked").generic_string()
      + R"(",
      "jobs": [
        {
          "id": "shared.buffers",
          "type": "buffer-container",
          "source": "Buffers/shared.buffers.json"
        },
        {
          "id": "proc.cube",
          "type": "geometry-descriptor",
          "source": "Geometry/cube.geometry.json",
          "depends_on": ["shared.buffers"]
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value())
    << "Expected manifest to contain a value" << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 2U);
  EXPECT_EQ(manifest->jobs.at(1).id, "proc.cube");
  ASSERT_EQ(manifest->jobs.at(1).depends_on.size(), 1U);
  EXPECT_EQ(manifest->jobs.at(1).depends_on.at(0), "shared.buffers");

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(1).BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value())
    << "Expected request to contain a value" << request_errors.str();
  ASSERT_TRUE(request->orchestration.has_value())
    << "Expected request->orchestration to contain a value";
  EXPECT_EQ(request->orchestration->job_id, "proc.cube");
  ASSERT_EQ(request->orchestration->depends_on.size(), 1U);
  EXPECT_EQ(request->orchestration->depends_on.at(0), "shared.buffers");
}

NOLINT_TEST(ImportManifestGeometryDescriptorTest,
  RejectsGeometryDescriptorJobWithDisallowedKeys)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_disallowed_key" / "import_manifest.json";
  WriteText(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "type": "geometry-descriptor",
          "source": "Geometry/cube.geometry.json",
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

} // namespace
