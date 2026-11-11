//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/PhysicsSidecarSource.cpp

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/PhysicsSidecarSource.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Core/Meta/Physics/Backend.h>
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

    ExpectRefusal(Prepare(text), import_codes::kPhysicsSidecarBindingDuplicate,
      "/bindings/colliders/1");
  }

  NOLINT_TEST(
    PhysicsSidecarPreparationTest, RefusesSoftBodyComplianceOutOfRange)
  {
    auto soft_body = SoftBody(0);
    soft_body["edge_compliance"] = 1.0e39;
    const auto text = Sidecar().AppendBinding("soft_bodies", soft_body).Text();

    ExpectRefusal(Prepare(text), import_codes::kPhysicsSidecarValueOutOfRange,
      "/bindings/soft_bodies/0/edge_compliance");
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesSoftBodyPressureOutOfRange)
  {
    auto soft_body = SoftBody(0);
    soft_body["pressure_coefficient"] = -1.0e39;
    const auto text = Sidecar().AppendBinding("soft_bodies", soft_body).Text();

    ExpectRefusal(Prepare(text), import_codes::kPhysicsSidecarValueOutOfRange,
      "/bindings/soft_bodies/0/pressure_coefficient");
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesSoftBodyWithoutIterations)
  {
    auto soft_body = SoftBody(0);
    soft_body["solver_iteration_count"] = 0;
    const auto text = Sidecar().AppendBinding("soft_bodies", soft_body).Text();

    ExpectRefusal(Prepare(text), import_codes::kPhysicsSidecarValueOutOfRange,
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

  auto Joint(const std::string_view type) -> json
  {
    return json {
      { "node_index_a", 0 },
      { "node_index_b", 1 },
      { "constraint_type", type },
    };
  }

  auto Axes(const float tx, const float rx) -> json
  {
    return json::array({ tx, 0.0F, 0.0F, rx, 0.0F, 0.0F });
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, ReadsJointSettings)
  {
    auto hinge = Joint("hinge");
    hinge["constraint_space"] = "local";
    hinge["local_frame_a_position"] = { 1.0F, 2.0F, 3.0F };
    hinge["local_frame_a_rotation"] = { 0.0F, 0.0F, 1.0F, 0.0F };
    hinge["limits_lower"] = Axes(0.0F, -0.5F);
    hinge["limits_upper"] = Axes(0.0F, 0.5F);
    hinge["priority"] = 4;
    hinge["backend"] = { { "target", "jolt" } };
    const auto text = Sidecar().AppendBinding("joints", hinge).Text();

    const auto prepared = Prepare(text);

    ASSERT_HAS_VALUE(prepared.document)
      << DiagnosticSummary(prepared.diagnostics);
    ASSERT_EQ(prepared.document->joints.size(), 1U);
    const auto& joint = prepared.document->joints.front();
    EXPECT_EQ(
      joint.authored_backend, core::meta::physics::PhysicsBackend::kJolt);
    const auto& settings = joint.settings;
    EXPECT_EQ(settings.type, JointConstraintType::kHinge);
    EXPECT_EQ(settings.space, JointConstraintSpace::kLocal);
    EXPECT_EQ(settings.frame_a.position, (std::array { 1.0F, 2.0F, 3.0F }));
    EXPECT_EQ(
      settings.frame_a.rotation, (std::array { 0.0F, 0.0F, 1.0F, 0.0F }));
    ASSERT_HAS_VALUE(settings.limits_upper);
    EXPECT_EQ(settings.limits_upper->at(3), 0.5F);
    EXPECT_EQ(settings.priority, 4U);
  }

  //! Each binding records the backend.target it authors, `kNone` without one.
  NOLINT_TEST(PhysicsSidecarPreparationTest, RecordsTheAuthoredBackend)
  {
    auto rigid_body = Collider(0);
    rigid_body["backend"] = { { "target", "physx" } };
    const auto text = Sidecar()
                        .AppendBinding("rigid_bodies", rigid_body)
                        .AppendBinding("joints", Joint("fixed"))
                        .Text();

    const auto prepared = Prepare(text);

    ASSERT_HAS_VALUE(prepared.document)
      << DiagnosticSummary(prepared.diagnostics);
    EXPECT_EQ(prepared.document->rigid_bodies.front().authored_backend,
      core::meta::physics::PhysicsBackend::kPhysX);
    EXPECT_EQ(prepared.document->joints.front().authored_backend,
      core::meta::physics::PhysicsBackend::kNone);
  }

  //! Members the engine does not implement yet are refused, never dropped.
  class UnsupportedMemberTest
    : public ::testing::TestWithParam<std::pair<std::string, json>> { };

  NOLINT_TEST_P(UnsupportedMemberTest, IsRefusedWhereAuthored)
  {
    const auto& [pointer, value] = GetParam();
    const auto is_soft_body = pointer.starts_with("/backend");
    auto binding = is_soft_body ? SoftBody(0) : Joint("hinge");
    binding[json::json_pointer(pointer)] = value;
    const auto* const kind = is_soft_body ? "soft_bodies" : "joints";
    const auto text = Sidecar().AppendBinding(kind, binding).Text();

    ExpectRefusal(Prepare(text),
      import_codes::kPhysicsSidecarCapabilityUnsupported,
      "/bindings/" + std::string(kind) + "/0" + pointer);
  }

  INSTANTIATE_TEST_SUITE_P(PhysicsSidecarPreparationTest, UnsupportedMemberTest,
    ::testing::Values(std::pair { std::string("/break_force"), json(10.0) },
      std::pair { std::string("/break_torque"), json(10.0) },
      std::pair { std::string("/collide_connected"), json(true) },
      std::pair { std::string("/motor_modes"),
        json::array({ "off", "off", "off", "velocity", "off", "off" }) },
      std::pair { std::string("/motor_target_velocities"), Axes(0.0F, 1.0F) },
      std::pair { std::string("/motor_target_positions"), Axes(0.0F, 1.0F) },
      std::pair { std::string("/backend/lra_stiffness_fraction"), json(0.5) },
      std::pair {
        std::string("/backend/skinned_constraint_enable"), json(true) }));

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesAnUnpairedJointMember)
  {
    auto hinge = Joint("hinge");
    hinge["spring_stiffnesses"] = Axes(0.0F, 10.0F);
    const auto text = Sidecar().AppendBinding("joints", hinge).Text();

    ExpectRefusal(Prepare(text),
      import_codes::kPhysicsSidecarJointMembersUnpaired,
      "/bindings/joints/0/spring_stiffnesses");
  }

  //! A hinge turns about rx only; a value on another axis has no meaning.
  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesAValueOnAnUnusedAxis)
  {
    auto hinge = Joint("hinge");
    hinge["limits_lower"] = Axes(-0.1F, -0.5F);
    hinge["limits_upper"] = Axes(0.0F, 0.5F);
    const auto text = Sidecar().AppendBinding("joints", hinge).Text();

    ExpectRefusal(Prepare(text),
      import_codes::kPhysicsSidecarJointMemberNotApplicable,
      "/bindings/joints/0/limits_lower/0");
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesARotatedPointFrame)
  {
    auto point = Joint("point");
    point["local_frame_b_rotation"] = { 0.0F, 0.0F, 1.0F, 0.0F };
    const auto text = Sidecar().AppendBinding("joints", point).Text();

    ExpectRefusal(Prepare(text),
      import_codes::kPhysicsSidecarJointMemberNotApplicable,
      "/bindings/joints/0/local_frame_b_rotation");
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesAxisModesOffSixDof)
  {
    auto hinge = Joint("hinge");
    hinge["axis_modes"]
      = json::array({ "free", "free", "free", "free", "free", "free" });
    const auto text = Sidecar().AppendBinding("joints", hinge).Text();

    ExpectRefusal(Prepare(text),
      import_codes::kPhysicsSidecarJointMemberNotApplicable,
      "/bindings/joints/0/axis_modes");
  }

  NOLINT_TEST(PhysicsSidecarPreparationTest, RefusesANonUnitFrameRotation)
  {
    auto hinge = Joint("hinge");
    hinge["local_frame_a_rotation"] = { 0.0F, 0.0F, 0.0F, 2.0F };
    const auto text = Sidecar().AppendBinding("joints", hinge).Text();

    ExpectRefusal(Prepare(text), import_codes::kPhysicsSidecarValueOutOfRange,
      "/bindings/joints/0/local_frame_a_rotation");
  }

  //! Each joint type's limit domain: a hinge angle lies in [-pi, 0] and
  //! [0, pi], a slider straddles 0, a distance is not negative, and a cone is
  //! symmetric.
  class JointLimitTest : public ::testing::TestWithParam<
                           std::tuple<std::string, json, json, std::string>> {
  };

  NOLINT_TEST_P(JointLimitTest, IsRefusedOutsideItsDomain)
  {
    const auto& [type, lower, upper, pointer] = GetParam();
    auto joint = Joint(type);
    joint["limits_lower"] = lower;
    joint["limits_upper"] = upper;
    const auto text = Sidecar().AppendBinding("joints", joint).Text();

    ExpectRefusal(Prepare(text), import_codes::kPhysicsSidecarValueOutOfRange,
      "/bindings/joints/0" + pointer);
  }

  INSTANTIATE_TEST_SUITE_P(PhysicsSidecarPreparationTest, JointLimitTest,
    ::testing::Values(std::tuple { std::string("hinge"), Axes(0.0F, -0.5F),
                        Axes(0.0F, 4.0F), std::string("/limits_upper/3") },
      std::tuple { std::string("slider"), Axes(0.2F, 0.0F), Axes(1.0F, 0.0F),
        std::string("/limits_lower/0") },
      std::tuple { std::string("distance"), Axes(-1.0F, 0.0F), Axes(1.0F, 0.0F),
        std::string("/limits_lower/0") },
      std::tuple { std::string("cone"),
        json::array({ 0.0F, 0.0F, 0.0F, 0.0F, -0.5F, -0.3F }),
        json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.5F, 0.3F }),
        std::string("/limits_upper") }));

} // namespace
} // namespace oxygen::content::import::test
