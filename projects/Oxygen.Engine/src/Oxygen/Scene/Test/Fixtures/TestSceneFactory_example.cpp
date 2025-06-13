//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Testing/GTest.h>

#include "./TestSceneFactory.h"
#include <Oxygen/Scene/SceneNode.h>

using oxygen::scene::SceneNode;
using oxygen::scene::testing::PositionalNameGenerator;
using oxygen::scene::testing::TestSceneFactory;

namespace {

//! Example test fixture showing basic TestSceneFactory usage.
class TestSceneFactoryExample : public ::testing::Test {
protected:
  void SetUp() override
  {
    // Reset factory to clean state and configure for our tests
    TestSceneFactory::Instance()
      .Reset()
      .SetDefaultCapacity(128)
      .GetNameGenerator()
      .SetPrefix("Test");
  }

  void TearDown() override
  {
    // Clean up after each test
    TestSceneFactory::Instance().Reset();
  }
};

//=== Basic Scene Creation Tests ===

NOLINT_TEST_F(TestSceneFactoryExample, CreateSingleNode)
{
  auto scene = TestSceneFactory::Instance().CreateSingleNodeScene("SingleTest");

  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->GetName(), "SingleTest");
  EXPECT_EQ(scene->GetNodeCount(), 1);
  EXPECT_FALSE(scene->IsEmpty());

  // Check the root node exists
  auto roots = scene->GetRootNodes();
  ASSERT_EQ(roots.size(), 1);
  EXPECT_TRUE(roots[0].IsValid());
  EXPECT_TRUE(roots[0].IsRoot());
}

NOLINT_TEST_F(TestSceneFactoryExample, CreateParentChild)
{
  auto scene
    = TestSceneFactory::Instance().CreateParentChildScene("ParentChildTest");

  EXPECT_EQ(scene->GetNodeCount(), 2);

  auto roots = scene->GetRootNodes();
  ASSERT_EQ(roots.size(), 1);

  auto parent = roots[0];
  EXPECT_TRUE(parent.HasChildren());
  EXPECT_FALSE(parent.HasParent());

  auto first_child = parent.GetFirstChild();
  ASSERT_TRUE(first_child.has_value());
  EXPECT_TRUE(first_child->HasParent());
  EXPECT_FALSE(first_child->HasChildren());
}

NOLINT_TEST_F(TestSceneFactoryExample, CreateLinearChain)
{
  auto scene
    = TestSceneFactory::Instance().CreateLinearChainScene("ChainTest", 4);

  EXPECT_EQ(scene->GetNodeCount(), 4);

  auto roots = scene->GetRootNodes();
  ASSERT_EQ(roots.size(), 1);

  // Walk the chain
  auto current = roots[0];
  int depth = 0;
  while (current.IsValid()) {
    ++depth;
    if (depth == 1) {
      EXPECT_FALSE(current.HasParent());
    } else {
      EXPECT_TRUE(current.HasParent());
    }

    auto child = current.GetFirstChild();
    if (child.has_value()) {
      current = *child;
    } else {
      break;
    }
  }

  EXPECT_EQ(depth, 4);
}

//=== Name Generator Tests ===

NOLINT_TEST_F(TestSceneFactoryExample, DefaultNameGenerator)
{
  auto scene = TestSceneFactory::Instance().CreateParentWithChildrenScene(
    "DefaultNaming", 3);

  // Default generator should create meaningful names
  auto roots = scene->GetRootNodes();
  ASSERT_EQ(roots.size(), 1);

  // Root should be named based on our prefix + role
  auto root_obj = roots[0].GetObject();
  ASSERT_TRUE(root_obj.has_value());
  EXPECT_TRUE(root_obj->get().GetName().find("Test") != std::string::npos);
}

NOLINT_TEST_F(TestSceneFactoryExample, PositionalNameGenerator)
{
  TestSceneFactory::Instance()
    .SetNameGenerator(std::make_unique<PositionalNameGenerator>())
    .GetNameGenerator()
    .SetPrefix("Node");

  auto scene = TestSceneFactory::Instance().CreateParentWithChildrenScene(
    "PositionalNaming", 3);

  auto roots = scene->GetRootNodes();
  ASSERT_EQ(roots.size(), 1);

  auto root_obj = roots[0].GetObject();
  ASSERT_TRUE(root_obj.has_value());
  EXPECT_EQ(root_obj->get().GetName(), "NodeFirst");
}

//=== Chainable Configuration Tests ===

NOLINT_TEST_F(TestSceneFactoryExample, ChainableConfiguration)
{
  // Test fluent interface
  auto scene = TestSceneFactory::Instance()
                 .Reset()
                 .SetDefaultCapacity(64)
                 .SetNameGenerator(std::make_unique<PositionalNameGenerator>())
                 .CreateSingleNodeScene("ChainTest");

  EXPECT_EQ(scene->GetName(), "ChainTest");
  EXPECT_EQ(scene->GetNodeCount(), 1);
}

//=== Template Tests ===

NOLINT_TEST_F(TestSceneFactoryExample, SimpleTemplate)
{
  // Register a simple template with actual scene structure
  const std::string simple_template = R"({
    "nodes": [
      {
        "name": "SimpleRoot",
        "transform": {
          "position": [0, 0, 0],
          "rotation": [0, 0, 0],
          "scale": [1, 1, 1]
        }
      }
    ]
  })";

  TestSceneFactory::Instance().RegisterTemplate("simple", simple_template);

  auto scene
    = TestSceneFactory::Instance().CreateFromTemplate("simple", "TemplateTest");

  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->GetName(), "TemplateTest");
  EXPECT_EQ(scene->GetNodeCount(), 1);

  auto roots = scene->GetRootNodes();
  ASSERT_EQ(roots.size(), 1);

  auto root_obj = roots[0].GetObject();
  ASSERT_TRUE(root_obj.has_value());
  EXPECT_EQ(root_obj->get().GetName(), "SimpleRoot");
}

NOLINT_TEST_F(TestSceneFactoryExample, ComplexHierarchyTemplate)
{
  // Create a realistic game object hierarchy
  const std::string complex_template = R"({
    "nodes": [
      {
        "name": "Player",
        "transform": {
          "position": [0, 1, 0],
          "rotation": [0, 0, 0],
          "scale": [1, 1, 1]
        },
        "flags": {
          "visible": true
        },
        "children": [
          {
            "name": "PlayerModel",
            "transform": {
              "position": [0, 0, 0],
              "rotation": [0, 0, 0],
              "scale": [1, 1, 1]
            }
          },
          {
            "name": "WeaponMount",
            "transform": {
              "position": [0.5, 0.8, 0],
              "rotation": [0, 90, 0],
              "scale": [1, 1, 1]
            },
            "children": [
              {
                "name": "Rifle",
                "transform": {
                  "position": [0, 0, 0.3],
                  "rotation": [0, 0, 0],
                  "scale": [1, 1, 1]
                }
              }
            ]
          },
          {
            "name": "Camera",
            "transform": {
              "position": [0, 1.7, 0],
              "rotation": [0, 0, 0],
              "scale": [1, 1, 1]
            }
          }
        ]
      },
      {
        "name": "Environment",
        "transform": {
          "position": [0, 0, 0],
          "rotation": [0, 0, 0],
          "scale": [1, 1, 1]
        },
        "children": [
          {
            "name": "Ground",
            "transform": {
              "position": [0, 0, 0],
              "rotation": [0, 0, 0],
              "scale": [10, 1, 10]
            }
          },
          {
            "name": "Building",
            "transform": {
              "position": [5, 0, 5],
              "rotation": [0, 45, 0],
              "scale": [2, 3, 2]
            }
          }
        ]
      }
    ]
  })";

  TestSceneFactory::Instance().RegisterTemplate("complex", complex_template);

  auto scene = TestSceneFactory::Instance().CreateFromTemplate(
    "complex", "ComplexScene");

  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->GetNodeCount(),
    8); // Player + 3 children + Rifle + Environment + 2 children

  auto roots = scene->GetRootNodes();
  EXPECT_EQ(roots.size(), 2); // Player and Environment

  // Validate Player hierarchy
  auto player_it = std::ranges::find_if(roots, [](SceneNode& node) {
    const auto obj = node.GetObject();
    return obj.has_value() && obj->get().GetName() == "Player";
  });
  ASSERT_NE(player_it, roots.end());

  auto player = *player_it;
  EXPECT_TRUE(player.HasChildren());

  // Check Player's transform
  auto player_transform = player.GetTransform();
  auto player_pos = player_transform.GetLocalPosition();
  ASSERT_TRUE(player_pos.has_value());
  EXPECT_FLOAT_EQ(player_pos->x, 0.0f);
  EXPECT_FLOAT_EQ(player_pos->y, 1.0f);
  EXPECT_FLOAT_EQ(player_pos->z, 0.0f);

  // Validate WeaponMount exists and has correct position
  auto first_child = player.GetFirstChild();
  ASSERT_TRUE(first_child.has_value());

  // Find WeaponMount among children
  bool found_weapon_mount = false;
  auto current_child = first_child;
  while (current_child.has_value()) {
    auto child_obj = current_child->GetObject();
    if (child_obj.has_value() && child_obj->get().GetName() == "WeaponMount") {
      found_weapon_mount = true;

      // Check WeaponMount transform
      auto weapon_transform = current_child->GetTransform();
      auto weapon_pos = weapon_transform.GetLocalPosition();
      ASSERT_TRUE(weapon_pos.has_value());
      EXPECT_FLOAT_EQ(weapon_pos->x, 0.5f);
      EXPECT_FLOAT_EQ(weapon_pos->y, 0.8f);
      EXPECT_FLOAT_EQ(weapon_pos->z, 0.0f);

      // Check that WeaponMount has Rifle child
      EXPECT_TRUE(current_child->HasChildren());
      break;
    }
    current_child = current_child->GetNextSibling();
  }
  EXPECT_TRUE(found_weapon_mount);
}

NOLINT_TEST_F(TestSceneFactoryExample, DirectJsonCreation)
{
  // Create scene directly from JSON without template registration
  const std::string scene_json = R"({
    "nodes": [
      {
        "name": "LightSystem",
        "transform": {
          "position": [0, 10, 0],
          "rotation": [45, 0, 0],
          "scale": [1, 1, 1]
        },
        "children": [
          {
            "name": "DirectionalLight",
            "transform": {
              "position": [0, 0, 0],
              "rotation": [0, 0, 0],
              "scale": [1, 1, 1]
            }
          },
          {
            "name": "AmbientLight",
            "transform": {
              "position": [0, 0, 0],
              "rotation": [0, 0, 0],
              "scale": [0.5, 0.5, 0.5]
            }
          }
        ]
      }
    ]
  })";

  auto scene = TestSceneFactory::Instance().CreateFromJson(
    scene_json, "DirectJsonScene");

  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->GetName(), "DirectJsonScene");
  EXPECT_EQ(scene->GetNodeCount(), 3); // LightSystem + 2 children

  auto roots = scene->GetRootNodes();
  ASSERT_EQ(roots.size(), 1);

  auto light_system = roots[0];
  auto light_obj = light_system.GetObject();
  ASSERT_TRUE(light_obj.has_value());
  EXPECT_EQ(light_obj->get().GetName(), "LightSystem");

  // Verify transform with rotation
  auto transform = light_system.GetTransform();
  auto rotation = transform.GetLocalRotation();
  ASSERT_TRUE(rotation.has_value());

  // The rotation should be approximately 45 degrees around X-axis
  // Note: glm::quat constructor from euler angles expects radians
  auto expected_quat = glm::quat(glm::radians(glm::vec3(45.0f, 0.0f, 0.0f)));
  EXPECT_NEAR(rotation->w, expected_quat.w, 1e-5f);
  EXPECT_NEAR(rotation->x, expected_quat.x, 1e-5f);
  EXPECT_NEAR(rotation->y, expected_quat.y, 1e-5f);
  EXPECT_NEAR(rotation->z, expected_quat.z, 1e-5f);
}

NOLINT_TEST_F(TestSceneFactoryExample, MixedNamingJson)
{
  // Test JSON with some named nodes and some auto-generated
  const std::string mixed_json = R"({
    "nodes": [
      {
        "name": "ExplicitRoot",
        "transform": {
          "position": [0, 0, 0],
          "scale": [2, 2, 2]
        },
        "children": [
          {
            "transform": {
              "position": [1, 0, 0]
            }
          },
          {
            "name": "ExplicitChild",
            "transform": {
              "position": [0, 1, 0]
            }
          },
          {
            "transform": {
              "position": [0, 0, 1]
            }
          }
        ]
      }
    ]
  })";

  auto scene
    = TestSceneFactory::Instance().CreateFromJson(mixed_json, "MixedScene");

  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->GetNodeCount(), 4); // Root + 3 children

  auto roots = scene->GetRootNodes();
  ASSERT_EQ(roots.size(), 1);

  auto root = roots[0];
  auto root_obj = root.GetObject();
  ASSERT_TRUE(root_obj.has_value());
  EXPECT_EQ(root_obj->get().GetName(), "ExplicitRoot");

  // Count children and check that some have generated names
  EXPECT_TRUE(root.HasChildren());

  int child_count = 0;
  bool found_explicit_child = false;
  bool found_generated_name = false;

  auto child = root.GetFirstChild();
  while (child.has_value()) {
    child_count++;
    auto child_obj = child->GetObject();
    ASSERT_TRUE(child_obj.has_value());

    const auto& name = child_obj->get().GetName();
    if (name == "ExplicitChild") {
      found_explicit_child = true;
    } else if (name.find("Test") != std::string::npos) { // Our prefix
      found_generated_name = true;
    }

    child = child->GetNextSibling();
  }

  EXPECT_EQ(child_count, 3);
  EXPECT_TRUE(found_explicit_child);
  EXPECT_TRUE(found_generated_name);
}

NOLINT_TEST_F(TestSceneFactoryExample, JsonErrorHandling)
{
  // Test malformed JSON
  NOLINT_EXPECT_THROW([[maybe_unused]] auto _
    = TestSceneFactory::Instance().CreateFromJson("invalid json", "ErrorScene"),
    std::invalid_argument);

  // Test invalid template registration
  NOLINT_EXPECT_THROW(
    TestSceneFactory::Instance().RegisterTemplate("bad", "not json"),
    std::invalid_argument);

  // Test non-object root
  NOLINT_EXPECT_THROW([[maybe_unused]] auto _
    = TestSceneFactory::Instance().CreateFromJson("[]", "ArrayScene"),
    std::invalid_argument);

  // Test template that's not an object
  NOLINT_EXPECT_THROW(
    TestSceneFactory::Instance().RegisterTemplate("array", "[]"),
    std::invalid_argument);
}

NOLINT_TEST_F(TestSceneFactoryExample, LargeSceneFromJson)
{
  // Test performance and correctness with a larger scene
  std::string large_scene = R"({
    "nodes": [
      {
        "name": "City",
        "transform": {
          "position": [0, 0, 0],
          "scale": [1, 1, 1]
        },
        "children": [)";

  // Generate buildings programmatically
  for (int i = 0; i < 10; ++i) {
    if (i > 0)
      large_scene += ",";
    large_scene += fmt::format(R"(
          {{
            "name": "Building{}",
            "transform": {{
              "position": [{}, 0, {}],
              "scale": [1, {}, 1]
            }},
            "children": [
              {{
                "name": "Building{}_Door",
                "transform": {{
                  "position": [0.5, 0, 0]
                }}
              }},
              {{
                "name": "Building{}_Roof",
                "transform": {{
                  "position": [0, {}, 0]
                }}
              }}
            ]
          }})",
      i, i * 2.0f, i * 3.0f, 2.0f + i * 0.5f, i, i, 2.0f + i * 0.5f);
  }

  large_scene += R"(
        ]
      }
    ]
  })";

  auto scene
    = TestSceneFactory::Instance().CreateFromJson(large_scene, "CityScene");

  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->GetNodeCount(), 31); // City + 10 buildings + 20 sub-objects

  auto roots = scene->GetRootNodes();
  ASSERT_EQ(roots.size(), 1);

  auto city = roots[0];
  auto city_obj = city.GetObject();
  ASSERT_TRUE(city_obj.has_value());
  EXPECT_EQ(city_obj->get().GetName(), "City");

  // Verify first building exists and has correct structure
  // NOTE: Due to scene graph implementation, children are added in reverse
  // order So the "first child" is actually the last building created
  // (Building9)
  auto first_building = city.GetFirstChild();
  ASSERT_TRUE(first_building.has_value());

  auto building_obj = first_building->GetObject();
  ASSERT_TRUE(building_obj.has_value());
  EXPECT_EQ(building_obj->get().GetName(),
    "Building9"); // Last created building becomes first child

  // Each building should have 2 children (door and roof)
  EXPECT_TRUE(first_building->HasChildren());

  int building_child_count = 0;
  auto building_child = first_building->GetFirstChild();
  while (building_child.has_value()) {
    building_child_count++;
    building_child = building_child->GetNextSibling();
  }
  EXPECT_EQ(building_child_count, 2);

  // Additional verification: ensure all 10 buildings are present
  int total_buildings = 0;
  auto current_building = city.GetFirstChild();
  std::vector<std::string> building_names;

  while (current_building.has_value()) {
    auto current_building_obj
      = current_building->GetObject(); // Renamed to avoid shadowing
    ASSERT_TRUE(current_building_obj.has_value());

    const auto& name = current_building_obj->get().GetName();
    if (name.starts_with("Building")) {
      total_buildings++;
      building_names.emplace_back(name); // Convert string_view to string
    }

    current_building = current_building->GetNextSibling();
  }

  EXPECT_EQ(total_buildings, 10) << "Should have exactly 10 buildings";

  // Verify all expected building names are present (in reverse order)
  for (int i = 0; i < 10; ++i) {
    const auto expected_name = fmt::format("Building{}", i);
    EXPECT_TRUE(
      std::find(building_names.begin(), building_names.end(), expected_name)
      != building_names.end())
      << "Missing building: " << expected_name;
  }
}

} // namespace
