//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/GeometryDescriptorImportRequestBuilder.cpp

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/GeometryDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/GeometryDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::EffectiveContentHashingEnabled;
using oxygen::content::import::GeometryDescriptorImportSettings;
using oxygen::content::import::internal::BuildGeometryDescriptorRequest;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::WriteText;

auto MakeBaseSettings(const std::filesystem::path& descriptor_path)
  -> GeometryDescriptorImportSettings
{
  auto settings = GeometryDescriptorImportSettings {};
  settings.descriptor_path = descriptor_path.string();
  settings.cooked_root = (descriptor_path.parent_path() / ".cooked").string();
  settings.job_name = "manifest-geometry";
  settings.with_content_hashing = true;
  return settings;
}

NOLINT_TEST(GeometryDescriptorImportRequestBuilderTest,
  BuildsRequestFromValidDescriptorWithNormalizedPayload)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "valid_request";
  const auto descriptor_path = dir / "Geometry" / "cube.geometry.json";
  WriteText(descriptor_path,
    R"({
      "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.geometry-descriptor.schema.json",
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

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildGeometryDescriptorRequest(settings, errors);

  ASSERT_HAS_VALUE(request)
    << "Expected request to contain a value" << errors.str();
  EXPECT_TRUE(errors.str().empty());
  ASSERT_HAS_VALUE(request->cooked_root)
    << "Expected request->cooked_root to contain a value";
  EXPECT_TRUE(request->cooked_root->is_absolute());
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(
    request->job_name, std::optional<std::string> { "manifest-geometry" });
  EXPECT_EQ(request->options.with_content_hashing,
    EffectiveContentHashingEnabled(false));
  ASSERT_HAS_VALUE(request->geometry_descriptor)
    << "Expected request->geometry_descriptor to contain a value";

  const auto normalized
    = json::parse(request->geometry_descriptor->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "ProcCube");
}

NOLINT_TEST(GeometryDescriptorImportRequestBuilderTest,
  RejectsDescriptorWithSchemaViolations)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "schema_violation";
  const auto descriptor_path = dir / "Geometry" / "bad.geometry.json";
  WriteText(descriptor_path,
    R"({
      "name": "Bad",
      "bounds": { "min": [-1, -1, -1], "max": [1, 1, 1] },
      "lods": [
        {
          "name": "LOD0",
          "mesh_type": "procedural",
          "bounds": { "min": [-1, -1, -1], "max": [1, 1, 1] },
          "procedural": { "generator": "Cube", "mesh_name": "Cube" },
          "submeshes": [
            {
              "slot_id": "018f8f8f-1111-7111-8111-111111111111",
              "material_ref": "/.cooked/Materials/default.omat",
              "views": [ { "view_ref": "__all__", "unexpected": true } ]
            }
          ]
        }
      ]
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildGeometryDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors.str(),
    ::testing::HasSubstr("geometry.descriptor.schema_validation_failed"));
}

NOLINT_TEST(GeometryDescriptorImportRequestBuilderTest, RejectsMissingFile)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "missing_file";
  const auto descriptor_path = dir / "Geometry" / "missing.geometry.json";
  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildGeometryDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("failed to open geometry descriptor"));
}

NOLINT_TEST(
  GeometryDescriptorImportRequestBuilderTest, RejectsRelativeCookedRoot)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "relative_output";
  const auto descriptor_path = dir / "Geometry" / "ok.geometry.json";
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

  auto settings = MakeBaseSettings(descriptor_path);
  settings.cooked_root = "relative/output";
  auto errors = std::ostringstream {};

  const auto request = BuildGeometryDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("cooked root must be an absolute path"));
}

} // namespace
