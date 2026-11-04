//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/PhysicsSidecarSource.cpp

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/PhysicsSidecarSource.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  using nlohmann::json;
  using oxygen::cooker::test::DiagnosticSummary;
  using oxygen::cooker::test::HasDiagnosticCode;
  using oxygen::cooker::test::PhysicsSidecar;

  constexpr auto kShape
    = std::string_view { "/Cooked/Physics/Shapes/test_shape.ocshape" };
  constexpr auto kMaterial
    = std::string_view { "/Cooked/Physics/Materials/default.opmat" };
  constexpr auto kMesh = std::string_view { "/Cooked/Geometry/mesh.ogeo" };
  constexpr auto kSourcePath = std::string_view { "physics.sidecar.json" };

  //! The outcome of preparing one document.
  struct Prepared {
    std::optional<PhysicsSidecarDocument> document;
    std::vector<ImportDiagnostic> diagnostics;
  };

  auto Prepare(const std::string_view text) -> Prepared
  {
    auto prepared = Prepared {};
    prepared.document
      = PreparePhysicsSidecarDocument(text, kSourcePath, prepared.diagnostics);
    return prepared;
  }

  auto Sidecar() -> PhysicsSidecar
  {
    return PhysicsSidecar { "/Content/Scenes/Test.physics-sidecar" };
  }

  auto Collider(const unsigned node_index) -> json
  {
    return json {
      { "node_index", node_index },
      { "shape_ref", kShape },
      { "material_ref", kMaterial },
    };
  }

  auto SoftBody(const unsigned node_index) -> json
  {
    return json::parse(R"({
      "node_index": )"
      + std::to_string(node_index) + R"(,
      "source_mesh_ref": ")"
      + std::string(kMesh) + R"(",
      "collision_layer": 1,
      "collision_mask": 65535,
      "edge_compliance": 0.0001,
      "shear_compliance": 0.0001,
      "bend_compliance": 0.0002,
      "volume_compliance": 0.0002,
      "pressure_coefficient": 0.0,
      "tether_mode": "none",
      "tether_max_distance_multiplier": 1.0,
      "global_damping": 0.08,
      "restitution": 0.2,
      "friction": 0.6,
      "vertex_radius": 0.02,
      "solver_iteration_count": 8,
      "self_collision": false,
      "pinned_vertices": [0, 4],
      "kinematic_vertices": [1],
      "backend": { "target": "jolt" }
    })");
  }

  auto Wheel(const unsigned node_index, const unsigned axle_index,
    const std::string_view side) -> json
  {
    return json {
      { "node_index", node_index },
      { "axle_index", axle_index },
      { "side", side },
    };
  }

  auto Vehicle(const unsigned node_index, json wheels) -> json
  {
    return json {
      { "node_index", node_index },
      { "controller_type", "wheeled" },
      { "wheels", std::move(wheels) },
    };
  }

  //! The pointers of the diagnostics with `code`.
  auto PointersOf(const Prepared& prepared, const std::string_view code)
    -> std::vector<std::string>
  {
    auto pointers = std::vector<std::string> {};
    for (const auto& diagnostic : prepared.diagnostics) {
      if (diagnostic.code == code) {
        pointers.push_back(diagnostic.json_pointer);
      }
    }
    return pointers;
  }

  //! The document is refused with one `code` located at `pointer`.
  auto ExpectRefusal(const Prepared& prepared, const std::string_view code,
    const std::string_view pointer) -> void
  {
    EXPECT_FALSE(prepared.document.has_value());
    EXPECT_TRUE(HasDiagnosticCode(prepared.diagnostics, code))
      << DiagnosticSummary(prepared.diagnostics);
    EXPECT_THAT(
      PointersOf(prepared, code), ::testing::Contains(std::string(pointer)))
      << DiagnosticSummary(prepared.diagnostics);
    for (const auto& diagnostic : prepared.diagnostics) {
      EXPECT_EQ(diagnostic.source_path, kSourcePath);
      EXPECT_TRUE(diagnostic.object_path.empty());
    }
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, AcceptsValidDocument)
  {
    const auto text
      = Sidecar()
          .AppendBinding("rigid_bodies",
            json {
              { "node_index", 0 },
              { "shape_ref", kShape },
              { "material_ref", kMaterial },
              { "body_type", "dynamic" },
              { "mass", 2.0 },
            })
          .AppendBinding("colliders", Collider(1))
          .AppendBinding("soft_bodies", SoftBody(2))
          .AppendBinding("joints",
            json { { "node_index_a", 0 }, { "node_index_b", "world" } })
          .AppendBinding("vehicles",
            Vehicle(
              3, json::array({ Wheel(4, 0, "left"), Wheel(5, 0, "right") })))
          .AppendBinding("aggregates", json { { "node_index", 6 } })
          .Text();

    const auto prepared = Prepare(text);

    ASSERT_HAS_VALUE(prepared.document)
      << DiagnosticSummary(prepared.diagnostics);
    EXPECT_TRUE(prepared.diagnostics.empty());
    const auto& document = *prepared.document;
    ASSERT_EQ(document.rigid_bodies.size(), 1U);
    EXPECT_EQ(document.rigid_bodies.front().shape_ref, kShape);
    EXPECT_EQ(document.rigid_bodies.front().record.mass, 2.0F);
    ASSERT_EQ(document.colliders.size(), 1U);
    ASSERT_EQ(document.soft_bodies.size(), 1U);
    EXPECT_EQ(document.soft_bodies.front().record.pinned_vertex_count, 2U);
    EXPECT_EQ(document.soft_bodies.front().record.kinematic_vertex_count, 1U);
    ASSERT_EQ(document.joints.size(), 1U);
    EXPECT_EQ(document.joints.front().record.node_index_b,
      data::pak::physics::kWorldAttachmentNodeIndex);
    ASSERT_EQ(document.vehicles.size(), 1U);
    EXPECT_EQ(document.vehicles.front().wheels.size(), 2U);
    ASSERT_EQ(document.aggregates.size(), 1U);
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesInvalidJson)
  {
    ExpectRefusal(Prepare("{ invalid }"),
      import_codes::kPhysicsSidecarPayloadParseFailed, "");
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesJsonWithComments)
  {
    ExpectRefusal(Prepare("{ /* note */ \"bindings\": {} }"),
      import_codes::kPhysicsSidecarPayloadParseFailed, "");
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesNonObjectRoot)
  {
    ExpectRefusal(
      Prepare("[]"), import_codes::kPhysicsSidecarSchemaValidationFailed, "");
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesMissingBindings)
  {
    ExpectRefusal(
      Prepare("{}"), import_codes::kPhysicsSidecarSchemaValidationFailed, "");
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesMissingRequiredField)
  {
    const auto text
      = Sidecar()
          .AppendBinding("colliders", json { { "node_index", 0 } })
          .Text();

    ExpectRefusal(Prepare(text),
      import_codes::kPhysicsSidecarSchemaValidationFailed,
      "/bindings/colliders/0");
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, ReportsEverySchemaIssue)
  {
    const auto text
      = Sidecar()
          .AppendBinding("colliders", json { { "node_index", 0 } })
          .AppendBinding("colliders", json { { "node_index", 1 } })
          .Text();

    const auto prepared = Prepare(text);

    EXPECT_FALSE(prepared.document.has_value());
    EXPECT_THAT(
      PointersOf(prepared, import_codes::kPhysicsSidecarSchemaValidationFailed),
      ::testing::IsSupersetOf({ std::string("/bindings/colliders/0"),
        std::string("/bindings/colliders/1") }));
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesTwoBindingsOfANode)
  {
    const auto text = Sidecar()
                        .AppendBinding("colliders", Collider(0))
                        .AppendBinding("colliders", Collider(0))
                        .Text();

    ExpectRefusal(Prepare(text), import_codes::kPhysicsSidecarPayloadInvalid,
      "/bindings/colliders/1");
  }

  NOLINT_TEST(
    PhysicsSidecarPreparationTest, RefusesSoftBodyComplianceOutOfRange)
  {
    auto soft_body = SoftBody(0);
    soft_body["edge_compliance"] = 1.0e39;
    const auto text = Sidecar().AppendBinding("soft_bodies", soft_body).Text();

    ExpectRefusal(Prepare(text), import_codes::kPhysicsSidecarPayloadInvalid,
      "/bindings/soft_bodies/0/edge_compliance");
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesSoftBodyPressureOutOfRange)
  {
    auto soft_body = SoftBody(0);
    soft_body["pressure_coefficient"] = -1.0e39;
    const auto text = Sidecar().AppendBinding("soft_bodies", soft_body).Text();

    ExpectRefusal(Prepare(text), import_codes::kPhysicsSidecarPayloadInvalid,
      "/bindings/soft_bodies/0/pressure_coefficient");
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesSoftBodyWithoutIterations)
  {
    auto soft_body = SoftBody(0);
    soft_body["solver_iteration_count"] = 0;
    const auto text = Sidecar().AppendBinding("soft_bodies", soft_body).Text();

    ExpectRefusal(Prepare(text), import_codes::kPhysicsSidecarPayloadInvalid,
      "/bindings/soft_bodies/0/solver_iteration_count");
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesVehicleWithOneWheel)
  {
    const auto text = Sidecar()
                        .AppendBinding("vehicles",
                          Vehicle(0, json::array({ Wheel(1, 0, "left") })))
                        .Text();

    ExpectRefusal(Prepare(text),
      import_codes::kPhysicsSidecarSchemaValidationFailed,
      "/bindings/vehicles/0/wheels");
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesWheelOnTheChassisNode)
  {
    const auto text
      = Sidecar()
          .AppendBinding("vehicles",
            Vehicle(
              0, json::array({ Wheel(0, 0, "left"), Wheel(2, 0, "right") })))
          .Text();

    ExpectRefusal(Prepare(text), import_codes::kPhysicsSidecarPayloadInvalid,
      "/bindings/vehicles/0/wheels/0/node_index");
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesWheelsOfOneNode)
  {
    const auto text
      = Sidecar()
          .AppendBinding("vehicles",
            Vehicle(
              0, json::array({ Wheel(1, 0, "left"), Wheel(1, 0, "right") })))
          .Text();

    ExpectRefusal(Prepare(text), import_codes::kPhysicsSidecarPayloadInvalid,
      "/bindings/vehicles/0/wheels/1/node_index");
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesWheelsOfOneRole)
  {
    const auto text
      = Sidecar()
          .AppendBinding("vehicles",
            Vehicle(
              0, json::array({ Wheel(1, 0, "left"), Wheel(2, 0, "left") })))
          .Text();

    ExpectRefusal(Prepare(text), import_codes::kPhysicsSidecarPayloadInvalid,
      "/bindings/vehicles/0/wheels/1");
  }

} // namespace
} // namespace oxygen::content::import::test
