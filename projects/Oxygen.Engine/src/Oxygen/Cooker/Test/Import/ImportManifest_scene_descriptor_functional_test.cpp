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
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/SceneDescriptorTestData.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::EffectiveContentHashingEnabled;
using oxygen::content::import::ImportManifest;
using oxygen::content::import::test::MakeCurrentSceneDescriptor;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::WriteText;

NOLINT_TEST(ImportManifestSceneDescriptorTest,
  ResolvesOrderedContextRootsAndOverridesDefaults)
{
  const ScopedTempDir temp;
  const auto path = temp.Path() / "context_roots" / "import_manifest.json";
  const auto root = path.parent_path();
  WriteText(root / "scene.json",
    MakeCurrentSceneDescriptor(R"({"name":"Scene","nodes":[{"name":"Root"}]})")
      .dump());
  WriteText(path, R"({
    "version":1,"output":"out",
    "defaults":{"scene_descriptor":{"cooked_context_roots":["Libraries/Low"]}},
    "jobs":[
      {"type":"scene-descriptor","source":"scene.json"},
      {"type":"scene-descriptor","source":"scene.json",
       "cooked_context_roots":["Libraries/High","out"]}
    ]})");
  auto errors = std::ostringstream {};
  const auto manifest = ImportManifest::Load(path, std::nullopt, errors);
  ASSERT_HAS_VALUE(manifest)
    << "Expected manifest to contain a value" << errors.str();
  const auto inherited = manifest->jobs.at(0).BuildRequest(errors);
  const auto overridden = manifest->jobs.at(1).BuildRequest(errors);
  ASSERT_HAS_VALUE(inherited)
    << "Expected inherited to contain a value" << errors.str();
  ASSERT_HAS_VALUE(overridden)
    << "Expected overridden to contain a value" << errors.str();
  ASSERT_EQ(inherited->cooked_context_roots.size(), 1U);
  EXPECT_EQ(inherited->cooked_context_roots.at(0), root / "Libraries/Low");
  ASSERT_EQ(overridden->cooked_context_roots.size(), 2U);
  EXPECT_EQ(overridden->cooked_context_roots.at(0), root / "Libraries/High");
  EXPECT_EQ(overridden->cooked_context_roots.at(1), root / "out");
}

NOLINT_TEST(ImportManifestSceneDescriptorTest, RejectsInvalidContextRootValues)
{
  const ScopedTempDir temp;
  const auto path
    = temp.Path() / "invalid_context_roots" / "import_manifest.json";
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
    WriteText(path, document.dump());
    auto errors = std::ostringstream {};
    EXPECT_FALSE(ImportManifest::Load(path, std::nullopt, errors).has_value());
    EXPECT_FALSE(errors.str().empty());
  }
}

NOLINT_TEST(ImportManifestSceneDescriptorTest,
  BuildsSceneDescriptorRequestWithDefaultsAndDescriptorOverrides)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "builds_scene_descriptor" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Scenes" / "demo.scene.json";
  WriteText(descriptor_path,
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
  WriteText(manifest_path, manifest_json);

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
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
  EXPECT_EQ(request->job_name, std::optional<std::string> { "demo-scene-job" });
  ASSERT_HAS_VALUE(request->scene_descriptor)
    << "Expected request->scene_descriptor to contain a value";

  EXPECT_EQ(request->options.with_content_hashing,
    EffectiveContentHashingEnabled(false));

  const auto normalized
    = json::parse(request->scene_descriptor->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "DemoScene");
}

NOLINT_TEST(ImportManifestSceneDescriptorTest,
  CollectsAndPropagatesSceneDescriptorDependencies)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "collects_scene_dependencies" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Scenes" / "demo.scene.json";
  WriteText(descriptor_path,
    MakeCurrentSceneDescriptor(R"({
      "name": "DemoScene",
      "nodes": [ { "name": "Root" } ]
    })")
      .dump());

  WriteText(manifest_path,
    R"({
      "version": 1,
      "output": ")"
      + (manifest_path.parent_path() / "scene-cooked").generic_string() + R"(",
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
  ASSERT_HAS_VALUE(manifest)
    << "Expected manifest to contain a value" << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 2U);
  EXPECT_EQ(manifest->jobs.at(1).id, "scene.demo");
  ASSERT_EQ(manifest->jobs.at(1).depends_on.size(), 1U);
  EXPECT_EQ(manifest->jobs.at(1).depends_on.at(0), "geo.cube");

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(1).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request)
    << "Expected request to contain a value" << request_errors.str();
  ASSERT_HAS_VALUE(request->orchestration)
    << "Expected request->orchestration to contain a value";
  EXPECT_EQ(request->orchestration->job_id, "scene.demo");
  ASSERT_EQ(request->orchestration->depends_on.size(), 1U);
  EXPECT_EQ(request->orchestration->depends_on.at(0), "geo.cube");
}

NOLINT_TEST(ImportManifestSceneDescriptorTest,
  RejectsSceneDescriptorJobWithDisallowedKeys)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_disallowed_key" / "import_manifest.json";
  WriteText(manifest_path,
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
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("manifest schema validation failed"));
}

} // namespace
