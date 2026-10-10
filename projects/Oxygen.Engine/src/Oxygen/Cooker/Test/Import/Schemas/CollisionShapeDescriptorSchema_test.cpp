//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.collision-shape-descriptor.schema.json

#include <string>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

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
  static const auto schema = LoadSchema(
    "Import/Schemas/oxygen.collision-shape-descriptor.schema.json");
  return schema;
}

class CollisionShapeDescriptorSchemaCaseTest
  : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(CollisionShapeDescriptorSchemaCaseTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, CollisionShapeDescriptorSchemaCaseTest,
  ::testing::Values(
    SchemaCase {
      "AcceptsCanonicalPrimitiveDocument",
      R"({
    "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.collision-shape-descriptor.schema.json",
    "name": "floor_box",
    "shape_type": "box",
    "material_ref": "/.cooked/Physics/Materials/ground.opmat",
    "half_extents": [25.0, 0.5, 25.0],
    "local_position": [0.0, -0.5, 0.0],
    "collision_own_layer": 1,
    "collision_target_layers": 18446744073709551615
  })",
      true,
      "",
    },
    SchemaCase {
      "RejectsTopLevelUnknownFieldDocument",
      R"({
    "name": "hull_shape",
    "shape_type": "convex_hull",
    "material_ref": "/.cooked/Physics/Materials/steel.opmat",
    "unexpected_field": "/.cooked/Physics/Resources/hull.opres"
  })",
      false,
      "'unexpected_field'",
    },
    SchemaCase {
      "AcceptsPayloadBackedShapeWithoutUnknownField",
      R"({
    "name": "hull_shape",
    "shape_type": "convex_hull",
    "material_ref": "/.cooked/Physics/Materials/steel.opmat"
  })",
      true,
      "",
    },
    SchemaCase {
      "RejectsUnknownPayloadFieldForPrimitiveShape",
      R"({
    "name": "sphere_shape",
    "shape_type": "sphere",
    "material_ref": "/.cooked/Physics/Materials/steel.opmat",
    "radius": 1.0,
    "unexpected_field": "/.cooked/Physics/Resources/not_allowed.opres"
  })",
      false,
      "'unexpected_field'",
    },
    SchemaCase {
      "AcceptsCompoundShapeWithChildren",
      R"({
    "name": "compound_shape",
    "shape_type": "compound",
    "material_ref": "/.cooked/Physics/Materials/steel.opmat",
    "children": [
      {
        "shape_type": "sphere",
        "radius": 0.4,
        "local_position": [0.0, 0.0, 0.0],
        "local_rotation": [0.0, 0.0, 0.0, 1.0],
        "local_scale": [1.0, 1.0, 1.0]
      },
      {
        "shape_type": "box",
        "half_extents": [0.2, 0.3, 0.4],
        "local_position": [0.7, 0.0, 0.0],
        "local_rotation": [0.0, 0.0, 0.0, 1.0],
        "local_scale": [1.0, 1.0, 1.0]
      }
    ]
  })",
      true,
      "",
    },
    SchemaCase {
      "RejectsCompoundShapeWithoutChildren",
      R"({
    "name": "compound_shape",
    "shape_type": "compound",
    "material_ref": "/.cooked/Physics/Materials/steel.opmat"
  })",
      false,
      "'children'",
    }),
  oxygen::cooker::test::SchemaCaseName);

NOLINT_TEST(CollisionShapeDescriptorSchemaTest, CapsuleAllowsSphereLimit)
{
  auto doc = json {
    { "shape_type", "capsule" },
    { "material_ref", "/.cooked/Physics/Materials/default.opmat" },
    { "radius", 0.5 },
    { "half_height", 0.0 },
  };
  EXPECT_THAT(ValidateJson(Schema(), doc), IsEmpty());
  doc["half_height"] = -0.1;
  EXPECT_THAT(
    ValidateJson(Schema(), doc), Contains(HasSubstr("/half_height: ")));
}

} // namespace
