//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using nlohmann::json_schema::error_handler;
using nlohmann::json_schema::json_validator;

class CollectingErrorHandler final : public error_handler {
public:
  void error(const json::json_pointer& ptr, const json& instance,
    const std::string& message) override
  {
    auto out = std::ostringstream {};
    const auto path = ptr.to_string();
    out << (path.empty() ? "<root>" : path) << ": " << message;
    if (!instance.is_discarded()) {
      out << " (value=" << instance.dump() << ")";
    }
    errors_.push_back(out.str());
  }

  [[nodiscard]] auto HasErrors() const noexcept -> bool
  {
    return !errors_.empty();
  }

  [[nodiscard]] auto ToString() const -> std::string
  {
    auto out = std::ostringstream {};
    for (const auto& error : errors_) {
      out << "- " << error << "\n";
    }
    return out.str();
  }

private:
  std::vector<std::string> errors_;
};

auto FindRepoRoot() -> std::filesystem::path
{
  auto path = std::filesystem::path(__FILE__).parent_path();
  while (!path.empty()) {
    if (std::filesystem::exists(path / "src" / "Oxygen" / "Cooker" / "Import"
          / "Schemas" / "oxygen.geometry-descriptor.schema.json")) {
      return path;
    }
    path = path.parent_path();
  }
  return {};
}

auto SchemaFile(const std::filesystem::path& repo_root) -> std::filesystem::path
{
  return repo_root / "src" / "Oxygen" / "Cooker" / "Import" / "Schemas"
    / "oxygen.geometry-descriptor.schema.json";
}

auto LoadJsonFile(const std::filesystem::path& path) -> std::optional<json>
{
  auto in = std::ifstream(path);
  if (!in) {
    return std::nullopt;
  }
  try {
    auto parsed = json {};
    in >> parsed;
    return parsed;
  } catch (...) {
    return std::nullopt;
  }
}

auto ValidateSchema(
  const json& schema, const json& instance, std::string& errors) -> bool
{
  try {
    auto validator = json_validator {};
    validator.set_root_schema(schema);
    auto handler = CollectingErrorHandler {};
    [[maybe_unused]] auto _ = validator.validate(instance, handler);
    if (handler.HasErrors()) {
      errors = handler.ToString();
      return false;
    }
    return true;
  } catch (const std::exception& ex) {
    errors = ex.what();
    return false;
  }
}

NOLINT_TEST(GeometryDescriptorJsonSchemaTest, AcceptsCanonicalDocument)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.geometry-descriptor.schema.json",
    "name": "ProcCube",
    "content_hashing": true,
    "bounds": { "min": [-0.5, -0.5, -0.5], "max": [0.5, 0.5, 0.5] },
    "lods": [
      {
        "name": "LOD0",
        "mesh_type": "procedural",
        "bounds": { "min": [-0.5, -0.5, -0.5], "max": [0.5, 0.5, 0.5] },
        "procedural": {
          "generator": "Cube",
          "mesh_name": "CubeMesh"
        },
        "submeshes": [
          {
            "material_ref": "/.cooked/Materials/default.omat",
            "views": [ { "view_ref": "__all__" } ]
          }
        ]
      }
    ]
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

NOLINT_TEST(GeometryDescriptorJsonSchemaTest, ValidatesIcoSphereParameters)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "name": "ProcIcoSphere",
    "bounds": { "min": [-0.5, -0.5, -0.5], "max": [0.5, 0.5, 0.5] },
    "lods": [
      {
        "name": "LOD0",
        "mesh_type": "procedural",
        "bounds": { "min": [-0.5, -0.5, -0.5], "max": [0.5, 0.5, 0.5] },
        "procedural": {
          "generator": "IcoSphere",
          "mesh_name": "BallGeo",
          "params": {
            "subdivision_level": 2
          }
        },
        "submeshes": [
          {
            "material_ref": "/.cooked/Materials/default.omat",
            "views": [ { "view_ref": "__all__" } ]
          }
        ]
      }
    ]
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
  for (const auto subdivisions : { 0, 8 }) {
    auto candidate = doc;
    candidate["lods"][0]["procedural"]["params"]["subdivision_level"]
      = subdivisions;
    EXPECT_TRUE(ValidateSchema(*schema, candidate, errors)) << errors;
  }
  for (const auto& subdivisions : { json(-1), json(9), json(2.5), json("2") }) {
    auto candidate = doc;
    candidate["lods"][0]["procedural"]["params"]["subdivision_level"]
      = subdivisions;
    EXPECT_FALSE(ValidateSchema(*schema, candidate, errors));
  }
}

NOLINT_TEST(GeometryDescriptorJsonSchemaTest, AcceptsSubdividedCubeProcedural)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "name": "ProcJellyCube",
    "bounds": { "min": [-0.5, -0.5, -0.5], "max": [0.5, 0.5, 0.5] },
    "lods": [
      {
        "name": "LOD0",
        "mesh_type": "procedural",
        "bounds": { "min": [-0.5, -0.5, -0.5], "max": [0.5, 0.5, 0.5] },
        "procedural": {
          "generator": "SubdividedCube",
          "mesh_name": "JellyCube",
          "params": {
            "segments": 8
          }
        },
        "submeshes": [
          {
            "material_ref": "/.cooked/Materials/default.omat",
            "views": [ { "view_ref": "__all__" } ]
          }
        ]
      }
    ]
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

auto CapsuleDescriptor(const json& params) -> json
{
  auto doc = json::parse(R"({
    "name": "Capsule",
    "bounds": { "min": [-0.5, -0.5, -1], "max": [0.5, 0.5, 1] },
    "lods": [{
      "name": "LOD0",
      "mesh_type": "procedural",
      "bounds": { "min": [-0.5, -0.5, -1], "max": [0.5, 0.5, 1] },
      "procedural": { "generator": "Capsule", "mesh_name": "Capsule" },
      "submeshes": [{
        "material_ref": "/.cooked/Materials/default.omat",
        "views": [{ "view_ref": "__all__" }]
      }]
    }]
  })");
  if (!params.is_null()) {
    doc["lods"][0]["procedural"]["params"] = params;
  }
  return doc;
}

NOLINT_TEST(GeometryDescriptorJsonSchemaTest, AcceptsCapsuleParameterBounds)
{
  const auto schema = LoadJsonFile(SchemaFile(FindRepoRoot()));
  ASSERT_TRUE(schema.has_value());
  const auto cases = std::vector<json> {
    nullptr,
    json::object(),
    { { "hemisphere_segments", 1 }, { "radial_segments", 3 }, { "height", 1.0 },
      { "radius", 0.5 } },
    { { "hemisphere_segments", 64 }, { "radial_segments", 256 },
      { "height", 3.0 }, { "radius", 0.75 } },
  };
  for (const auto& params : cases) {
    SCOPED_TRACE(params.dump());
    auto errors = std::string {};
    EXPECT_TRUE(ValidateSchema(*schema, CapsuleDescriptor(params), errors))
      << errors;
  }
}

NOLINT_TEST(
  GeometryDescriptorJsonSchemaTest, RejectsUnrepresentablePrimitiveParameters)
{
  const auto schema = LoadJsonFile(SchemaFile(FindRepoRoot()));
  ASSERT_TRUE(schema.has_value());
  constexpr auto kTooManySegments = uint64_t { 1 } << 32U;
  constexpr auto kTooLargeDimension
    = static_cast<double>(std::numeric_limits<float>::max()) * 2.0;
  struct Case {
    std::string_view generator;
    json parameters;
  };
  const auto cases = std::vector<Case> {
    { "Sphere", { { "latitude_segments", kTooManySegments } } },
    { "Plane", { { "x_segments", kTooManySegments } } },
    { "Cylinder", { { "segments", kTooManySegments } } },
    { "Cone", { { "segments", kTooManySegments } } },
    { "Torus", { { "major_segments", kTooManySegments } } },
    { "Plane", { { "size", kTooLargeDimension } } },
    { "Cylinder", { { "radius", kTooLargeDimension } } },
    { "Cone", { { "height", kTooLargeDimension } } },
    { "Torus", { { "minor_radius", kTooLargeDimension } } },
    { "Quad", { { "width", kTooLargeDimension } } },
  };
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.generator);
    auto doc = CapsuleDescriptor(test_case.parameters);
    doc["lods"][0]["procedural"]["generator"] = test_case.generator;
    auto errors = std::string {};
    EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
  }
}

NOLINT_TEST(GeometryDescriptorJsonSchemaTest, RejectsInvalidCapsuleParameters)
{
  const auto schema = LoadJsonFile(SchemaFile(FindRepoRoot()));
  ASSERT_TRUE(schema.has_value());
  const auto cases = std::vector<json> {
    { { "hemisphere_segments", 0 } },
    { { "hemisphere_segments", 65 } },
    { { "hemisphere_segments", 1.5 } },
    { { "radial_segments", 2 } },
    { { "radial_segments", 257 } },
    { { "radial_segments", "32" } },
    { { "height", 0.0 } },
    { { "height", 1.0e39 } },
    { { "radius", -0.5 } },
    { { "radius", 1.0e39 } },
    { { "segments", 16 } },
  };
  for (const auto& params : cases) {
    SCOPED_TRACE(params.dump());
    auto errors = std::string {};
    EXPECT_FALSE(ValidateSchema(*schema, CapsuleDescriptor(params), errors));
  }
}

NOLINT_TEST(GeometryDescriptorJsonSchemaTest, RejectsUnknownNestedFields)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "name": "ProcCube",
    "bounds": { "min": [-0.5, -0.5, -0.5], "max": [0.5, 0.5, 0.5] },
    "lods": [
      {
        "name": "LOD0",
        "mesh_type": "procedural",
        "bounds": { "min": [-0.5, -0.5, -0.5], "max": [0.5, 0.5, 0.5] },
        "procedural": {
          "generator": "Cube",
          "mesh_name": "CubeMesh",
          "unexpected": true
        },
        "submeshes": [
          {
            "material_ref": "/.cooked/Materials/default.omat",
            "views": [ { "view_ref": "__all__" } ]
          }
        ]
      }
    ]
  })");

  auto errors = std::string {};
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

} // namespace
