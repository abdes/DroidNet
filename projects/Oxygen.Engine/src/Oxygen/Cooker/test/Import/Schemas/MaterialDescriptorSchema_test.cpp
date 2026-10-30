//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.material-descriptor.schema.json

#include <string>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::LoadSchema;
using oxygen::cooker::test::MaterialDescriptor;
using oxygen::cooker::test::SchemaCase;
using oxygen::cooker::test::ValidateJson;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

auto Schema() -> const json&
{
  static const auto schema
    = LoadSchema("Import/Schemas/oxygen.material-descriptor.schema.json");
  return schema;
}

auto WoodFloor() -> MaterialDescriptor
{
  return MaterialDescriptor { "/Content/Materials/WoodFloor.omat" };
}

class MaterialDescriptorSchemaCaseTest
  : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(MaterialDescriptorSchemaCaseTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, MaterialDescriptorSchemaCaseTest,
  ::testing::Values(
    SchemaCase {
      "AcceptsCanonicalDocument",
      WoodFloor()
        .Set("/$schema",
          "./src/Oxygen/Cooker/Import/Schemas/"
          "oxygen.material-descriptor.schema.json")
        .Set("/content_hashing", true)
        .Set("/domain", "opaque")
        .Set("/alpha_mode", "opaque")
        .Set("/orm_policy", "auto")
        .Set("/parameters/base_color", json::array({ 1.0, 1.0, 1.0, 1.0 }))
        .Set("/parameters/metalness", 0.1)
        .Set("/parameters/roughness", 0.8)
        .Texture("base_color", "/.cooked/Textures/WoodFloor_Color.otex")
        .Set("/textures/base_color/uv_set", 0)
        .Set(
          "/textures/base_color/uv_transform/scale", json::array({ 1.0, 1.0 }))
        .Set(
          "/textures/base_color/uv_transform/offset", json::array({ 0.0, 0.0 }))
        .Set("/textures/base_color/uv_transform/rotation_radians", 0.0)
        .Texture("roughness", "/.cooked/Textures/WoodFloor_Roughness.otex")
        .Append("/shaders", json::parse(R"({
            "stage": "vertex",
            "source_path": "Vortex/Stages/Translucency/ForwardMesh_VS.hlsl",
            "entry_point": "MainVS"
          })"))
        .Append("/shaders", json::parse(R"({
            "stage": "pixel",
            "source_path": "Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
            "entry_point": "MainPS",
            "defines": "USE_FOG=1"
          })"))
        .Text(),
      true,
      "",
    },
    SchemaCase {
      "RejectsUnknownNestedFields",
      WoodFloor()
        .Texture("base_color", "/.cooked/Textures/WoodFloor_Color.otex")
        .Set("/textures/base_color/unknown_setting", true)
        .Text(),
      false,
      "'unknown_setting'",
    },
    SchemaCase {
      "RequiresTextureVirtualPath",
      WoodFloor().Set("/textures/base_color/uv_set", 0).Text(),
      false,
      "'virtual_path'",
    }),
  oxygen::cooker::test::SchemaCaseName);

NOLINT_TEST(MaterialDescriptorSchemaTest, ValidatesCanonicalEmission)
{
  auto doc
    = MaterialDescriptor { "/Content/Materials/HDR.omat" }
        .Set("/parameters/emissive_color", json::array({ 1.0, 0.25, 0.0 }))
        .Set("/parameters/emissive_intensity", 65504.0)
        .Json();
  EXPECT_THAT(ValidateJson(Schema(), doc), IsEmpty());

  doc.at("parameters").update({ { "emissive_intensity", 65505.0 } });
  EXPECT_THAT(ValidateJson(Schema(), doc),
    Contains(HasSubstr("/parameters/emissive_intensity: ")));
  doc.at("parameters").update({ { "emissive_intensity", -1.0 } });
  EXPECT_THAT(ValidateJson(Schema(), doc),
    Contains(HasSubstr("/parameters/emissive_intensity: ")));
  doc.at("parameters").update({ { "emissive_intensity", 0.0 } });
  doc.at("parameters").update({ { "emissive_color", { 1.1, 0.0, 0.0 } } });
  EXPECT_THAT(ValidateJson(Schema(), doc),
    Contains(HasSubstr("/parameters/emissive_color/0: ")));
  doc.at("parameters").update({ { "emissive_color", { 1.0, 0.0, 0.0 } } });
  doc.update(
    { { "parameters", { { "emissive_factor", { 1.0, 1.0, 1.0 } } } } });
  EXPECT_THAT(
    ValidateJson(Schema(), doc), Contains(HasSubstr("'emissive_factor'")));
}

} // namespace
