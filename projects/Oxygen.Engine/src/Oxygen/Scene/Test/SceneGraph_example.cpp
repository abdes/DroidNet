//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <thread>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

// When not building with shared libraries / DLLs, make sure to link the main
// executable (statically) with the type registry initialization library.
namespace oxygen {
class TypeRegistry;
} // namespace oxygen
extern "C" auto InitializeTypeRegistry() -> oxygen::TypeRegistry*;
namespace {
[[maybe_unused]] const auto* const kTsRegistryUnused = InitializeTypeRegistry();
} // namespace

namespace {

//=============================================================================
// Demo State Management
//=============================================================================

// Central state for the entire demo - holds shared resources and configuration
struct DemoState {
  // Shared scenes
  std::shared_ptr<Scene> main_scene;
  std::shared_ptr<Scene> animation_scene;

  // Main scene nodes (optional since SceneNode has no default constructor)
  std::optional<SceneNode> root;
  std::optional<SceneNode> child1;
  std::optional<SceneNode> child2;
  std::optional<SceneNode> grandchild;

  // Transform demo nodes
  std::optional<SceneNode> transform_root;
  std::optional<SceneNode> orbit_node;
  std::optional<SceneNode> scaling_node;

  // Animation nodes
  std::vector<SceneNode> animated_nodes;

  // Demo configuration
  struct AnimationConfig {
    float duration = 3.0f;
    float time_step = 0.5f;

    struct OrbitalParams {
      float radius = 5.0f;
      float speed = 1.0f;
      glm::vec3 center { 0.0f, 0.0f, 0.0f };
    } orbital;

    struct PendulumParams {
      float amplitude = 45.0f;
      float period = 3.0f;
    } pendulum;

    struct PulsingParams {
      float base_scale = 1.0f;
      float pulse_amplitude = 0.5f;
      float pulse_frequency = 2.0f;
    } pulsing;
  } anim_config;

  explicit DemoState()
    : main_scene(std::make_shared<Scene>("TransformExampleScene"))
    , animation_scene(std::make_shared<Scene>("AnimationScene"))
  // SceneNodes will be initialized in setup functions
  {
  }
};

//=============================================================================
// Utility Functions
//=============================================================================

// Helper to create a quaternion from Euler angles (degrees)
auto CreateRotationFromEuler(
  const float pitch, const float yaw, const float roll) -> glm::quat
{
  return { glm::vec3(
    glm::radians(pitch), glm::radians(yaw), glm::radians(roll)) };
}

// Helper to create a LookAt quaternion manually
auto CreateLookAtRotation(const glm::vec3& from, const glm::vec3& to,
  const glm::vec3& up = glm::vec3(0, 1, 0)) -> glm::quat
{
  const auto forward = glm::normalize(to - from);
  const auto right = glm::normalize(glm::cross(forward, up));
  const auto actual_up = glm::cross(right, forward);

  // Create rotation matrix (note: -forward because we use -Z as forward in
  // some systems)
  glm::mat4 look_matrix(1.0f);
  look_matrix[0] = glm::vec4(right, 0);
  look_matrix[1] = glm::vec4(actual_up, 0);
  look_matrix[2] = glm::vec4(-forward, 0);

  return glm::quat_cast(look_matrix);
}

// Helper to format vector values for output
auto FormatVec3(const glm::vec3& vec) -> std::string
{
  return std::format("({:.2f}, {:.2f}, {:.2f})", vec.x, vec.y, vec.z);
}

// Helper to print transform information (called after Scene::Update())
void PrintTransformInfo(
  const SceneNode& node, const std::string& label, const bool show_world = true)
{
  if (!node.IsValid()) {
    std::cout << "  " << label << ": [INVALID NODE]\n";
    return;
  }

  // All nodes created via Scene have TransformComponent - it's guaranteed
  const auto transform = node.GetTransform();

  std::cout << "  " << label << " (Local): ";
  if (const auto local_pos = transform.GetLocalPosition()) {
    std::cout << FormatVec3(*local_pos);
  } else {
    std::cout << "[no local position]";
  }

  if (show_world) {
    std::cout << " -> World: ";
    if (const auto world_pos = transform.GetWorldPosition()) {
      std::cout << FormatVec3(*world_pos);
    } else {
      std::cout << "[no world position]";
    }
  }
  std::cout << "\n";
}

// Helper to print a node's name and visibility, with ASCII tree structure
void PrintNodeInfo(const SceneNode& node, const std::string& prefix,
  const bool is_last, const bool is_root = false)
{
  std::cout << prefix;
  if (!is_root) {
    std::cout << (is_last ? "\\-- " : "|-- ");
  }
  const auto obj_opt = node.GetObject();
  if (obj_opt) {
    const auto& obj = obj_opt->get();
    std::cout << obj.GetName();
    std::cout << " [visible="
              << (obj.GetFlags().GetEffectiveValue(SceneNodeFlags::kVisible)
                     ? "true"
                     : "false")
              << "]\n";
  } else {
    std::cout << "<invalid node>\n";
  }
}

// Recursive tree printer with ASCII tree drawing
void PrintTree(const SceneNode& node, const std::string& prefix = "",
  const bool is_last = true, const bool is_root = true)
{
  PrintNodeInfo(node, prefix, is_last, is_root);

  // Collect children
  std::vector<SceneNode> children;
  auto child = node.GetFirstChild();
  while (child) {
    children.push_back(*child);
    child = child->GetNextSibling();
  }

  for (size_t i = 0; i < children.size(); ++i) {
    const bool last = i == children.size() - 1;
    std::string child_prefix = prefix;
    if (!is_root) {
      child_prefix += is_last ? "    " : "|   ";
    }
    PrintTree(children[i], child_prefix, last, false);
  }
}

// Helper for fatal error reporting
[[noreturn]] void PrintErrorAndExit(const std::string& msg)
{
  std::cerr << "[ERROR] " << msg << '\n';
  std::cerr.flush();
  std::cout.flush();
  std::quick_exit(EXIT_FAILURE);
}

// Helper for subsection dividers
void PrintSubSection(const std::string& title)
{
  std::cout << "\n-- " << title << " --\n";
}

// Helper for aligned status checks
void PrintStatus(const std::string& label, const std::string& value,
  const std::string& note = "")
{
  std::cout << "  - " << std::left << std::setw(28) << label << ": "
            << std::setw(6) << value << (note.empty() ? "" : "  (" + note + ")")
            << "\n";
}

//=============================================================================
// Demo Part 1: Basic Scene Graph Operations
//=============================================================================

void RunBasicSceneDemo(DemoState& state)
{
  PrintSubSection("Basic Scene Creation");
  std::cout << "  * Scene:         'TransformExampleScene'\n";

  state.root = state.main_scene->CreateNode("Root");
  std::cout << "  * Root node:     'Root'\n";

  // Safe optional handling
  const auto child1_opt
    = state.main_scene->CreateChildNode(*state.root, "Child1");
  const auto child2_opt
    = state.main_scene->CreateChildNode(*state.root, "Child2");
  if (!child1_opt || !child2_opt) {
    PrintErrorAndExit("Failed to create child nodes");
  }

  state.child1 = *child1_opt;
  state.child2 = *child2_opt;
  std::cout << "  * Children:      'Child1', 'Child2'\n";

  // Safe grandchild creation
  const auto grandchild_opt
    = state.main_scene->CreateChildNode(*state.child1, "Grandchild");
  if (!grandchild_opt) {
    PrintErrorAndExit("Failed to create Grandchild");
  }
  state.grandchild = *grandchild_opt;
  std::cout << "  * Grandchild:    'Grandchild' (under 'Child1')\n";

  // Safe object access
  const auto child2_obj = state.child2->GetObject();
  if (child2_obj) {
    child2_obj->get().SetName("SecondChild");
    std::cout << "  * Renamed:       'Child2' -> 'SecondChild'\n";
  } else {
    PrintErrorAndExit("Failed to get object for Child2");
  }

  // Safe flag access
  const auto child1_obj_opt = state.child1->GetObject();
  if (!child1_obj_opt) {
    PrintErrorAndExit("Failed to get object for Child1");
  }

  auto& child1_flags = child1_obj_opt->get().GetFlags();
  child1_flags.SetLocalValue(SceneNodeFlags::kVisible, false);
  std::cout << "  * Set 'Child1' visibility: false\n";

  PrintSubSection("Node Status Checks");
  PrintStatus("Is 'grandchild' valid?",
    state.grandchild->IsValid() ? "yes" : "no",
    state.grandchild->IsValid() ? "ok" : "error");
  PrintStatus("Is 'root' a root node?", state.root->IsRoot() ? "yes" : "no",
    state.root->IsRoot() ? "ok" : "error");
  PrintStatus("Is 'Child1' a root node?",
    !state.child1->IsRoot() ? "no" : "yes",
    !state.child1->IsRoot() ? "ok" : "error");
  PrintStatus("Is 'Child1' visible?",
    child1_flags.GetEffectiveValue(SceneNodeFlags::kVisible) ? "yes" : "no",
    child1_flags.GetEffectiveValue(SceneNodeFlags::kVisible) ? "error" : "ok");

  PrintSubSection("Scene Hierarchy");
  PrintTree(*state.root);
}

//=============================================================================
// Demo Part 2: Transform API Demonstration
//=============================================================================

// Creates a node with transform component (guaranteed by Scene)
auto CreateTransformNode(Scene& scene, const std::string& name,
  const SceneNode* parent = nullptr) -> SceneNode
{
  auto node = parent != nullptr ? scene.CreateChildNode(*parent, name).value()
                                : scene.CreateNode(name);

  // All nodes created via Scene have TransformComponent automatically
  // Just initialize with identity transform
  auto transform = node.GetTransform();
  transform.SetLocalTransform(glm::vec3 { 0.0f, 0.0f, 0.0f }, // position
    glm::quat { 1.0f, 0.0f, 0.0f, 0.0f }, // rotation (identity)
    glm::vec3 { 1.0f, 1.0f, 1.0f } // scale
  );

  return node;
}

void RunTransformDemo(DemoState& state)
{
  PrintSubSection("High-Level Transform API Demo");

  // Create nodes with transforms (TransformComponent guaranteed by Scene)
  state.transform_root
    = CreateTransformNode(*state.main_scene, "TransformRoot");
  state.orbit_node = CreateTransformNode(
    *state.main_scene, "OrbitDemo", &*state.transform_root);
  state.scaling_node
    = CreateTransformNode(*state.main_scene, "ScaleDemo", &*state.orbit_node);

  std::cout << "  * Created transform hierarchy: TransformRoot -> OrbitDemo "
               "-> ScaleDemo\n";

  // FRAME CYCLE DEMO: Prepare data phase
  std::cout
    << "  * STEP 1: Preparing transform data (setting local values)...\n";
  auto root_transform = state.transform_root->GetTransform();
  root_transform.SetLocalPosition({ 0.0f, 5.0f, 0.0f });
  std::cout << "    - Set TransformRoot position to (0, 5, 0)\n";

  auto orbit_transform = state.orbit_node->GetTransform();
  orbit_transform.SetLocalPosition({ 3.0f, 0.0f, 0.0f });
  orbit_transform.SetLocalRotation(CreateRotationFromEuler(0.0f, 45.0f, 0.0f));
  std::cout
    << "    - Set OrbitDemo position to (3, 0, 0) and rotation to 45° Y\n";

  auto scale_transform = state.scaling_node->GetTransform();
  scale_transform.SetLocalScale({ 2.0f, 0.5f, 2.0f });
  std::cout << "    - Set ScaleDemo scale to (2, 0.5, 2)\n";

  // FRAME CYCLE DEMO: Scene update phase
  std::cout << "  * STEP 2: Scene::Update() - propagating transforms through "
               "hierarchy...\n";
  state.main_scene->Update();
  std::cout << "    - Transform hierarchy updated\n";

  // FRAME CYCLE DEMO: Process/display phase
  std::cout << "  * STEP 3: Processing results (world transforms now valid)\n";
  PrintSubSection("Transform Values After Scene Update");
  PrintTransformInfo(*state.transform_root, "TransformRoot");
  PrintTransformInfo(*state.orbit_node, "OrbitDemo");
  PrintTransformInfo(*state.scaling_node, "ScaleDemo");

  // Demonstrate transform operations (next frame cycle)
  PrintSubSection("Transform Operations Demo (Next Frame)");

  std::cout << "  * STEP 1: Applying transform operations (preparing next "
               "frame)...\n";
  orbit_transform.Translate(
    { 1.0f, 0.0f, 1.0f }, true); // Local space translation
  std::cout << "    - Translated OrbitDemo by (1, 0, 1) in local space\n";

  scale_transform.Rotate(
    CreateRotationFromEuler(0.0f, 0.0f, 30.0f), true); // Local rotation
  std::cout << "    - Rotated ScaleDemo by 30 degrees around Z axis in local "
               "space\n";

  scale_transform.Scale({ 1.5f, 1.0f, 1.5f }); // Scale multiplication
  std::cout << "    - Scaled ScaleDemo by factor (1.5, 1.0, 1.5)\n";

  std::cout << "  * STEP 2: Scene::Update() - propagating changes...\n";
  state.main_scene->Update();
  std::cout << "    - Transform changes propagated\n";

  std::cout << "  * STEP 3: Processing updated results\n";
  PrintSubSection("Transform Values After Operations");

  // First verify that the transforms worked by showing local values only
  std::cout << "  Local values after operations:\n";
  PrintTransformInfo(
    *state.orbit_node, "OrbitDemo (after translate)", false); // Local only
  PrintTransformInfo(*state.scaling_node, "ScaleDemo (after rotate & scale)",
    false); // Local only

  // Now access world positions - this should work since we called
  // Scene::Update()
  std::cout << "  World values after operations:\n";
  PrintTransformInfo(*state.orbit_node, "OrbitDemo (after translate)",
    true); // Now show world too
  PrintTransformInfo(*state.scaling_node, "ScaleDemo (after rotate & scale)",
    true); // Now show world too
}

//=============================================================================
// Demo Part 3: Transform Animation Simulation
//=============================================================================

// Animation update functions
void UpdateOrbitalTransform(SceneNode& node, const float time,
  const DemoState::AnimationConfig::OrbitalParams& params)
{
  auto transform = node.GetTransform();

  const float angle = time * params.speed;
  const glm::vec3 offset { params.radius * std::cos(angle), 0.0f,
    params.radius * std::sin(angle) };

  const glm::vec3 position = params.center + offset;
  transform.SetLocalPosition(position);

  const auto look_rotation = CreateLookAtRotation(position, params.center);
  transform.SetLocalRotation(look_rotation);
}

void UpdatePendulumTransform(SceneNode& node, const float time,
  const DemoState::AnimationConfig::PendulumParams& params)
{
  auto transform = node.GetTransform();

  const float swing_angle = params.amplitude
    * std::sin(2.0f * std::numbers::pi_v<float> * time / params.period);
  const auto rotation = CreateRotationFromEuler(0.0f, 0.0f, swing_angle);

  transform.SetLocalRotation(rotation);
}

void UpdatePulsingScale(SceneNode& node, const float time,
  const DemoState::AnimationConfig::PulsingParams& params)
{
  auto transform = node.GetTransform();

  const float scale_factor = params.base_scale
    + params.pulse_amplitude
      * std::sin(
        2.0f * std::numbers::pi_v<float> * params.pulse_frequency * time);
  const glm::vec3 scale { scale_factor, scale_factor, scale_factor };

  transform.SetLocalScale(scale);
}

// Sets up a hierarchical scene with animated transforms
void SetupAnimatedScene(DemoState& state)
{
  // Create root node
  auto root = CreateTransformNode(*state.animation_scene, "AnimationRoot");
  root.GetTransform().SetLocalPosition({ 0.0f, 0.0f, 0.0f });
  state.animated_nodes.push_back(root);

  // Create orbital parent (revolves around origin)
  const auto orbital_parent
    = CreateTransformNode(*state.animation_scene, "OrbitalParent", &root);
  state.animated_nodes.push_back(orbital_parent);

  // Create pendulum child (swings relative to orbital parent)
  auto pendulum
    = CreateTransformNode(*state.animation_scene, "Pendulum", &orbital_parent);
  pendulum.GetTransform().SetLocalPosition(
    { 3.0f, 0.0f, 0.0f }); // Offset from parent
  state.animated_nodes.push_back(pendulum);

  // Create pulsing grandchild (scales relative to pendulum)
  auto pulser
    = CreateTransformNode(*state.animation_scene, "Pulser", &pendulum);
  pulser.GetTransform().SetLocalPosition(
    { 1.0f, 2.0f, 0.0f }); // Offset from pendulum
  state.animated_nodes.push_back(pulser);
}

// Simulates one frame of animation using proper game engine pattern
void SimulateAnimationFrame(DemoState& state, const float time)
{
  // FRAME STEP 1: Prepare data - Update all local transforms (game logic)
  UpdateOrbitalTransform(
    state.animated_nodes[1], time, state.anim_config.orbital);
  UpdatePendulumTransform(
    state.animated_nodes[2], time, state.anim_config.pendulum);
  UpdatePulsingScale(state.animated_nodes[3], time, state.anim_config.pulsing);

  // FRAME STEP 2: Scene::Update() - Propagate transforms through hierarchy
  state.animation_scene->Update();

  // FRAME STEP 3: Process/Display - Now world transforms are valid and can be
  // accessed (This is where you would typically render/draw the frame)
}

void RunAnimationDemo(DemoState& state)
{
  PrintSubSection("Setting Up Animation Simulation");

  SetupAnimatedScene(state);

  std::cout << "  * Created animated scene with " << state.animated_nodes.size()
            << " nodes\n";
  std::cout << "  * Hierarchy: AnimationRoot -> OrbitalParent -> Pendulum -> "
               "Pulser\n";
  std::cout << "  * OrbitalParent: Orbits around origin\n";
  std::cout << "  * Pendulum: Swings relative to orbital parent\n";
  std::cout << "  * Pulser: Scales (pulses) relative to pendulum\n";

  std::cout << "\n=== Transform Animation Simulation ===\n";
  std::cout << "Duration: " << state.anim_config.duration
            << "s, Time step: " << state.anim_config.time_step << "s\n";
  std::cout << "Following proper game engine frame pattern:\n";
  std::cout << "  1. Prepare Data (set local transforms)\n";
  std::cout << "  2. Scene::Update() (propagate transforms)\n";
  std::cout << "  3. Process/Display (read world transforms)\n\n";

  for (float time = 0.0f; time <= state.anim_config.duration;
    time += state.anim_config.time_step) {
    std::cout << "=== FRAME at Time: " << std::fixed << std::setprecision(2)
              << time << "s ===\n";

    // Execute one complete frame cycle
    SimulateAnimationFrame(state, time);

    // Display current transform states (this is the "present" phase)
    for (const auto& node : state.animated_nodes) {
      if (const auto obj = node.GetObject()) {
        PrintTransformInfo(node, std::string(obj->get().GetName()));
      }
    }

    std::cout << "\n";

    // Add a pause for demonstration (remove for automated testing)
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  PrintSubSection("Animation Complete");
  std::cout << "  * Transform simulation finished\n";
  std::cout << "  * Demonstrated hierarchical transform inheritance\n";
  std::cout << "  * Showed position, rotation, and scale animations\n";
  std::cout << "  * Used Scene::Update() for proper dirty flag propagation\n";
}

//=============================================================================
// Demo Part 4: Cleanup and Validation
//=============================================================================

void RunCleanupDemo(DemoState& state)
{
  PrintSubSection("Parent Lookup (Original Demo)");
  std::cout << "  Parent of 'Grandchild': ";
  if (const auto parent_opt = state.grandchild->GetParent()) {
    // Safe parent access
    const auto& parent = *parent_opt;
    const auto parent_obj = parent.GetObject();
    if (parent_obj && parent_obj->get().GetName() == "Child1") {
      std::cout << parent_obj->get().GetName() << " (ok)\n";
    } else {
      std::cout << "(wrong parent)\n";
    }
  } else {
    std::cout << "(not found)\n";
  }

  PrintSubSection("Destroying 'Child1' subtree...");
  state.main_scene->DestroyNodeHierarchy(*state.child1);

  PrintSubSection("Scene hierarchy after deletion");
  PrintTree(*state.root);

  PrintSubSection("Post-Deletion Checks (Lazy Invalidation)");
  // Check validity before access (should be valid due to lazy invalidation)
  PrintStatus("Is 'Grandchild' valid?  (before access)",
    state.grandchild->IsValid() ? "yes" : "no",
    state.grandchild->IsValid() ? "ok" : "error");
  std::cout << "  - Accessing 'Grandchild' object: ";
  const auto grandchild_obj = state.grandchild->GetObject();
  if (!grandchild_obj) {
    std::cout << "object not found  (ok)\n";
  } else {
    std::cout << grandchild_obj->get().GetName() << " (error)\n";
  }
  // Check validity after access (should now be invalid)
  PrintStatus("Is 'Grandchild' valid?   (after access)",
    state.grandchild->IsValid() ? "yes" : "no",
    state.grandchild->IsValid() ? "error" : "ok");
}

} // namespace

//=============================================================================
// Main Function - Orchestrates All Demo Parts
//=============================================================================

auto main() -> int
{
  // Configure logging for cleaner debugging output
  loguru::g_preamble_date = false;
  loguru::g_preamble_file = true;
  loguru::g_preamble_verbose = false;
  loguru::g_preamble_time = false;
  loguru::g_preamble_uptime = false;
  loguru::g_preamble_thread = true;
  loguru::g_preamble_header = false;
  loguru::g_stderr_verbosity = loguru::Verbosity_2;
  loguru::g_colorlogtostderr = true;

  std::cout << "=== Oxygen Engine Scene Graph Transform Demo ===\n";

  // Initialize demo state
  DemoState state;

  // Run all demo parts in sequence
  RunBasicSceneDemo(state);
  RunTransformDemo(state);
  RunAnimationDemo(state);
  RunCleanupDemo(state);

  std::cout << "\n=== Demo Complete ===\n";
  std::cout << "Demonstrated:\n";
  std::cout << "  1. Basic scene graph operations\n";
  std::cout << "  2. High-level Transform API usage\n";
  std::cout
    << "  3. Transform operations (SetLocal*, Translate, Rotate, Scale)\n";
  std::cout << "  4. World vs Local coordinate spaces\n";
  std::cout << "  5. Hierarchical transform inheritance\n";
  std::cout << "  6. Real-time transform animation simulation\n";
  std::cout << "  7. Scene::Update() for proper transform propagation\n";

  return EXIT_SUCCESS;
}
