//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/SceneDescriptorImportRequestBuilder.cpp

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Import/SceneDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/SceneDescriptorImportSettings.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/SceneDescriptorTestData.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::EffectiveContentHashingEnabled;
using oxygen::content::import::SceneDescriptorImportSettings;
using oxygen::content::import::internal::BuildSceneDescriptorRequest;
using oxygen::content::import::test::MakeCurrentSceneDescriptor;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::WriteText;

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
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "valid_request";
  const auto descriptor_path = dir / "Scenes" / "demo.scene.json";
  WriteText(descriptor_path,
    MakeCurrentSceneDescriptor(R"({
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
    })")
      .dump());

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildSceneDescriptorRequest(settings, errors);

  ASSERT_TRUE(request.has_value())
    << "Expected request to contain a value" << errors.str();
  EXPECT_TRUE(errors.str().empty());
  ASSERT_TRUE(request->cooked_root.has_value())
    << "Expected request->cooked_root to contain a value";
  EXPECT_TRUE(request->cooked_root->is_absolute());
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(request->job_name, std::optional<std::string> { "manifest-scene" });
  EXPECT_EQ(request->options.with_content_hashing,
    EffectiveContentHashingEnabled(false));
  ASSERT_TRUE(request->scene_descriptor.has_value())
    << "Expected request->scene_descriptor to contain a value";

  const auto normalized
    = json::parse(request->scene_descriptor->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "DemoScene");
}

NOLINT_TEST(SceneDescriptorImportRequestBuilderTest,
  ContextRootsRequireAbsolutePathsAndKeepLastPriorityPosition)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "context_root_order";
  const auto descriptor = dir / "scene.json";
  WriteText(descriptor,
    MakeCurrentSceneDescriptor(R"({"name":"Scene","nodes":[{"name":"Root"}]})")
      .dump());
  auto settings = MakeBaseSettings(descriptor);
  settings.cooked_context_roots = { "relative/root" };
  auto errors = std::ostringstream {};
  EXPECT_FALSE(BuildSceneDescriptorRequest(settings, errors).has_value());
  const auto library = dir / "library";
  settings.cooked_context_roots = { library.string(), settings.cooked_root };
  const auto request = BuildSceneDescriptorRequest(settings, errors);
  ASSERT_TRUE(request.has_value())
    << "Expected request to contain a value" << errors.str();
  const auto roots
    = oxygen::content::import::internal::BuildUniqueMountedCookedRoots(
      *request);
  ASSERT_EQ(roots.size(), 2U);
  EXPECT_EQ(roots.at(0), library);
  EXPECT_EQ(roots.at(1), std::filesystem::path(settings.cooked_root));
}

NOLINT_TEST(SceneDescriptorImportRequestBuilderTest,
  RejectsDescriptorWithSchemaViolations)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "schema_violation";
  const auto descriptor_path = dir / "Scenes" / "bad.scene.json";
  WriteText(descriptor_path,
    MakeCurrentSceneDescriptor(R"({
      "name": "BadScene",
      "nodes": [
        { "name": "Root", "unexpected": true }
      ]
    })")
      .dump());

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildSceneDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors.str(),
    ::testing::HasSubstr("scene.descriptor.schema_validation_failed"));
}

NOLINT_TEST(SceneDescriptorImportRequestBuilderTest, RejectsMissingFile)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "missing_file";
  const auto descriptor_path = dir / "Scenes" / "missing.scene.json";
  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildSceneDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("failed to open scene descriptor"));
}

NOLINT_TEST(SceneDescriptorImportRequestBuilderTest, RejectsRelativeCookedRoot)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "relative_output";
  const auto descriptor_path = dir / "Scenes" / "ok.scene.json";
  WriteText(descriptor_path,
    MakeCurrentSceneDescriptor(R"({
      "name": "DemoScene",
      "nodes": [ { "name": "Root" } ]
    })")
      .dump());

  auto settings = MakeBaseSettings(descriptor_path);
  settings.cooked_root = "relative/output";
  auto errors = std::ostringstream {};

  const auto request = BuildSceneDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("cooked root must be an absolute path"));
}

NOLINT_TEST(SceneDescriptorImportRequestBuilderTest,
  RejectsLegacyDescriptorVersionWithRecookMessage)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "legacy_version";
  const auto descriptor_path = dir / "Scenes" / "legacy.scene.json";
  WriteText(descriptor_path,
    R"({
      "version": 2,
      "name": "LegacyScene",
      "nodes": [ { "name": "Root" } ]
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildSceneDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("scene.descriptor.recook_required"))
    << errors.str();
}

} // namespace
