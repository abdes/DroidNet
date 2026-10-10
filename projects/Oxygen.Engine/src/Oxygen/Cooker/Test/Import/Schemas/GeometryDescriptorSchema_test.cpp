//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.geometry-descriptor.schema.json

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::LoadSchema;
using oxygen::cooker::test::SchemaCase;
using oxygen::cooker::test::ValidateJson;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

auto Schema() -> const json&
{
  static const auto schema
    = LoadSchema("Import/Schemas/oxygen.geometry-descriptor.schema.json");
  return schema;
}

constexpr auto kParams = std::string_view { "/lods/0/procedural/params/" };

auto ParamError(const std::string_view key) -> std::string
{
  return std::string(kParams) + std::string(key) + ": ";
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
        "slot_id": "018f8f8f-1111-7111-8111-111111111111",
        "material_ref": "/.cooked/Materials/default.omat",
        "views": [{ "view_ref": "__all__" }]
      }]
    }]
  })");
  if (!params.is_null()) {
    doc.at("lods").at(0).at("procedural").update({ { "params", params } });
  }
  return doc;
}

class GeometryDescriptorSchemaCaseTest
  : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(GeometryDescriptorSchemaCaseTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, GeometryDescriptorSchemaCaseTest,
  ::testing::Values(
    SchemaCase {
      "AcceptsCanonicalDocument",
      R"({
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
            "slot_id": "018f8f8f-1111-7111-8111-111111111111",
            "material_ref": "/.cooked/Materials/default.omat",
            "views": [ { "view_ref": "__all__" } ]
          }
        ]
      }
    ]
  })",
      true,
      "",
    },
    SchemaCase {
      "AcceptsSubdividedCubeProcedural",
      R"({
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
            "slot_id": "018f8f8f-1111-7111-8111-111111111111",
            "material_ref": "/.cooked/Materials/default.omat",
            "views": [ { "view_ref": "__all__" } ]
          }
        ]
      }
    ]
  })",
      true,
      "",
    },
    SchemaCase {
      "RejectsUnknownNestedFields",
      R"({
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
            "slot_id": "018f8f8f-1111-7111-8111-111111111111",
            "material_ref": "/.cooked/Materials/default.omat",
            "views": [ { "view_ref": "__all__" } ]
          }
        ]
      }
    ]
  })",
      false,
      "'unexpected'",
    }),
  oxygen::cooker::test::SchemaCaseName);

NOLINT_TEST(GeometryDescriptorSchemaTest, ValidatesIcoSphereParameters)
{
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
            "slot_id": "018f8f8f-1111-7111-8111-111111111111",
            "material_ref": "/.cooked/Materials/default.omat",
            "views": [ { "view_ref": "__all__" } ]
          }
        ]
      }
    ]
  })");

  EXPECT_THAT(ValidateJson(Schema(), doc), IsEmpty());
  for (const auto subdivisions : { 0, 8 }) {
    auto candidate = doc;
    candidate.at("lods")
      .at(0)
      .at("procedural")
      .at("params")
      .update({ { "subdivision_level", subdivisions } });
    EXPECT_THAT(ValidateJson(Schema(), candidate), IsEmpty());
  }
  for (const auto& subdivisions : { json(-1), json(9), json(2.5), json("2") }) {
    auto candidate = doc;
    candidate.at("lods")
      .at(0)
      .at("procedural")
      .at("params")
      .update({ { "subdivision_level", subdivisions } });
    EXPECT_THAT(ValidateJson(Schema(), candidate),
      Contains(HasSubstr(ParamError("subdivision_level"))))
      << subdivisions.dump();
  }
}

NOLINT_TEST(GeometryDescriptorSchemaTest, AcceptsCapsuleParameterBounds)
{
  const auto cases = std::vector<json> {
    nullptr,
    json::object(),
    {
      { "hemisphere_segments", 1 },
      { "radial_segments", 3 },
      { "height", 1.0 },
      { "radius", 0.5 },
    },
    {
      { "hemisphere_segments", 64 },
      { "radial_segments", 256 },
      { "height", 3.0 },
      { "radius", 0.75 },
    },
  };
  for (const auto& params : cases) {
    SCOPED_TRACE(params.dump());
    EXPECT_THAT(ValidateJson(Schema(), CapsuleDescriptor(params)), IsEmpty());
  }
}

NOLINT_TEST(
  GeometryDescriptorSchemaTest, RejectsUnrepresentablePrimitiveParameters)
{
  constexpr auto kTooManySegments = uint64_t { 1 } << 32U;
  constexpr auto kTooLargeDimension
    = static_cast<double>(std::numeric_limits<float>::max()) * 2.0;
  struct Case {
    std::string_view generator;
    std::string_view key;
    json parameters;
  };
  const auto cases = std::vector<Case> {
    { "Sphere", "latitude_segments",
      { { "latitude_segments", kTooManySegments } } },
    { "Plane", "x_segments", { { "x_segments", kTooManySegments } } },
    { "Cylinder", "segments", { { "segments", kTooManySegments } } },
    { "Cone", "segments", { { "segments", kTooManySegments } } },
    { "Torus", "major_segments", { { "major_segments", kTooManySegments } } },
    { "Plane", "size", { { "size", kTooLargeDimension } } },
    { "Cylinder", "radius", { { "radius", kTooLargeDimension } } },
    { "Cone", "height", { { "height", kTooLargeDimension } } },
    { "Torus", "minor_radius", { { "minor_radius", kTooLargeDimension } } },
    { "Quad", "width", { { "width", kTooLargeDimension } } },
  };
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.generator);
    auto doc = CapsuleDescriptor(test_case.parameters);
    doc.at("lods")
      .at(0)
      .at("procedural")
      .update({ { "generator", test_case.generator } });
    EXPECT_THAT(ValidateJson(Schema(), doc),
      Contains(HasSubstr(ParamError(test_case.key))));
  }
}

NOLINT_TEST(GeometryDescriptorSchemaTest, RejectsInvalidCapsuleParameters)
{
  struct Case {
    std::string error_substr;
    json parameters;
  };
  const auto cases = std::vector<Case> {
    { ParamError("hemisphere_segments"), { { "hemisphere_segments", 0 } } },
    { ParamError("hemisphere_segments"), { { "hemisphere_segments", 65 } } },
    { ParamError("hemisphere_segments"), { { "hemisphere_segments", 1.5 } } },
    { ParamError("radial_segments"), { { "radial_segments", 2 } } },
    { ParamError("radial_segments"), { { "radial_segments", 257 } } },
    { ParamError("radial_segments"), { { "radial_segments", "32" } } },
    { ParamError("height"), { { "height", 0.0 } } },
    { ParamError("height"), { { "height", 1.0e39 } } },
    { ParamError("radius"), { { "radius", -0.5 } } },
    { ParamError("radius"), { { "radius", 1.0e39 } } },
    // Not a capsule parameter, so it is reported as an extra property.
    { "'segments'", { { "segments", 16 } } },
  };
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.parameters.dump());
    EXPECT_THAT(ValidateJson(Schema(), CapsuleDescriptor(test_case.parameters)),
      Contains(HasSubstr(test_case.error_substr)));
  }
}

} // namespace
