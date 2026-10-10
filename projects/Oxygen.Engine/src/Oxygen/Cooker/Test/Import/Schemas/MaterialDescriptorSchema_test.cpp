//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.material-descriptor.schema.json

#include <string>
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
    = LoadSchema("Import/Schemas/oxygen.material-descriptor.schema.json");
  return schema;
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
      R"({
    "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.material-descriptor.schema.json",
    "name": "WoodFloor",
    "content_hashing": true,
    "domain": "opaque",
    "alpha_mode": "opaque",
    "orm_policy": "auto",
    "parameters": {
      "base_color": [1.0, 1.0, 1.0, 1.0],
      "metalness": 0.1,
      "roughness": 0.8
    },
    "textures": {
      "base_color": {
        "virtual_path": "/.cooked/Textures/WoodFloor_Color.otex",
        "uv_set": 0,
        "uv_transform": {
          "scale": [1.0, 1.0],
          "offset": [0.0, 0.0],
          "rotation_radians": 0.0
        }
      },
      "roughness": {
        "virtual_path": "/.cooked/Textures/WoodFloor_Roughness.otex"
      }
    },
    "shaders": [
      {
        "stage": "vertex",
        "source_path": "Vortex/Stages/Translucency/ForwardMesh_VS.hlsl",
        "entry_point": "MainVS"
      },
      {
        "stage": "pixel",
        "source_path": "Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
        "entry_point": "MainPS",
        "defines": "USE_FOG=1"
      }
    ]
  }))",
      true,
      "",
    },
    SchemaCase {
      "RejectsUnknownNestedFields",
      R"({
    "name": "WoodFloor",
    "textures": {
      "base_color": {
        "virtual_path": "/.cooked/Textures/WoodFloor_Color.otex",
        "unknown_setting": true
      }
    }
  })",
      false,
      "'unknown_setting'",
    },
    SchemaCase {
      "RequiresTextureVirtualPath",
      R"({
    "name": "WoodFloor",
    "textures": {
      "base_color": {
        "uv_set": 0
      }
    }
  })",
      false,
      "'virtual_path'",
    }),
  oxygen::cooker::test::SchemaCaseName);

NOLINT_TEST(MaterialDescriptorSchemaTest, ValidatesCanonicalEmission)
{
  auto doc = json::parse(R"({
    "name": "HDR",
    "parameters": {
      "emissive_color": [1.0, 0.25, 0.0],
      "emissive_intensity": 65504.0
    }
  })");
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
