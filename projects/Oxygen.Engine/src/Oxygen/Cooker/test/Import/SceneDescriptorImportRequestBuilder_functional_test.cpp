//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/SceneDescriptorImportRequestBuilder.cpp

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequestCodes.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Import/SceneDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/SceneDescriptorImportSettings.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/SceneDescriptorTestData.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {

using oxygen::content::import::ImportDiagnostic;
namespace request_codes = oxygen::content::import::request_codes;
using oxygen::cooker::test::DiagnosticSummary;
using oxygen::cooker::test::HasDiagnostic;

using nlohmann::json;
using oxygen::content::import::SceneDescriptorImportSettings;
using oxygen::content::import::internal::BuildSceneDescriptorRequest;
using oxygen::content::import::test::MakeCurrentSceneDescriptor;
using oxygen::cooker::test::WriteText;
using oxygen::testing::ScopedTempDir;

auto MakeBaseSettings(const std::filesystem::path& descriptor_path)
  -> SceneDescriptorImportSettings
{
  auto settings = SceneDescriptorImportSettings {};
  settings.descriptor_path = descriptor_path.string();
  settings.cooked_root = (descriptor_path.parent_path() / ".cooked").string();
  settings.job_name = "manifest-scene";
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
      "nodes": [
        { "id": "00000000-0000-4000-8000-000000000001", "name": "Root" },
        { "id": "00000000-0000-4000-8000-000000000002", "name": "MeshNode", "parent": 0 }
      ],
      "renderables": [
        { "node": 1, "geometry_ref": "/Cooked/Geometry/cube.ogeo" }
      ]
    })")
      .dump());

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::vector<ImportDiagnostic> {};

  const auto request = BuildSceneDescriptorRequest(settings, errors);

  ASSERT_HAS_VALUE(request)
    << "Expected request to contain a value" << DiagnosticSummary(errors);
  EXPECT_TRUE(errors.empty());
  ASSERT_HAS_VALUE(request->cooked_root)
    << "Expected request->cooked_root to contain a value";
  EXPECT_TRUE(request->cooked_root->is_absolute());
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(request->job_name, std::optional<std::string> { "manifest-scene" });
  ASSERT_HAS_VALUE(request->scene_descriptor)
    << "Expected request->scene_descriptor to contain a value";

  const auto normalized
    = json::parse(request->scene_descriptor->descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "DemoScene");
}

NOLINT_TEST(SceneDescriptorImportRequestBuilderTest,
  ContextRootsRequireAbsolutePathsAndKeepLastPriorityPosition)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "context_root_order";
  const auto descriptor = dir / "scene.json";
  WriteText(descriptor,
    MakeCurrentSceneDescriptor(
      R"({"name":"Scene","nodes":[{"id":"00000000-0000-4000-8000-000000000001","name":"Root"}]})")
      .dump());
  auto settings = MakeBaseSettings(descriptor);
  settings.cooked_context_roots = { "relative/root" };
  auto errors = std::vector<ImportDiagnostic> {};
  EXPECT_FALSE(BuildSceneDescriptorRequest(settings, errors).has_value());
  const auto library = dir / "library";
  settings.cooked_context_roots = { library.string(), settings.cooked_root };
  const auto request = BuildSceneDescriptorRequest(settings, errors);
  ASSERT_HAS_VALUE(request)
    << "Expected request to contain a value" << DiagnosticSummary(errors);
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
        { "id": "00000000-0000-4000-8000-000000000001", "name": "Root", "unexpected": true }
      ]
    })")
      .dump());

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::vector<ImportDiagnostic> {};

  const auto request = BuildSceneDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors,
    HasDiagnostic(oxygen::content::import::import_codes::
        kSceneDescriptorSchemaValidationFailed));
}

NOLINT_TEST(SceneDescriptorImportRequestBuilderTest, RejectsMissingFile)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "missing_file";
  const auto descriptor_path = dir / "Scenes" / "missing.scene.json";
  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::vector<ImportDiagnostic> {};

  const auto request = BuildSceneDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors, HasDiagnostic(request_codes::kDocumentReadFailed));
}

NOLINT_TEST(SceneDescriptorImportRequestBuilderTest, RejectsRelativeCookedRoot)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "relative_output";
  const auto descriptor_path = dir / "Scenes" / "ok.scene.json";
  WriteText(descriptor_path,
    MakeCurrentSceneDescriptor(R"({
      "name": "DemoScene",
      "nodes": [ { "id": "00000000-0000-4000-8000-000000000001", "name": "Root" } ]
    })")
      .dump());

  auto settings = MakeBaseSettings(descriptor_path);
  settings.cooked_root = "relative/output";
  auto errors = std::vector<ImportDiagnostic> {};

  const auto request = BuildSceneDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors, HasDiagnostic(request_codes::kCookedRootNotAbsolute));
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
  auto errors = std::vector<ImportDiagnostic> {};

  const auto request = BuildSceneDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors,
    HasDiagnostic(
      oxygen::content::import::import_codes::kSceneDescriptorRecookRequired))
    << DiagnosticSummary(errors);
}

} // namespace
