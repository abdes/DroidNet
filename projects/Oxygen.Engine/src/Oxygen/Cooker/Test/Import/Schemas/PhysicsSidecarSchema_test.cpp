//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.physics-sidecar.schema.json

#include <string>

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
  static const auto schema
    = LoadSchema("Import/Schemas/oxygen.physics-sidecar.schema.json");
  return schema;
}

class PhysicsSidecarSchemaCaseTest
  : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(PhysicsSidecarSchemaCaseTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, PhysicsSidecarSchemaCaseTest,
  ::testing::Values(
    SchemaCase {
      "PhysicsSidecarSchemaAcceptsCanonicalDocument",
      R"({
    "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.physics-sidecar.schema.json",
    "bindings": {
      "rigid_bodies": [
        {
          "node_index": 0,
          "shape_ref": "/.cooked/Physics/Shapes/test_shape.ocshape",
          "material_ref": "/.cooked/Physics/Materials/default.opmat",
          "body_type": "dynamic",
          "motion_quality": "discrete",
          "center_of_mass_override": [0.0, 0.1, 0.0],
          "inertia_tensor_override": [1.0, 1.1, 1.2],
          "max_linear_velocity": 20.0,
          "max_angular_velocity": 10.0,
          "allowed_dof": {
            "translate_x": true,
            "translate_y": false,
            "translate_z": true,
            "rotate_x": true,
            "rotate_y": false,
            "rotate_z": true
          },
          "backend": {
            "target": "physx",
            "min_velocity_iters": 4,
            "min_position_iters": 1,
            "max_contact_impulse": 1000.0,
            "contact_report_threshold": 3.5
          }
        }
      ],
      "colliders": [
        {
          "node_index": 1,
          "shape_ref": "/.cooked/Physics/Shapes/test_shape.ocshape",
          "material_ref": "/.cooked/Physics/Materials/default.opmat",
          "is_sensor": true
        }
      ],
      "characters": [
        {
          "node_index": 2,
          "shape_ref": "/.cooked/Physics/Shapes/test_shape.ocshape",
          "step_down_distance": 0.2,
          "skin_width": 0.02,
          "predictive_contact_distance": 0.04,
          "inner_shape_ref": "/.cooked/Physics/Shapes/test_inner.ocshape",
          "backend": {
            "target": "jolt",
            "penetration_recovery_speed": 0.5,
            "max_num_hits": 16,
            "hit_reduction_cos_max_angle": 0.9
          }
        }
      ],
      "soft_bodies": [
        {
          "node_index": 3,
          "source_mesh_ref": "/.cooked/Geometry/softbody_sphere.ogeo",
          "collision_layer": 2,
          "collision_mask": 65535,
          "collision_mesh_ref": "/.cooked/Geometry/softbody_collision.ogeo",
          "edge_compliance": 0.0001,
          "shear_compliance": 0.0001,
          "bend_compliance": 0.0002,
          "volume_compliance": 0.0002,
          "pressure_coefficient": 0.0,
          "tether_mode": "geodesic",
          "tether_max_distance_multiplier": 1.05,
          "global_damping": 0.08,
          "restitution": 0.2,
          "friction": 0.6,
          "vertex_radius": 0.02,
          "solver_iteration_count": 8,
          "self_collision": false,
          "pinned_vertices": [0, 4],
          "kinematic_vertices": [1],
          "backend": {
            "target": "physx",
            "youngs_modulus": 100000.0,
            "poisson_ratio": 0.3,
            "dynamic_friction": 0.5
          }
        }
      ],
      "joints": [
        {
          "node_index_a": 0,
          "node_index_b": 1,
          "constraint_type": "six_dof",
          "constraint_space": "local",
          "local_frame_a_position": [0.0, 0.5, 0.0],
          "local_frame_a_rotation": [0.0, 0.0, 0.0, 1.0],
          "local_frame_b_position": [0.0, -0.5, 0.0],
          "local_frame_b_rotation": [0.0, 0.0, 0.0, 1.0],
          "limits_lower": [-0.1, -0.2, -0.3, -0.4, -0.5, -0.6],
          "limits_upper": [0.1, 0.2, 0.3, 0.4, 0.5, 0.6],
          "spring_stiffnesses": [10.0, 10.0, 10.0, 5.0, 5.0, 5.0],
          "spring_damping_ratios": [0.7, 0.7, 0.7, 0.6, 0.6, 0.6],
          "motor_modes": ["off", "velocity", "position", "off", "off", "off"],
          "motor_target_velocities": [0.0, 1.0, 2.0, 0.0, 0.0, 0.0],
          "motor_target_positions": [0.0, 0.1, 0.2, 0.0, 0.0, 0.0],
          "motor_max_forces": [20.0, 20.0, 20.0, 10.0, 10.0, 10.0],
          "motor_max_torques": [5.0, 5.0, 5.0, 2.5, 2.5, 2.5],
          "motor_drive_frequencies": [3.0, 3.0, 3.0, 2.0, 2.0, 2.0],
          "motor_damping_ratios": [0.9, 0.9, 0.9, 0.8, 0.8, 0.8],
          "break_force": 1000.0,
          "break_torque": 250.0,
          "collide_connected": false,
          "priority": 3,
          "backend": {
            "target": "jolt",
            "num_velocity_steps_override": 4,
            "num_position_steps_override": 2
          }
        }
      ],
      "vehicles": [
        {
          "node_index": 4,
          "controller_type": "wheeled",
          "wheels": [
            {
              "node_index": 6,
              "axle_index": 0,
              "side": "left",
              "backend": {
                "target": "jolt",
                "wheel_castor": 0.01
              }
            },
            {
              "node_index": 7,
              "axle_index": 0,
              "side": "right",
              "backend": {
                "target": "physx"
              }
            }
          ]
        }
      ],
      "aggregates": [
        {
          "node_index": 5,
          "max_bodies": 32
        }
      ]
    }
  })",
      true,
      "",
    },
    SchemaCase {
      "PhysicsSidecarSchemaRejectsUnknownFields",
      R"({
    "bindings": {
      "rigid_bodies": [
        {
          "node_index": 0,
          "shape_ref": "/.cooked/Physics/Shapes/test_shape.ocshape",
          "material_ref": "/.cooked/Physics/Materials/default.opmat",
          "unknown_field": true
        }
      ]
    }
  })",
      false,
      "'unknown_field'",
    },
    SchemaCase {
      "PhysicsSidecarSchemaAcceptsJointWorldAttachmentToken",
      R"({
    "bindings": {
      "joints": [
        {
          "node_index_a": 2,
          "node_index_b": "world"
        }
      ]
    }
  })",
      true,
      "",
    },
    SchemaCase {
      "PhysicsSidecarSchemaRejectsJointWorldAttachmentNull",
      R"({
    "bindings": {
      "joints": [
        {
          "node_index_a": 0,
          "node_index_b": null
        }
      ]
    }
  })",
      false,
      "/bindings/joints/0/node_index_b: ",
    },
    SchemaCase {
      "PhysicsSidecarSchemaRejectsMissingRequiredShapeReference",
      R"({
    "bindings": {
      "rigid_bodies": [
        {
          "node_index": 0,
          "material_ref": "/.cooked/Physics/Materials/default.opmat"
        }
      ]
    }
  })",
      false,
      "'shape_ref'",
    },
    SchemaCase {
      "PhysicsSidecarSchemaRejectsSoftBodyMissingCollisionFilterFields",
      R"({
    "bindings": {
      "soft_bodies": [
        {
          "node_index": 3,
          "source_mesh_ref": "/.cooked/Geometry/softbody_sphere.ogeo",
          "edge_compliance": 0.0001,
          "shear_compliance": 0.0001,
          "bend_compliance": 0.0002,
          "volume_compliance": 0.0002,
          "pressure_coefficient": 0.0,
          "tether_mode": "geodesic",
          "tether_max_distance_multiplier": 1.05,
          "global_damping": 0.08,
          "restitution": 0.2,
          "friction": 0.6,
          "vertex_radius": 0.02,
          "solver_iteration_count": 8,
          "self_collision": false,
          "pinned_vertices": [],
          "kinematic_vertices": [],
          "backend": {
            "target": "jolt"
          }
        }
      ]
    }
  })",
      false,
      "'collision_layer'",
    }),
  oxygen::cooker::test::SchemaCaseName);

NOLINT_TEST(PhysicsSidecarSchemaTest, RejectsLegacyFieldNames)
{
  const auto doc = json::parse(R"({
    "bindings": {
      "rigid_bodies": [
        {
          "node_index": 0,
          "shape_virtual_path": "/.cooked/Physics/Shapes/test_shape.ocshape",
          "material_virtual_path": "/.cooked/Physics/Materials/default.opmat"
        }
      ],
      "joints": [
        {
          "node_index_a": 0,
          "node_index_b": 1,
          "spring_stiffness": 10.0,
          "spring_damping_ratio": 0.5,
          "motor_mode": "velocity",
          "motor_target_velocity": 1.0,
          "motor_target_position": 0.0,
          "motor_max_force": 25.0,
          "motor_max_torque": 5.0,
          "motor_drive_frequency": 4.0,
          "motor_damping_ratio": 0.9
        }
      ],
      "soft_bodies": [
        {
          "node_index": 3,
          "jolt_settings_ref": "/.cooked/Physics/Resources/soft_body_settings_jolt.opres",
          "cluster_count": 8,
          "stiffness": 4.0,
          "damping": 0.08
        }
      ]
    }
  })");

  const auto errors = ValidateJson(Schema(), doc);
  for (const auto* legacy_name : {
         "'shape_virtual_path'",
         "'material_virtual_path'",
         "'spring_stiffness'",
         "'spring_damping_ratio'",
         "'motor_mode'",
         "'motor_target_velocity'",
         "'motor_target_position'",
         "'motor_max_force'",
         "'motor_max_torque'",
         "'motor_drive_frequency'",
         "'motor_damping_ratio'",
         "'jolt_settings_ref'",
         "'cluster_count'",
         "'stiffness'",
         "'damping'",
       }) {
    EXPECT_THAT(errors, Contains(HasSubstr(legacy_name)));
  }
}

} // namespace
