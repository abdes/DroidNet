//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/CollisionShapeSource.cpp

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/CollisionShapeSource.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  using nlohmann::json;
  using oxygen::cooker::test::CollisionShapeDescriptor;
  using oxygen::cooker::test::HasDiagnosticCode;
  namespace phys = data::pak::physics;

  constexpr auto kIdentity = "/Content/Physics/Floor.ocshape";
  constexpr auto kMaterial = "/Content/Physics/Ground.opmat";
  constexpr auto kGeometry = "/Content/Meshes/Rock.ogeo";

  auto Prepare(
    const std::string& text, std::vector<ImportDiagnostic>& diagnostics)
  {
    return internal::PrepareCollisionShapeDescriptor(
      text, "floor.ocshape.json", diagnostics);
  }

  //! A descriptor of `type` without the default box's half extents.
  auto Shape(const std::string_view type) -> CollisionShapeDescriptor
  {
    auto document = CollisionShapeDescriptor { kIdentity, kMaterial };
    document.Remove("/half_extents").Set("/shape_type", std::string(type));
    return document;
  }

  auto WorldBoundary() -> CollisionShapeDescriptor
  {
    auto document = Shape("world_boundary");
    document.Set("/boundary_mode", "aabb_clamp")
      .Set("/limits_min", { -10.0, -10.0, -10.0 })
      .Set("/limits_max", { 10.0, 10.0, 10.0 });
    return document;
  }

  //! A height field of `count` x `count` samples.
  auto HeightField(const uint32_t count, const json& heights)
    -> CollisionShapeDescriptor
  {
    auto document = Shape("height_field");
    document.Set("/sample_count", count).Set("/heights", heights);
    return document;
  }

  auto Sphere(const double radius) -> json
  {
    return {
      { "shape_type", "sphere" },
      { "radius", radius },
      { "local_position", { 0.0, 0.0, 0.0 } },
      { "local_rotation", { 0.0, 0.0, 0.0, 1.0 } },
      { "local_scale", { 1.0, 1.0, 1.0 } },
    };
  }

  auto Compound(const json& first, const json& second)
    -> CollisionShapeDescriptor
  {
    auto document = Shape("compound");
    document.Append("/children", first).Append("/children", second);
    return document;
  }

  //! The bounds of a world boundary child.
  struct Limits final {
    json min;
    json max;
  };

  auto BoundaryChild(const Limits& limits) -> json
  {
    auto child = Sphere(1.0);
    child.erase("radius");
    child["shape_type"] = "world_boundary";
    child["boundary_mode"] = "aabb_clamp";
    child["limits_min"] = limits.min;
    child["limits_max"] = limits.max;
    return child;
  }

  auto HullChild() -> json
  {
    auto child = Sphere(1.0);
    child.erase("radius");
    child["shape_type"] = "convex_hull";
    child["geometry_ref"] = kGeometry;
    return child;
  }

  //! Expects one refusal with `code` at `pointer`.
  auto ExpectRefusal(const std::string& text, const std::string_view code,
    const std::string_view pointer) -> void
  {
    std::vector<ImportDiagnostic> diagnostics;
    const auto prepared = Prepare(text, diagnostics);
    EXPECT_FALSE(prepared.has_value());
    ASSERT_EQ(diagnostics.size(), 1U) << text;
    EXPECT_EQ(diagnostics[0].code, code);
    EXPECT_EQ(diagnostics[0].json_pointer, pointer);
  }

  NOLINT_TEST(CollisionShapePreparationTest, ReadsTheTypedShape)
  {
    std::vector<ImportDiagnostic> diagnostics;
    auto document = CollisionShapeDescriptor { kIdentity, kMaterial };
    document.Set("/half_extents", { 1.0, 2.0, 3.0 })
      .Set("/collision_own_layer", 4)
      .Set("/local_scale", { 1.0, 2.0, 1.0 });

    const auto prepared = Prepare(document.Text(), diagnostics);

    ASSERT_HAS_VALUE(prepared);
    EXPECT_TRUE(diagnostics.empty());
    EXPECT_FALSE(prepared->material_ref.empty());
    EXPECT_EQ(prepared->collision_own_layer, 4U);
    EXPECT_EQ(prepared->shape.type, phys::ShapeType::kBox);
    const auto* box = std::get_if<internal::BoxParams>(&prepared->shape.params);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->half_extents, (std::array { 1.0F, 2.0F, 3.0F }));
    EXPECT_TRUE(prepared->children.empty());
  }

  NOLINT_TEST(CollisionShapePreparationTest, AcceptsAWorldBoundaryAndCompound)
  {
    std::vector<ImportDiagnostic> diagnostics;

    EXPECT_TRUE(Prepare(WorldBoundary().Text(), diagnostics).has_value());
    const auto compound
      = Prepare(Compound(Sphere(1.0),
                  BoundaryChild({ .min = { -1, -1, -1 }, .max = { 1, 1, 1 } }))
                  .Text(),
        diagnostics);
    ASSERT_HAS_VALUE(compound);
    EXPECT_TRUE(diagnostics.empty());
    ASSERT_EQ(compound->children.size(), 2U);
    EXPECT_EQ(compound->children[1].type, phys::ShapeType::kWorldBoundary);
    EXPECT_EQ(compound->children[1].json_pointer, "/children/1");
  }

  NOLINT_TEST(CollisionShapePreparationTest, ReadsAConvexHullWithDefaults)
  {
    std::vector<ImportDiagnostic> diagnostics;
    auto document = Shape("convex_hull");
    document.Set("/geometry_ref", kGeometry);

    const auto prepared = Prepare(document.Text(), diagnostics);

    ASSERT_HAS_VALUE(prepared);
    const auto* hull
      = std::get_if<internal::ConvexHullParams>(&prepared->shape.params);
    ASSERT_NE(hull, nullptr);
    EXPECT_EQ(hull->geometry_ref, kGeometry);
    EXPECT_FLOAT_EQ(
      hull->convex_radius, internal::ConvexHullParams::kDefaultConvexRadius);
    EXPECT_FLOAT_EQ(
      hull->hull_tolerance, internal::ConvexHullParams::kDefaultHullTolerance);
  }

  NOLINT_TEST(CollisionShapePreparationTest, ListsEveryGeometryReference)
  {
    std::vector<ImportDiagnostic> diagnostics;
    auto mesh_child = HullChild();
    mesh_child["shape_type"] = "triangle_mesh";
    mesh_child["geometry_ref"] = "/Content/Meshes/Floor.ogeo";

    const auto prepared
      = Prepare(Compound(HullChild(), mesh_child).Text(), diagnostics);

    ASSERT_HAS_VALUE(prepared);
    const auto references = internal::GeometryReferencesOf(*prepared);
    ASSERT_EQ(references.size(), 2U);
    EXPECT_EQ(references[0].reference, kGeometry);
    EXPECT_EQ(references[0].json_pointer, "/children/0/geometry_ref");
    EXPECT_EQ(references[1].reference, "/Content/Meshes/Floor.ogeo");
    EXPECT_EQ(references[1].json_pointer, "/children/1/geometry_ref");
  }

  NOLINT_TEST(CollisionShapePreparationTest, ReadsHeightFieldHoles)
  {
    std::vector<ImportDiagnostic> diagnostics;

    const auto prepared
      = Prepare(HeightField(2, { 0.0, nullptr, 1.5, 2.0 }).Text(), diagnostics);

    ASSERT_HAS_VALUE(prepared);
    const auto* field
      = std::get_if<internal::HeightFieldParams>(&prepared->shape.params);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->sample_count, 2U);
    ASSERT_EQ(field->heights.size(), 4U);
    EXPECT_FALSE(field->heights[1].has_value());
    EXPECT_EQ(field->heights[2], 1.5F);
    EXPECT_EQ(
      field->block_size, internal::HeightFieldParams::kDefaultBlockSize);
  }

  NOLINT_TEST(CollisionShapePreparationTest, RefusesAnIncompleteHeightGrid)
  {
    ExpectRefusal(HeightField(3, { 0.0, 1.0, 2.0, 3.0 }).Text(),
      import_codes::kPhysicsShapeHeightFieldInvalid, "/heights");
    ExpectRefusal(HeightField(2, { nullptr, nullptr, nullptr, nullptr }).Text(),
      import_codes::kPhysicsShapeHeightFieldInvalid, "/heights");
  }

  NOLINT_TEST(CollisionShapePreparationTest, RefusesInvalidJson)
  {
    std::vector<ImportDiagnostic> diagnostics;

    const auto prepared = Prepare("{ \"shape_type\": ", diagnostics);

    EXPECT_FALSE(prepared.has_value());
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(
      diagnostics[0].code, import_codes::kPhysicsShapeRequestInvalidJson);
    EXPECT_EQ(diagnostics[0].source_path, "floor.ocshape.json");
  }

  NOLINT_TEST(CollisionShapePreparationTest, RefusesANonObjectRoot)
  {
    ExpectRefusal("\"box\"", import_codes::kPhysicsShapeRequestInvalidJson, "");
  }

  NOLINT_TEST(CollisionShapePreparationTest, LocatesASchemaIssue)
  {
    auto document = Shape("sphere");
    document.Set("/radius", -1.0);

    ExpectRefusal(document.Text(),
      import_codes::kPhysicsShapeSchemaValidationFailed, "/radius");
  }

  NOLINT_TEST(CollisionShapePreparationTest, RefusesAValueAFloatCannotHold)
  {
    auto sphere = Shape("sphere");
    sphere.Set("/radius", 1.0e100);
    ExpectRefusal(
      sphere.Text(), import_codes::kPhysicsShapeValueOutOfRange, "/radius");

    auto placed = CollisionShapeDescriptor { kIdentity, kMaterial };
    placed.Set("/local_position", { 0.0, -1.0e300, 0.0 });
    ExpectRefusal(placed.Text(), import_codes::kPhysicsShapeValueOutOfRange,
      "/local_position/1");

    auto child = Sphere(1.0);
    child["local_position"] = { 1.0e40, 0.0, 0.0 };
    ExpectRefusal(Compound(Sphere(1.0), child).Text(),
      import_codes::kPhysicsShapeValueOutOfRange,
      "/children/1/local_position/0");
  }

  NOLINT_TEST(CollisionShapePreparationTest, RefusesADimensionThatUnderflows)
  {
    auto sphere = Shape("sphere");
    sphere.Set("/radius", 1.0e-50);
    ExpectRefusal(
      sphere.Text(), import_codes::kPhysicsShapeValueOutOfRange, "/radius");

    auto box = CollisionShapeDescriptor { kIdentity, kMaterial };
    box.Set("/half_extents", { 1.0, 1.0e-60, 1.0 });
    ExpectRefusal(box.Text(), import_codes::kPhysicsShapeValueOutOfRange,
      "/half_extents/1");
  }

  NOLINT_TEST(CollisionShapePreparationTest, AcceptsTheBoundaryValues)
  {
    std::vector<ImportDiagnostic> diagnostics;
    auto document = Shape("sphere");
    document
      .Set("/radius", static_cast<double>(std::numeric_limits<float>::max()))
      .Set("/collision_own_layer", uint64_t { 1 } << 63U)
      .Set("/collision_target_layers", std::numeric_limits<uint64_t>::max());

    const auto prepared = Prepare(document.Text(), diagnostics);

    ASSERT_HAS_VALUE(prepared);
    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(prepared->collision_own_layer, uint64_t { 1 } << 63U);
    EXPECT_EQ(
      prepared->collision_target_layers, std::numeric_limits<uint64_t>::max());
  }

  NOLINT_TEST(CollisionShapePreparationTest, RefusesANegativeLayer)
  {
    auto document = CollisionShapeDescriptor { kIdentity, kMaterial };
    document.Set("/collision_own_layer", -1);
    ExpectRefusal(document.Text(), import_codes::kPhysicsShapeValueOutOfRange,
      "/collision_own_layer");

    auto targets = CollisionShapeDescriptor { kIdentity, kMaterial };
    targets.Set("/collision_target_layers", -1);
    ExpectRefusal(targets.Text(), import_codes::kPhysicsShapeValueOutOfRange,
      "/collision_target_layers");
  }

  NOLINT_TEST(CollisionShapePreparationTest, RefusesAnOwnLayerOfSeveralBits)
  {
    auto document = CollisionShapeDescriptor { kIdentity, kMaterial };
    document.Set("/collision_own_layer", 3);
    ExpectRefusal(document.Text(), import_codes::kPhysicsShapeOwnLayerInvalid,
      "/collision_own_layer");

    auto none = CollisionShapeDescriptor { kIdentity, kMaterial };
    none.Set("/collision_own_layer", 0);
    ExpectRefusal(none.Text(), import_codes::kPhysicsShapeOwnLayerInvalid,
      "/collision_own_layer");
  }

  NOLINT_TEST(CollisionShapePreparationTest, RefusesAMemberTheTypeDoesNotUse)
  {
    auto box = CollisionShapeDescriptor { kIdentity, kMaterial };
    box.Set("/radius", 1.0);
    ExpectRefusal(
      box.Text(), import_codes::kPhysicsShapeMemberNotApplicable, "/radius");

    auto child = Sphere(1.0);
    child["half_extents"] = { 1.0, 1.0, 1.0 };
    ExpectRefusal(Compound(Sphere(1.0), child).Text(),
      import_codes::kPhysicsShapeMemberNotApplicable,
      "/children/1/half_extents");
  }

  NOLINT_TEST(
    CollisionShapePreparationTest, RefusesANonUniformScaleWhereUniform)
  {
    auto sphere = Shape("sphere");
    sphere.Set("/radius", 1.0).Set("/local_scale", { 1.0, 2.0, 1.0 });
    ExpectRefusal(sphere.Text(), import_codes::kPhysicsShapeScaleNotUniform,
      "/local_scale");

    auto hull = Shape("convex_hull");
    hull.Set("/geometry_ref", kGeometry).Set("/local_scale", { 2.0, 1.0, 1.0 });
    ExpectRefusal(
      hull.Text(), import_codes::kPhysicsShapeScaleNotUniform, "/local_scale");
  }

  NOLINT_TEST(CollisionShapePreparationTest, RefusesANonUnitRotation)
  {
    auto document = CollisionShapeDescriptor { kIdentity, kMaterial };
    document.Set("/local_rotation", { 0.0, 0.0, 0.0, 2.0 });

    ExpectRefusal(document.Text(), import_codes::kPhysicsShapeValueOutOfRange,
      "/local_rotation");
  }

  NOLINT_TEST(CollisionShapePreparationTest, RefusesADegenerateAnalyticShape)
  {
    auto plane = Shape("plane");
    plane.Set("/normal", { 0.0, 0.0, 0.0 }).Set("/distance", 0.0);
    ExpectRefusal(
      plane.Text(), import_codes::kPhysicsShapeValueOutOfRange, "/normal");

    auto cylinder = Shape("cylinder");
    cylinder.Set("/radius", 1.0).Set("/half_height", 0.0);
    ExpectRefusal(cylinder.Text(), import_codes::kPhysicsShapeValueOutOfRange,
      "/half_height");
  }

  NOLINT_TEST(
    CollisionShapePreparationTest, RefusesUnorderedWorldBoundaryLimits)
  {
    auto inverted = WorldBoundary();
    inverted.Set("/limits_min", { 0.0, 20.0, 0.0 });
    ExpectRefusal(inverted.Text(),
      import_codes::kPhysicsShapeWorldBoundaryLimitsInvalid, "/limits_max/1");

    // The runtime needs a positive extent on every axis.
    auto flat = WorldBoundary();
    flat.Set("/limits_min", { -10.0, -10.0, 10.0 });
    ExpectRefusal(flat.Text(),
      import_codes::kPhysicsShapeWorldBoundaryLimitsInvalid, "/limits_max/2");
  }

  NOLINT_TEST(CollisionShapePreparationTest, ReportsEveryInvertedCompoundChild)
  {
    std::vector<ImportDiagnostic> diagnostics;
    auto document
      = Compound(BoundaryChild({ .min = { 5, 0, 0 }, .max = { 1, 1, 1 } }),
        BoundaryChild({ .min = { 0, 0, 9 }, .max = { 1, 1, 1 } }));

    const auto prepared = Prepare(document.Text(), diagnostics);

    EXPECT_FALSE(prepared.has_value());
    ASSERT_EQ(diagnostics.size(), 2U);
    EXPECT_EQ(diagnostics[0].json_pointer, "/children/0/limits_max/0");
    EXPECT_EQ(diagnostics[1].json_pointer, "/children/1/limits_max/2");
  }

  NOLINT_TEST(CollisionShapePreparationTest, SchemaRefusesANestedCompound)
  {
    std::vector<ImportDiagnostic> diagnostics;
    auto nested = Sphere(1.0);
    nested["shape_type"] = "compound";
    auto document = Compound(Sphere(1.0), nested);

    const auto prepared = Prepare(document.Text(), diagnostics);

    EXPECT_FALSE(prepared.has_value());
    EXPECT_TRUE(HasDiagnosticCode(
      diagnostics, import_codes::kPhysicsShapeSchemaValidationFailed));
  }

} // namespace
} // namespace oxygen::content::import::test
