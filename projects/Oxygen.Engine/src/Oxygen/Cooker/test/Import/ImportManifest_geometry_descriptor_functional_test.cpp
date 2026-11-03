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
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {

using nlohmann::json;
using oxygen::content::import::ImportManifest;
using oxygen::cooker::test::WriteText;
using oxygen::testing::ScopedTempDir;

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
      "lods": [
        {
          "name": "LOD0",
          "mesh_type": "procedural",
          "procedural": { "generator": "Cube", "mesh_name": "CubeMesh" },
          "submeshes": [
            {
              "surface_id": "018f8f8f-1111-7111-8111-111111111111",
              "material_ref": "/Cooked/Materials/default.omat",
              "views": [ { "view_ref": "__all__" } ]
            }
          ]
        }
      ]
    })");

  const auto* const manifest_json = R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "output": ".cooked",
      "defaults": {
        "geometry_descriptor": {
          "cooked_context_roots": ["Libraries/Materials"],
          "name": "default-geometry-name"
        }
      },
      "jobs": [
        {
          "type": "geometry-descriptor",
          "source": "Geometry/cube.geometry.json",
          "virtual_path": "/Cooked/Geometry/Cube.ogeo",
          "name": "cube-job"
        }
      ]
    })";
  WriteText(manifest_path, manifest_json);

  auto errors = std::ostringstream {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest)
    << "Expected manifest to contain a value" << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request)
    << "Expected request to contain a value" << request_errors.str();
  ASSERT_HAS_VALUE(request->cooked_root)
    << "Expected request->cooked_root to contain a value";
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(request->job_name, std::optional<std::string> { "cube-job" });
  EXPECT_EQ(request->asset_virtual_path, "/Cooked/Geometry/Cube.ogeo");
  ASSERT_HAS_VALUE(request->geometry_descriptor)
    << "Expected request->geometry_descriptor to contain a value";
  ASSERT_EQ(request->cooked_context_roots.size(), 1U);
  EXPECT_EQ(request->cooked_context_roots.at(0), root / "Libraries/Materials");

  const auto normalized
    = json::parse(request->geometry_descriptor->descriptor_json);
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
      "lods": [
        {
          "name": "LOD0",
          "mesh_type": "procedural",
          "procedural": { "generator": "Cube", "mesh_name": "CubeMesh" },
          "submeshes": [
            {
              "surface_id": "018f8f8f-1111-7111-8111-111111111111",
              "material_ref": "/Cooked/Materials/default.omat",
              "views": [ { "view_ref": "__all__" } ]
            }
          ]
        }
      ]
    })");

  WriteText(manifest_path,
    R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "output": "geometry-cooked",
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
          "virtual_path": "/Cooked/Geometry/Cube.ogeo",
          "depends_on": ["shared.buffers"]
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest)
    << "Expected manifest to contain a value" << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 2U);
  EXPECT_EQ(manifest->jobs.at(1).id, "proc.cube");
  ASSERT_EQ(manifest->jobs.at(1).depends_on.size(), 1U);
  EXPECT_EQ(manifest->jobs.at(1).depends_on.at(0), "shared.buffers");

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(1).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request)
    << "Expected request to contain a value" << request_errors.str();
  ASSERT_HAS_VALUE(request->orchestration)
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
      "mounts": [{ "name": "Content", "path": "." }],
      "jobs": [
        {
          "type": "geometry-descriptor",
          "source": "Geometry/cube.geometry.json",
          "intent": "albedo"
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("manifest schema validation failed"));
}

} // namespace
