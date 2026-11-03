//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.geometry-descriptor.schema.json,
// Import/Schemas/oxygen.lod-policy.schema.json

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::GeometryDescriptor;
using oxygen::cooker::test::LoadSchema;
using oxygen::cooker::test::SchemaCase;
using oxygen::cooker::test::ValidateJson;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;

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

constexpr auto kDefaultMaterial
  = std::string_view { "/Cooked/Materials/default.omat" };

//! A one-LOD procedural capsule named `name`, with one submesh of the default
//! material.
auto Procedural(const std::string_view name) -> GeometryDescriptor
{
  auto geometry = GeometryDescriptor {
    "/Content/Geometry/" + std::string { name } + ".ogeo",
  };
  geometry.SubmeshMaterial(0, 0, kDefaultMaterial);
  return geometry;
}

auto CapsuleDescriptor(const json& params) -> json
{
  auto geometry = Procedural("Capsule");
  if (!params.is_null()) {
    geometry.Set("/lods/0/procedural/params", params);
  }
  return geometry.Json();
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
      Procedural("ProcCube")
        .Set("/$schema",
          "./src/Oxygen/Cooker/Import/Schemas/"
          "oxygen.geometry-descriptor.schema.json")
        .Set("/lods/0/procedural/generator", "Cube")
        .Set("/lods/0/procedural/mesh_name", "CubeMesh")
        .Text(),
      true,
      "",
    },
    SchemaCase {
      "AcceptsSubdividedCubeProcedural",
      Procedural("ProcJellyCube")
        .Set("/lods/0/procedural/generator", "SubdividedCube")
        .Set("/lods/0/procedural/mesh_name", "JellyCube")
        .Set("/lods/0/procedural/params/segments", 8)
        .Text(),
      true,
      "",
    },
    SchemaCase {
      "RejectsUnknownNestedFields",
      Procedural("ProcCube")
        .Set("/lods/0/procedural/generator", "Cube")
        .Set("/lods/0/procedural/mesh_name", "CubeMesh")
        .Set("/lods/0/procedural/unexpected", true)
        .Text(),
      false,
      "'unexpected'",
    }),
  oxygen::cooker::test::SchemaCaseName);

NOLINT_TEST(GeometryDescriptorSchemaTest, ValidatesIcoSphereParameters)
{
  const auto doc = Procedural("ProcIcoSphere")
                     .Set("/lods/0/procedural/generator", "IcoSphere")
                     .Set("/lods/0/procedural/mesh_name", "BallGeo")
                     .Set("/lods/0/procedural/params/subdivision_level", 2)
                     .Json();

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

//! A procedural mesh's bounds come from its generator, never the descriptor.
NOLINT_TEST(GeometryDescriptorSchemaTest, RejectsBoundsOnProceduralMeshes)
{
  const auto bounds
    = json::parse(R"({ "min": [-1, -1, -1], "max": [1, 1, 1] })");
  const auto base = CapsuleDescriptor(nullptr);
  ASSERT_THAT(ValidateJson(Schema(), base), IsEmpty());

  auto on_lod = base;
  on_lod.at("lods").at(0)["bounds"] = bounds;
  EXPECT_THAT(ValidateJson(Schema(), on_lod), Not(IsEmpty()));

  auto on_submesh = base;
  on_submesh.at("lods").at(0).at("submeshes").at(0)["bounds"] = bounds;
  EXPECT_THAT(ValidateJson(Schema(), on_submesh), Not(IsEmpty()));

  auto on_view = base;
  on_view.at("lods").at(0).at("submeshes").at(0).at("views").at(0)["bounds"]
    = bounds;
  EXPECT_THAT(ValidateJson(Schema(), on_view), Not(IsEmpty()));

  // The geometry's own bounds stay optional authoring for every mesh type.
  auto on_geometry = base;
  on_geometry["bounds"] = bounds;
  EXPECT_THAT(ValidateJson(Schema(), on_geometry), IsEmpty());
}

//! A buffer mesh has no generator, so its descriptor bounds it.
NOLINT_TEST(GeometryDescriptorSchemaTest, RequiresBoundsOnBufferMeshes)
{
  auto doc
    = Procedural("Capsule")
        .Remove("/lods/0/procedural")
        .Set("/lods/0/mesh_type", "standard")
        .Reference("/lods/0/buffers/vb_ref", "/Cooked/Buffers/vertices.obuf")
        .Reference("/lods/0/buffers/ib_ref", "/Cooked/Buffers/indices.obuf")
        .Set("/lods/0/submeshes/0/views/0/view_ref", "surface")
        .Json();
  auto& lod = doc.at("lods").at(0);
  EXPECT_THAT(ValidateJson(Schema(), doc), Not(IsEmpty()));

  lod["bounds"] = json::parse(R"({ "min": [-1, -1, -1], "max": [1, 1, 1] })");
  EXPECT_THAT(ValidateJson(Schema(), doc), IsEmpty());
}

//! A standard mesh whose references are written as the test says.
auto BufferMesh() -> GeometryDescriptor
{
  auto geometry = Procedural("Capsule");
  geometry.Remove("/lods/0/procedural")
    .Set("/lods/0/mesh_type", "standard")
    .Set("/lods/0/bounds",
      json::parse(R"({ "min": [-1, -1, -1], "max": [1, 1, 1] })"))
    .Set("/lods/0/buffers/vb_ref", "Vertices.obuf")
    .Set("/lods/0/buffers/ib_ref", "../Buffers/Indices.obuf")
    .Set("/lods/0/submeshes/0/material_ref", "../Materials/Red.omat")
    .Set("/lods/0/submeshes/0/views/0/view_ref", "surface")
    .AppendBuffer(
      { { "uri", "raw/vertices.bin" } }, "/Content/Geometry/Vertices.obuf");
  return geometry;
}

//! References are absolute in another mount or relative to the descriptor.
NOLINT_TEST(GeometryDescriptorSchemaTest, AcceptsRelativeReferences)
{
  auto doc = BufferMesh();
  EXPECT_THAT(ValidateJson(Schema(), doc.Json()), IsEmpty());
  EXPECT_EQ(doc.Json().at("buffers").at(0).at("virtual_path"), "Vertices.obuf");

  doc.Set("/lods/0/submeshes/0/material_ref", "/Engine/Materials/Default.omat");
  EXPECT_THAT(ValidateJson(Schema(), doc.Json()), IsEmpty());
}

NOLINT_TEST(GeometryDescriptorSchemaTest, RefusesAReferenceOfAnotherType)
{
  struct Case {
    std::string_view pointer;
    std::string_view text;
  };
  const auto cases = std::vector<Case> {
    { .pointer = "/lods/0/buffers/vb_ref", .text = "Vertices.otex" },
    { .pointer = "/lods/0/buffers/ib_ref", .text = "/Content/Indices.omat" },
    { .pointer = "/lods/0/submeshes/0/material_ref", .text = "Red.obuf" },
    { .pointer = "/buffers/0/virtual_path", .text = "Vertices.omat" },
  };
  for (const auto& [pointer, text] : cases) {
    SCOPED_TRACE(std::string(pointer));
    auto doc = BufferMesh();
    doc.Set(pointer, std::string(text));
    EXPECT_THAT(ValidateJson(Schema(), doc.Json()), Not(IsEmpty()));
  }
}

NOLINT_TEST(GeometryDescriptorSchemaTest, RefusesMalformedReferences)
{
  const auto cases = std::vector<std::string_view> {
    "",
    "Dir//Red.omat",
    "Dir/",
    "../../../",
    "A\\B.omat",
    "asset:///Red.omat",
    "./../Red.omat",
  };
  for (const auto text : cases) {
    SCOPED_TRACE(std::string(text));
    auto doc = BufferMesh();
    doc.Set("/lods/0/submeshes/0/material_ref", std::string(text));
    EXPECT_THAT(ValidateJson(Schema(), doc.Json()), Not(IsEmpty()));
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
    {
      .generator = "Sphere",
      .key = "latitude_segments",
      .parameters = { { "latitude_segments", kTooManySegments } },
    },
    {
      .generator = "Plane",
      .key = "x_segments",
      .parameters = { { "x_segments", kTooManySegments } },
    },
    {
      .generator = "Cylinder",
      .key = "segments",
      .parameters = { { "segments", kTooManySegments } },
    },
    {
      .generator = "Cone",
      .key = "segments",
      .parameters = { { "segments", kTooManySegments } },
    },
    {
      .generator = "Torus",
      .key = "major_segments",
      .parameters = { { "major_segments", kTooManySegments } },
    },
    {
      .generator = "Plane",
      .key = "size",
      .parameters = { { "size", kTooLargeDimension } },
    },
    {
      .generator = "Cylinder",
      .key = "radius",
      .parameters = { { "radius", kTooLargeDimension } },
    },
    {
      .generator = "Cone",
      .key = "height",
      .parameters = { { "height", kTooLargeDimension } },
    },
    {
      .generator = "Torus",
      .key = "minor_radius",
      .parameters = { { "minor_radius", kTooLargeDimension } },
    },
    {
      .generator = "Quad",
      .key = "width",
      .parameters = { { "width", kTooLargeDimension } },
    },
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
    {
      .error_substr = ParamError("hemisphere_segments"),
      .parameters = { { "hemisphere_segments", 0 } },
    },
    {
      .error_substr = ParamError("hemisphere_segments"),
      .parameters = { { "hemisphere_segments", 65 } },
    },
    {
      .error_substr = ParamError("hemisphere_segments"),
      .parameters = { { "hemisphere_segments", 1.5 } },
    },
    {
      .error_substr = ParamError("radial_segments"),
      .parameters = { { "radial_segments", 2 } },
    },
    {
      .error_substr = ParamError("radial_segments"),
      .parameters = { { "radial_segments", 257 } },
    },
    {
      .error_substr = ParamError("radial_segments"),
      .parameters = { { "radial_segments", "32" } },
    },
    {
      .error_substr = ParamError("height"),
      .parameters = { { "height", 0.0 } },
    },
    {
      .error_substr = ParamError("height"),
      .parameters = { { "height", 1.0e39 } },
    },
    {
      .error_substr = ParamError("radius"),
      .parameters = { { "radius", -0.5 } },
    },
    {
      .error_substr = ParamError("radius"),
      .parameters = { { "radius", 1.0e39 } },
    },
    // Not a capsule parameter, so it is reported as an extra property.
    { .error_substr = "'segments'", .parameters = { { "segments", 16 } } },
  };
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.parameters.dump());
    EXPECT_THAT(ValidateJson(Schema(), CapsuleDescriptor(test_case.parameters)),
      Contains(HasSubstr(test_case.error_substr)));
  }
}

//! Surface defaults and the default LOD policy, from the shared LOD policy
//! schema.
NOLINT_TEST(GeometryDescriptorSchemaTest, AcceptsAssetDefaults)
{
  const auto policies = std::vector<json> {
    json::parse(R"({ "type": "fixed", "lod_index": 2 })"),
    json::parse(
      R"({ "type": "distance", "thresholds": [0.5, 1], "hysteresis_ratio": 0.1 })"),
    json::parse(R"({ "type": "screen_space_error",
      "enter_finer_sse": [2], "exit_coarser_sse": [1] })"),
  };
  for (const auto& policy : policies) {
    SCOPED_TRACE(policy.dump());
    auto doc = CapsuleDescriptor(json {});
    doc["surfaces"] = json::parse(R"([{
      "surface_id": "018f8f8f-1111-7111-8111-111111111111",
      "visible": false, "casts_shadows": true }])");
    doc["lod_policy"] = policy;
    EXPECT_THAT(ValidateJson(Schema(), doc), IsEmpty());
  }
}

NOLINT_TEST(GeometryDescriptorSchemaTest, RejectsInvalidAssetDefaults)
{
  struct Case {
    std::string_view member;
    std::string_view value;
  };
  const auto cases = std::vector<Case> {
    { .member = "surfaces", .value = R"([{ "visible": false }])" },
    {
      .member = "surfaces",
      .value = R"([{ "surface_id": "x", "visible": false }])",
    },
    {
      .member = "surfaces",
      .value = R"([{ "surface_id": "018f8f8f-1111-7111-8111-111111111111",
        "material": "/Content/Red.omat" }])",
    },
    {
      .member = "lod_policy",
      .value = R"({ "type": "fixed", "lod_index": 8 })",
    },
    { .member = "lod_policy", .value = R"({ "type": "fixed" })" },
    {
      .member = "lod_policy",
      .value
      = R"({ "type": "distance", "thresholds": [1], "hysteresis_ratio": 1 })",
    },
    {
      .member = "lod_policy",
      .value = R"({ "type": "distance", "thresholds": [1, 2, 3, 4, 5, 6, 7, 8],
        "hysteresis_ratio": 0.1 })",
    },
    {
      .member = "lod_policy",
      .value = R"({ "type": "distance", "thresholds": [-1],
        "hysteresis_ratio": 0.1 })",
    },
    {
      .member = "lod_policy",
      .value = R"({ "type": "screen_space_error",
        "enter_finer_sse": [1] })",
    },
    { .member = "lod_policy", .value = R"({ "type": "nearest" })" },
  };
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.value);
    auto doc = CapsuleDescriptor(json {});
    doc[std::string(test_case.member)] = json::parse(test_case.value);
    EXPECT_THAT(ValidateJson(Schema(), doc), Not(IsEmpty()));
  }
}

} // namespace
