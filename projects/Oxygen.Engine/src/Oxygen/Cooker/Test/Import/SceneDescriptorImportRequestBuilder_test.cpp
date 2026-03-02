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

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/SceneDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/SceneDescriptorImportSettings.h>

namespace {

using nlohmann::json;
using oxygen::content::import::EffectiveContentHashingEnabled;
using oxygen::content::import::SceneDescriptorImportSettings;
using oxygen::content::import::internal::BuildSceneDescriptorRequest;

auto MakeTempDir(const std::string_view stem) -> std::filesystem::path
{
  auto dir = std::filesystem::temp_directory_path() / "oxygen_scene_desc";
  dir /= std::filesystem::path { std::string { stem } };
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  return dir;
}

auto WriteTextFile(
  const std::filesystem::path& path, const std::string_view text) -> void
{
  std::filesystem::create_directories(path.parent_path());
  auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out.is_open());
  out << text;
}

auto MakeBaseSettings(const std::filesystem::path& descriptor_path)
  -> SceneDescriptorImportSettings
{
  auto settings = SceneDescriptorImportSettings {};
  settings.descriptor_path = descriptor_path.string();
  settings.cooked_root = (descriptor_path.parent_path() / ".cooked").string();
  settings.job_name = "manifest-scene";
  settings.with_content_hashing = true;
  return settings;
}

NOLINT_TEST(SceneDescriptorImportRequestBuilderTest,
  BuildsRequestFromValidDescriptorWithNormalizedPayload)
{
  const auto dir = MakeTempDir("valid_request");
  const auto descriptor_path = dir / "Scenes" / "demo.scene.json";
  WriteTextFile(descriptor_path,
    R"({
      "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.scene-descriptor.schema.json",
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

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildSceneDescriptorRequest(settings, errors);

  ASSERT_TRUE(request.has_value()) << errors.str();
  EXPECT_TRUE(errors.str().empty());
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_TRUE(request->cooked_root->is_absolute());
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(request->job_name, std::optional<std::string> { "manifest-scene" });
  EXPECT_FALSE(
    EffectiveContentHashingEnabled(request->options.with_content_hashing));
  ASSERT_TRUE(request->scene_descriptor.has_value());

  const auto normalized
    = json::parse(request->scene_descriptor->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "DemoScene");
}

NOLINT_TEST(SceneDescriptorImportRequestBuilderTest,
  RejectsDescriptorWithSchemaViolations)
{
  const auto dir = MakeTempDir("schema_violation");
  const auto descriptor_path = dir / "Scenes" / "bad.scene.json";
  WriteTextFile(descriptor_path,
    R"({
      "name": "BadScene",
      "nodes": [
        { "name": "Root", "unexpected": true }
      ]
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildSceneDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("scene.descriptor.schema_validation_failed")
    != std::string::npos);
}

NOLINT_TEST(SceneDescriptorImportRequestBuilderTest, RejectsMissingFile)
{
  const auto dir = MakeTempDir("missing_file");
  const auto descriptor_path = dir / "Scenes" / "missing.scene.json";
  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildSceneDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(
    errors.str().find("failed to open scene descriptor") != std::string::npos);
}

NOLINT_TEST(SceneDescriptorImportRequestBuilderTest, RejectsRelativeCookedRoot)
{
  const auto dir = MakeTempDir("relative_output");
  const auto descriptor_path = dir / "Scenes" / "ok.scene.json";
  WriteTextFile(descriptor_path,
    R"({
      "name": "DemoScene",
      "nodes": [ { "name": "Root" } ]
    })");

  auto settings = MakeBaseSettings(descriptor_path);
  settings.cooked_root = "relative/output";
  auto errors = std::ostringstream {};

  const auto request = BuildSceneDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("cooked root must be an absolute path")
    != std::string::npos);
}

} // namespace
