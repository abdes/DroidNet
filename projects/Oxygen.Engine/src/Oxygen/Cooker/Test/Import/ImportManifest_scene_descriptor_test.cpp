//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "SceneDescriptorTestData.h"
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::EffectiveContentHashingEnabled;
using oxygen::content::import::ImportManifest;
using oxygen::content::import::test::MakeCurrentSceneDescriptor;

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
  ResolvesOrderedContextRootsAndOverridesDefaults)
{
  const auto path = MakeManifestPath("context_roots");
  const auto root = path.parent_path();
  WriteTextFile(root / "scene.json",
    MakeCurrentSceneDescriptor(R"({"name":"Scene","nodes":[{"name":"Root"}]})")
      .dump());
  WriteTextFile(path, R"({
    "version":1,"output":"out",
    "defaults":{"scene_descriptor":{"cooked_context_roots":["Libraries/Low"]}},
    "jobs":[
      {"type":"scene-descriptor","source":"scene.json"},
      {"type":"scene-descriptor","source":"scene.json",
       "cooked_context_roots":["Libraries/High","out"]}
    ]})");
  auto errors = std::ostringstream {};
  const auto manifest = ImportManifest::Load(path, std::nullopt, errors);
  if (!manifest.has_value()) {
    FAIL() << "Expected manifest to contain a value" << errors.str();
  }
  const auto inherited = manifest->jobs.at(0).BuildRequest(errors);
  const auto overridden = manifest->jobs.at(1).BuildRequest(errors);
  if (!inherited.has_value()) {
    FAIL() << "Expected inherited to contain a value" << errors.str();
  }
  if (!overridden.has_value()) {
    FAIL() << "Expected overridden to contain a value" << errors.str();
  }
  ASSERT_EQ(inherited->cooked_context_roots.size(), 1U);
  EXPECT_EQ(inherited->cooked_context_roots.at(0), root / "Libraries/Low");
  ASSERT_EQ(overridden->cooked_context_roots.size(), 2U);
  EXPECT_EQ(overridden->cooked_context_roots.at(0), root / "Libraries/High");
  EXPECT_EQ(overridden->cooked_context_roots.at(1), root / "out");
}

NOLINT_TEST(ImportManifestSceneDescriptorTest, RejectsInvalidContextRootValues)
{
  const auto path = MakeManifestPath("invalid_context_roots");
  for (const auto& roots : std::vector<json> {
         json("root"),
         json::array({ 42 }),
         json::array({ "" }),
       }) {
    const auto document = json {
      {
        "jobs",
        json::array({
          json {
            { "type", "scene-descriptor" },
            { "source", "scene.json" },
            { "cooked_context_roots", roots },
          },
        }),
      },
    };
    WriteTextFile(path, document.dump());
    auto errors = std::ostringstream {};
    EXPECT_FALSE(ImportManifest::Load(path, std::nullopt, errors).has_value());
    EXPECT_FALSE(errors.str().empty());
  }
}

NOLINT_TEST(ImportManifestSceneDescriptorTest,
  BuildsSceneDescriptorRequestWithDefaultsAndDescriptorOverrides)
{
  const auto manifest_path = MakeManifestPath("builds_scene_descriptor");
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Scenes" / "demo.scene.json";
  WriteTextFile(descriptor_path,
    MakeCurrentSceneDescriptor(R"({
      "name": "DemoScene",
      "content_hashing": false,
      "nodes": [
        { "name": "Root" },
        { "name": "MeshNode", "parent": 0 }
      ],
      "renderables": [
        { "node": 1, "geometry_ref": "/.cooked/Geometry/cube.ogeo" }
      ]
    })")
      .dump());

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
  if (!manifest.has_value()) {
    FAIL() << "Expected manifest to contain a value" << errors.str();
  }
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  if (!request.has_value()) {
    FAIL() << "Expected request to contain a value" << request_errors.str();
  }
  if (!request->cooked_root.has_value()) {
    FAIL() << "Expected request->cooked_root to contain a value";
  }
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(request->job_name, std::optional<std::string> { "demo-scene-job" });
  if (!request->scene_descriptor.has_value()) {
    FAIL() << "Expected request->scene_descriptor to contain a value";
  }

  EXPECT_EQ(request->options.with_content_hashing,
    EffectiveContentHashingEnabled(false));

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
    MakeCurrentSceneDescriptor(R"({
      "name": "DemoScene",
      "nodes": [ { "name": "Root" } ]
    })")
      .dump());

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
  if (!manifest.has_value()) {
    FAIL() << "Expected manifest to contain a value" << errors.str();
  }
  ASSERT_EQ(manifest->jobs.size(), 2U);
  EXPECT_EQ(manifest->jobs.at(1).id, "scene.demo");
  ASSERT_EQ(manifest->jobs.at(1).depends_on.size(), 1U);
  EXPECT_EQ(manifest->jobs.at(1).depends_on.at(0), "geo.cube");

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(1).BuildRequest(request_errors);
  if (!request.has_value()) {
    FAIL() << "Expected request to contain a value" << request_errors.str();
  }
  if (!request->orchestration.has_value()) {
    FAIL() << "Expected request->orchestration to contain a value";
  }
  EXPECT_EQ(request->orchestration->job_id, "scene.demo");
  ASSERT_EQ(request->orchestration->depends_on.size(), 1U);
  EXPECT_EQ(request->orchestration->depends_on.at(0), "geo.cube");
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
