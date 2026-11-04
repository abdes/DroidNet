//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/CollisionShapeSource.cpp

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/CollisionShapeSource.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  using nlohmann::json;
  using oxygen::cooker::test::CollisionShapeDescriptor;
  using oxygen::cooker::test::HasDiagnosticCode;

  constexpr auto kIdentity = "/Content/Physics/Floor.ocshape";
  constexpr auto kMaterial = "/Content/Physics/Ground.opmat";

  auto Prepare(
    const std::string& text, std::vector<ImportDiagnostic>& diagnostics)
  {
    return internal::PrepareCollisionShapeDescriptor(
      text, "floor.ocshape.json", diagnostics);
  }

  auto WorldBoundary() -> CollisionShapeDescriptor
  {
    auto document = CollisionShapeDescriptor { kIdentity, kMaterial };
    document.Remove("/half_extents")
      .Set("/shape_type", "world_boundary")
      .Set("/boundary_mode", "aabb_clamp")
      .Set("/limits_min", { -10.0, -10.0, -10.0 })
      .Set("/limits_max", { 10.0, 10.0, 10.0 });
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
    auto document = CollisionShapeDescriptor { kIdentity, kMaterial };
    document.Remove("/half_extents")
      .Set("/shape_type", "compound")
      .Append("/children", first)
      .Append("/children", second);
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

  NOLINT_TEST(CollisionShapePreparationTest, AcceptsAValidDocument)
  {
    std::vector<ImportDiagnostic> diagnostics;

    const auto prepared = Prepare(
      CollisionShapeDescriptor { kIdentity, kMaterial }.Text(), diagnostics);

    ASSERT_HAS_VALUE(prepared);
    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(prepared->at("shape_type"), "box");
  }

  NOLINT_TEST(CollisionShapePreparationTest, AcceptsAWorldBoundaryAndCompound)
  {
    std::vector<ImportDiagnostic> diagnostics;

    EXPECT_TRUE(Prepare(WorldBoundary().Text(), diagnostics).has_value());
    EXPECT_TRUE(
      Prepare(Compound(Sphere(1.0),
                BoundaryChild({ .min = { -1, -1, -1 }, .max = { 1, 1, 1 } }))
                .Text(),
        diagnostics)
        .has_value());
    EXPECT_TRUE(diagnostics.empty());
  }

  NOLINT_TEST(CollisionShapePreparationTest, RefusesInvalidJson)
  {
    std::vector<ImportDiagnostic> diagnostics;

    const auto prepared = Prepare("{ \"shape_type\": ", diagnostics);

    EXPECT_FALSE(prepared.has_value());
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics[0].code, "physics.shape.request_invalid_json");
    EXPECT_EQ(diagnostics[0].source_path, "floor.ocshape.json");
  }

  NOLINT_TEST(CollisionShapePreparationTest, RefusesANonObjectRoot)
  {
    std::vector<ImportDiagnostic> diagnostics;

    const auto prepared = Prepare("\"box\"", diagnostics);

    EXPECT_FALSE(prepared.has_value());
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics[0].code, "physics.shape.request_invalid_json");
  }

  NOLINT_TEST(CollisionShapePreparationTest, LocatesASchemaIssue)
  {
    std::vector<ImportDiagnostic> diagnostics;
    auto document = CollisionShapeDescriptor { kIdentity, kMaterial };
    document.Remove("/half_extents")
      .Set("/shape_type", "sphere")
      .Set("/radius", -1.0);

    const auto prepared = Prepare(document.Text(), diagnostics);

    EXPECT_FALSE(prepared.has_value());
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics[0].code, "physics.shape.schema_validation_failed");
    EXPECT_EQ(diagnostics[0].json_pointer, "/radius");
    EXPECT_TRUE(diagnostics[0].object_path.empty());
  }

  NOLINT_TEST(CollisionShapePreparationTest, RefusesInvertedWorldBoundaryLimits)
  {
    std::vector<ImportDiagnostic> diagnostics;
    auto document = WorldBoundary();
    document.Set("/limits_min", { 0.0, 20.0, 0.0 });

    const auto prepared = Prepare(document.Text(), diagnostics);

    EXPECT_FALSE(prepared.has_value());
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(
      diagnostics[0].code, "physics.shape.world_boundary_limits_invalid");
    EXPECT_EQ(diagnostics[0].json_pointer, "/limits_min");
  }

  NOLINT_TEST(
    CollisionShapePreparationTest, RefusesInvertedCompoundChildLimitsByIndex)
  {
    std::vector<ImportDiagnostic> diagnostics;
    auto document = Compound(
      Sphere(1.0), BoundaryChild({ .min = { 5, 0, 0 }, .max = { 1, 1, 1 } }));

    const auto prepared = Prepare(document.Text(), diagnostics);

    EXPECT_FALSE(prepared.has_value());
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(
      diagnostics[0].code, "physics.shape.world_boundary_limits_invalid");
    EXPECT_EQ(diagnostics[0].json_pointer, "/children/1/limits_min");
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
    EXPECT_EQ(diagnostics[0].json_pointer, "/children/0/limits_min");
    EXPECT_EQ(diagnostics[1].json_pointer, "/children/1/limits_min");
  }

  NOLINT_TEST(CollisionShapePreparationTest, SchemaRefusesNonAnalyticChildren)
  {
    std::vector<ImportDiagnostic> diagnostics;
    auto nested = Sphere(1.0);
    nested["shape_type"] = "compound";
    auto document = Compound(Sphere(1.0), nested);

    const auto prepared = Prepare(document.Text(), diagnostics);

    EXPECT_FALSE(prepared.has_value());
    EXPECT_TRUE(
      HasDiagnosticCode(diagnostics, "physics.shape.schema_validation_failed"));
  }

} // namespace
} // namespace oxygen::content::import::test
