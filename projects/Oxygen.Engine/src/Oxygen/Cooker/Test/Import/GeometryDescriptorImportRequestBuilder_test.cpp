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

#include <Oxygen/Cooker/Import/GeometryDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/GeometryDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>

namespace {

using nlohmann::json;
using oxygen::content::import::EffectiveContentHashingEnabled;
using oxygen::content::import::GeometryDescriptorImportSettings;
using oxygen::content::import::internal::BuildGeometryDescriptorRequest;

auto MakeTempDir(const std::string_view stem) -> std::filesystem::path
{
  auto dir = std::filesystem::temp_directory_path() / "oxygen_geometry_desc";
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
  const auto dir = MakeTempDir("valid_request");
  const auto descriptor_path = dir / "Geometry" / "cube.geometry.json";
  WriteTextFile(descriptor_path,
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

  ASSERT_TRUE(request.has_value()) << errors.str();
  EXPECT_TRUE(errors.str().empty());
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_TRUE(request->cooked_root->is_absolute());
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(
    request->job_name, std::optional<std::string> { "manifest-geometry" });
  EXPECT_FALSE(
    EffectiveContentHashingEnabled(request->options.with_content_hashing));
  ASSERT_TRUE(request->geometry_descriptor.has_value());

  const auto normalized
    = json::parse(request->geometry_descriptor->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "ProcCube");
}

NOLINT_TEST(GeometryDescriptorImportRequestBuilderTest,
  RejectsDescriptorWithSchemaViolations)
{
  const auto dir = MakeTempDir("schema_violation");
  const auto descriptor_path = dir / "Geometry" / "bad.geometry.json";
  WriteTextFile(descriptor_path,
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
  EXPECT_TRUE(errors.str().find("geometry.descriptor.schema_validation_failed")
    != std::string::npos);
}

NOLINT_TEST(GeometryDescriptorImportRequestBuilderTest, RejectsMissingFile)
{
  const auto dir = MakeTempDir("missing_file");
  const auto descriptor_path = dir / "Geometry" / "missing.geometry.json";
  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildGeometryDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("failed to open geometry descriptor")
    != std::string::npos);
}

NOLINT_TEST(
  GeometryDescriptorImportRequestBuilderTest, RejectsRelativeCookedRoot)
{
  const auto dir = MakeTempDir("relative_output");
  const auto descriptor_path = dir / "Geometry" / "ok.geometry.json";
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

  auto settings = MakeBaseSettings(descriptor_path);
  settings.cooked_root = "relative/output";
  auto errors = std::ostringstream {};

  const auto request = BuildGeometryDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("cooked root must be an absolute path")
    != std::string::npos);
}

} // namespace
