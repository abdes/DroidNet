//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.collision-shape-descriptor.schema.json

#include <string>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::CollisionShapeDescriptor;
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

//! A unit box named `name` of the physics material `material`.
auto Shape(const std::string& name, const std::string& material)
  -> CollisionShapeDescriptor
{
  return CollisionShapeDescriptor {
    "/Cooked/Physics/Shapes/" + name + ".ocshape",
    "/Cooked/Physics/Materials/" + material + ".opmat",
  }
    .Set("/name", name);
}

//! A payload-backed shape: the hull of a cooked geometry.
auto HullShape() -> CollisionShapeDescriptor
{
  return Shape("hull_shape", "steel")
    .Set("/shape_type", "convex_hull")
    .Remove("/half_extents")
    .Set("/geometry_ref", "../../Meshes/rock.ogeo");
}

//! A 2 x 2 height field.
auto HeightFieldShape() -> CollisionShapeDescriptor
{
  return Shape("terrain", "ground")
    .Set("/shape_type", "height_field")
    .Remove("/half_extents")
    .Set("/sample_count", 2)
    .Set("/heights", json::array({ 0.0, 1.0, nullptr, 2.0 }));
}

auto CompoundShape() -> CollisionShapeDescriptor
{
  return Shape("compound_shape", "steel")
    .Set("/shape_type", "compound")
    .Remove("/half_extents");
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
      Shape("floor_box", "ground")
        .Set("/$schema",
          "./src/Oxygen/Cooker/Import/Schemas/"
          "oxygen.collision-shape-descriptor.schema.json")
        .Set("/half_extents", json::array({ 25.0, 0.5, 25.0 }))
        .Set("/local_position", json::array({ 0.0, -0.5, 0.0 }))
        .Set("/collision_own_layer", 1)
        .Set("/collision_target_layers", 18446744073709551615ULL)
        .Text(),
      true,
      "",
    },
    SchemaCase {
      "AcceptsRelativeMaterialReference",
      Shape("floor_box", "ground")
        .Set("/material_ref", "../Materials/ground.opmat")
        .Text(),
      true,
      "",
    },
    SchemaCase {
      "RejectsMaterialReferenceOfAnotherType",
      Shape("floor_box", "ground")
        .Set("/material_ref", "../Materials/ground.ocshape")
        .Text(),
      false,
      "/material_ref: ",
    },
    SchemaCase {
      "RejectsStaleIdentityField",
      Shape("floor_box", "ground")
        .Set("/virtual_path", "/Cooked/Physics/Shapes/floor_box.ocshape")
        .Text(),
      false,
      "'virtual_path'",
    },
    SchemaCase {
      "RejectsTopLevelUnknownFieldDocument",
      HullShape()
        .Set("/unexpected_field", "/Cooked/Physics/Resources/hull.opres")
        .Text(),
      false,
      "'unexpected_field'",
    },
    SchemaCase {
      "AcceptsPayloadBackedShapeWithoutUnknownField",
      HullShape().Text(),
      true,
      "",
    },
    SchemaCase {
      "RejectsUnknownPayloadFieldForPrimitiveShape",
      Shape("sphere_shape", "steel")
        .Set("/shape_type", "sphere")
        .Remove("/half_extents")
        .Set("/radius", 1.0)
        .Set("/unexpected_field", "/Cooked/Physics/Resources/not_allowed.opres")
        .Text(),
      false,
      "'unexpected_field'",
    },
    SchemaCase {
      "AcceptsCompoundShapeWithChildren",
      CompoundShape()
        .Append("/children", json::parse(R"({
            "shape_type": "sphere",
            "radius": 0.4,
            "local_position": [0.0, 0.0, 0.0],
            "local_rotation": [0.0, 0.0, 0.0, 1.0],
            "local_scale": [1.0, 1.0, 1.0]
          })"))
        .Append("/children", json::parse(R"({
            "shape_type": "box",
            "half_extents": [0.2, 0.3, 0.4],
            "local_position": [0.7, 0.0, 0.0],
            "local_rotation": [0.0, 0.0, 0.0, 1.0],
            "local_scale": [1.0, 1.0, 1.0]
          })"))
        .Text(),
      true,
      "",
    },
    SchemaCase {
      "RejectsCompoundShapeWithoutChildren",
      CompoundShape().Text(),
      false,
      "'children'",
    },
    SchemaCase {
      "RejectsConvexHullWithoutGeometry",
      HullShape().Remove("/geometry_ref").Text(),
      false,
      "'geometry_ref'",
    },
    SchemaCase {
      "RejectsGeometryReferenceOfAnotherType",
      HullShape().Set("/geometry_ref", "../../Meshes/rock.opmat").Text(),
      false,
      "/geometry_ref: ",
    },
    SchemaCase {
      "AcceptsCompoundConvexHullChild",
      CompoundShape()
        .Append("/children", json::parse(R"({
            "shape_type": "convex_hull",
            "geometry_ref": "../../Meshes/rock.ogeo",
            "local_position": [0.0, 0.0, 0.0],
            "local_rotation": [0.0, 0.0, 0.0, 1.0],
            "local_scale": [1.0, 1.0, 1.0]
          })"))
        .Text(),
      true,
      "",
    },
    SchemaCase {
      "AcceptsHeightFieldWithHoles",
      HeightFieldShape().Text(),
      true,
      "",
    },
    SchemaCase {
      "RejectsHeightFieldBlockSizeOutOfRange",
      HeightFieldShape().Set("/block_size", 9).Text(),
      false,
      "/block_size: ",
    },
    SchemaCase {
      "RejectsHeightFieldOfOneSample",
      HeightFieldShape().Set("/sample_count", 1).Text(),
      false,
      "/sample_count: ",
    },
    SchemaCase {
      "AcceptsTheHighestOwnLayer",
      Shape("floor_box", "ground")
        .Set("/collision_own_layer", 9223372036854775808ULL)
        .Text(),
      true,
      "",
    },
    SchemaCase {
      "RejectsAFractionalLayer",
      Shape("floor_box", "ground").Set("/collision_own_layer", 1.5).Text(),
      false,
      "/collision_own_layer: ",
    }),
  oxygen::cooker::test::SchemaCaseName);

NOLINT_TEST(CollisionShapeDescriptorSchemaTest, CapsuleAllowsSphereLimit)
{
  auto doc = Shape("capsule", "default")
               .Set("/shape_type", "capsule")
               .Remove("/half_extents")
               .Set("/radius", 0.5)
               .Set("/half_height", 0.0)
               .Json();
  EXPECT_THAT(ValidateJson(Schema(), doc), IsEmpty());
  doc["half_height"] = -0.1;
  EXPECT_THAT(
    ValidateJson(Schema(), doc), Contains(HasSubstr("/half_height: ")));
}

} // namespace
