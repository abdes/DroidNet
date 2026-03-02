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
    / "oxygen_manifest_scene_descriptor";
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

NOLINT_TEST(ImportManifestSceneDescriptorTest,
  BuildsSceneDescriptorRequestWithDefaultsAndDescriptorOverrides)
{
  const auto manifest_path = MakeManifestPath("builds_scene_descriptor");
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Scenes" / "demo.scene.json";
  WriteTextFile(descriptor_path,
    R"({
      "name": "DemoScene",
      "content_hashing": false,
      "nodes": [
        { "name": "Root" },
        { "name": "MeshNode", "parent": 0 }
      ],
      "renderables": [
        { "node": 1, "geometry_ref": "/.cooked/Geometry/cube.ogeo" }
      ]
    })");

  const auto cooked_root = (root / ".cooked").generic_string();
  const auto manifest_json = std::string { R"({
      "version": 1,
      "output": ")" }
    + cooked_root + R"(",
      "defaults": {
        "scene_descriptor": {
          "content_hashing": true,
          "name": "default-scene-name"
        }
      },
      "jobs": [
        {
          "type": "scene-descriptor",
          "source": "Scenes/demo.scene.json",
          "name": "demo-scene-job",
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
  EXPECT_EQ(request->job_name, std::optional<std::string> { "demo-scene-job" });
  ASSERT_TRUE(request->scene_descriptor.has_value());

  EXPECT_FALSE(
    EffectiveContentHashingEnabled(request->options.with_content_hashing));

  const auto normalized
    = json::parse(request->scene_descriptor->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "DemoScene");
}

NOLINT_TEST(ImportManifestSceneDescriptorTest,
  CollectsAndPropagatesSceneDescriptorDependencies)
{
  const auto manifest_path = MakeManifestPath("collects_scene_dependencies");
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Scenes" / "demo.scene.json";
  WriteTextFile(descriptor_path,
    R"({
      "name": "DemoScene",
      "nodes": [ { "name": "Root" } ]
    })");

  WriteTextFile(manifest_path,
    R"({
      "version": 1,
      "output": "C:/tmp/scene-cooked",
      "jobs": [
        {
          "id": "geo.cube",
          "type": "geometry-descriptor",
          "source": "Geometry/cube.geometry.json"
        },
        {
          "id": "scene.demo",
          "type": "scene-descriptor",
          "source": "Scenes/demo.scene.json",
          "depends_on": ["geo.cube"]
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 2U);
  EXPECT_EQ(manifest->jobs[1].id, "scene.demo");
  ASSERT_EQ(manifest->jobs[1].depends_on.size(), 1U);
  EXPECT_EQ(manifest->jobs[1].depends_on[0], "geo.cube");

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs[1].BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  ASSERT_TRUE(request->orchestration.has_value());
  EXPECT_EQ(request->orchestration->job_id, "scene.demo");
  ASSERT_EQ(request->orchestration->depends_on.size(), 1U);
  EXPECT_EQ(request->orchestration->depends_on[0], "geo.cube");
}

NOLINT_TEST(ImportManifestSceneDescriptorTest,
  RejectsSceneDescriptorJobWithDisallowedKeys)
{
  const auto manifest_path = MakeManifestPath("rejects_disallowed_key");
  WriteTextFile(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "type": "scene-descriptor",
          "source": "Scenes/demo.scene.json",
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
