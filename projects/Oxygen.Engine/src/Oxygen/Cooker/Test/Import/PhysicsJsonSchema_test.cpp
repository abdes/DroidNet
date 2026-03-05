//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using nlohmann::json_schema::error_handler;
using nlohmann::json_schema::json_validator;

class CollectingErrorHandler final : public error_handler {
public:
  void error(const json::json_pointer& ptr, const json& instance,
    const std::string& message) override
  {
    auto out = std::ostringstream {};
    const auto path = ptr.to_string();
    out << (path.empty() ? "<root>" : path) << ": " << message;
    if (!instance.is_discarded()) {
      out << " (value=" << instance.dump() << ")";
    }
    errors_.push_back(out.str());
  }

  [[nodiscard]] auto HasErrors() const noexcept -> bool
  {
    return !errors_.empty();
  }

  [[nodiscard]] auto ToString() const -> std::string
  {
    auto out = std::ostringstream {};
    for (const auto& error : errors_) {
      out << "- " << error << "\n";
    }
    return out.str();
  }

private:
  std::vector<std::string> errors_;
};

auto FindRepoRoot() -> std::filesystem::path
{
  auto path = std::filesystem::path(__FILE__).parent_path();
  while (!path.empty()) {
    if (std::filesystem::exists(path / "src" / "Oxygen" / "Cooker" / "Import"
          / "Schemas" / "oxygen.physics-sidecar.schema.json")) {
      return path;
    }
    path = path.parent_path();
  }
  return {};
}

auto SchemaFile(const std::filesystem::path& repo_root,
  const std::string_view file_name) -> std::filesystem::path
{
  return repo_root / "src" / "Oxygen" / "Cooker" / "Import" / "Schemas"
    / std::string(file_name);
}

auto LoadJsonFile(const std::filesystem::path& path) -> std::optional<json>
{
  auto in = std::ifstream(path);
  if (!in) {
    return std::nullopt;
  }
  try {
    auto parsed = json {};
    in >> parsed;
    return parsed;
  } catch (...) {
    return std::nullopt;
  }
}

auto ValidateSchema(
  const json& schema, const json& instance, std::string& errors) -> bool
{
  try {
    auto validator = json_validator {};
    validator.set_root_schema(schema);
    auto handler = CollectingErrorHandler {};
    [[maybe_unused]] auto _ = validator.validate(instance, handler);
    if (handler.HasErrors()) {
      errors = handler.ToString();
      return false;
    }
    return true;
  } catch (const std::exception& ex) {
    errors = ex.what();
    return false;
  }
}

NOLINT_TEST(PhysicsJsonSchemaTest, PhysicsSidecarSchemaAcceptsCanonicalDocument)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());

  const auto schema
    = LoadJsonFile(SchemaFile(repo_root, "oxygen.physics-sidecar.schema.json"));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
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
          "material_ref": "/.cooked/Physics/Materials/default.opmat"
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
          "constraint_ref": "/.cooked/Physics/Resources/joint_5.opres",
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
          "constraint_ref": "/.cooked/Physics/Resources/vehicle_chassis.opres",
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
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

NOLINT_TEST(PhysicsJsonSchemaTest, PhysicsSidecarSchemaRejectsUnknownFields)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());

  const auto schema
    = LoadJsonFile(SchemaFile(repo_root, "oxygen.physics-sidecar.schema.json"));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
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
  })");

  auto errors = std::string {};
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

NOLINT_TEST(
  PhysicsJsonSchemaTest, PhysicsSidecarSchemaAcceptsJointWorldAttachmentForms)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());

  const auto schema
    = LoadJsonFile(SchemaFile(repo_root, "oxygen.physics-sidecar.schema.json"));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "bindings": {
      "joints": [
        {
          "node_index_a": 0,
          "node_index_b": null,
          "constraint_ref": "/.cooked/Physics/Resources/joint_world_a.opres"
        },
        {
          "node_index_a": 2,
          "node_index_b": "world",
          "constraint_ref": "/.cooked/Physics/Resources/joint_world_b.opres"
        }
      ]
    }
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

NOLINT_TEST(PhysicsJsonSchemaTest,
  PhysicsSidecarSchemaRejectsMissingRequiredShapeReference)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());

  const auto schema
    = LoadJsonFile(SchemaFile(repo_root, "oxygen.physics-sidecar.schema.json"));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "bindings": {
      "rigid_bodies": [
        {
          "node_index": 0,
          "material_ref": "/.cooked/Physics/Materials/default.opmat"
        }
      ]
    }
  })");

  auto errors = std::string {};
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

NOLINT_TEST(PhysicsJsonSchemaTest, PhysicsSidecarSchemaRejectsLegacyFieldNames)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());

  const auto schema
    = LoadJsonFile(SchemaFile(repo_root, "oxygen.physics-sidecar.schema.json"));
  ASSERT_TRUE(schema.has_value());

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
          "constraint_resource_index": 1
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

  auto errors = std::string {};
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

} // namespace
