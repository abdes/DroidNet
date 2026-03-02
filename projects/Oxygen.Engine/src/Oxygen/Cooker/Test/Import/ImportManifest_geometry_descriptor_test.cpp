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
    / "oxygen_manifest_geometry_descriptor";
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

NOLINT_TEST(ImportManifestGeometryDescriptorTest,
  BuildsGeometryDescriptorRequestWithDefaultsAndDescriptorOverrides)
{
  const auto manifest_path = MakeManifestPath("builds_geometry_descriptor");
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Geometry" / "cube.geometry.json";
  WriteTextFile(descriptor_path,
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
  EXPECT_EQ(request->job_name, std::optional<std::string> { "cube-job" });
  ASSERT_TRUE(request->geometry_descriptor.has_value());

  EXPECT_FALSE(
    EffectiveContentHashingEnabled(request->options.with_content_hashing));

  const auto normalized
    = json::parse(request->geometry_descriptor->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "ProcCube");
}

NOLINT_TEST(ImportManifestGeometryDescriptorTest,
  CollectsAndPropagatesGeometryDescriptorDependencies)
{
  const auto manifest_path = MakeManifestPath("collects_geometry_dependencies");
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Geometry" / "cube.geometry.json";
  WriteTextFile(descriptor_path,
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
              "material_ref": "/.cooked/Materials/default.omat",
              "views": [ { "view_ref": "__all__" } ]
            }
          ]
        }
      ]
    })");

  WriteTextFile(manifest_path,
    R"({
      "version": 1,
      "output": "C:/tmp/geometry-cooked",
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
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 2U);
  EXPECT_EQ(manifest->jobs[1].id, "proc.cube");
  ASSERT_EQ(manifest->jobs[1].depends_on.size(), 1U);
  EXPECT_EQ(manifest->jobs[1].depends_on[0], "shared.buffers");

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs[1].BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  ASSERT_TRUE(request->orchestration.has_value());
  EXPECT_EQ(request->orchestration->job_id, "proc.cube");
  ASSERT_EQ(request->orchestration->depends_on.size(), 1U);
  EXPECT_EQ(request->orchestration->depends_on[0], "shared.buffers");
}

NOLINT_TEST(ImportManifestGeometryDescriptorTest,
  RejectsGeometryDescriptorJobWithDisallowedKeys)
{
  const auto manifest_path = MakeManifestPath("rejects_disallowed_key");
  WriteTextFile(manifest_path,
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
  EXPECT_TRUE(errors.str().find("manifest schema validation failed")
    != std::string::npos);
}

} // namespace
