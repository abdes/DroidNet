//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <Oxygen/Scene/Scene.h>

namespace oxygen::scene::testing {

// Forward declaration for cached JSON template (implementation detail)
struct Template;

//! Concept for name generators that support hierarchical context
template <typename T>
concept ContextAwareNameGenerator
  = requires(T& generator, int depth, bool multiple_siblings) {
      { generator.SetDepth(depth) } -> std::same_as<void>;
      {
        generator.SetMultipleSiblingsExpected(multiple_siblings)
      } -> std::same_as<void>;
      { generator.IncrementNodeCount() } -> std::same_as<void>;
    };

//! Interface for generating node names in test scene factories.
class NameGenerator {
public:
  virtual ~NameGenerator() = default;

  //! Generates a name for a node with the given index.
  /*!
   Generates a unique name for a node based on its positional index within
   the creation context.

   The index represents the position of the node among its siblings or within
   the specific creation operation. For example, when creating multiple children
   under the same parent, index 0 is the first child, index 1 is the second
   child, etc. In a linear chain, index 0 is the root, index 1 is the first
   child, and so on. For forest creation, the index indicates which tree is
   being created.

   @param index The positional index of the node.
   @return A unique name string for the node at the given index position.
  */
  [[nodiscard]] virtual auto GenerateName(int index) const -> std::string = 0;

  //! Resets any internal state (e.g., counters).
  virtual void Reset() = 0;

  //! Sets the prefix for generated names.
  virtual void SetPrefix(const std::string& prefix) = 0;

  // No virtual methods for context support - use concepts instead!
};

//! Default name generator that creates meaningful names without embedding
//! hierarchy. Names remain valid after reparenting operations.
class DefaultNameGenerator : public NameGenerator {
public:
  DefaultNameGenerator() = default;

  [[nodiscard]] auto GenerateName(int index) const -> std::string override
  {
    // Generate role-based names that don't encode parent-child relationships
    const std::string role_name
      = DetermineRoleBasedName(prefix_, current_depth_);

    // For single nodes of a type, omit the index
    if (index == 0 && !multiple_siblings_expected_) {
      return role_name;
    }

    // For multiple nodes, append index
    return role_name + std::to_string(index);
  }

  void Reset() override
  {
    current_depth_ = 0;
    current_node_count_ = 0;
    multiple_siblings_expected_ = false;
  }

  void SetPrefix(const std::string& prefix) override { prefix_ = prefix; }

  // Context setters for intelligent naming - satisfy the concept
  void SetDepth(int depth) { current_depth_ = depth; }
  void SetMultipleSiblingsExpected(bool expected)
  {
    multiple_siblings_expected_ = expected;
  }
  void IncrementNodeCount() { current_node_count_++; }

private:
  [[nodiscard]] auto DetermineRoleBasedName(
    const std::string& prefix, int depth) const -> std::string
  {
    // Provide semantic names based on common graph patterns and depth
    if (prefix == "Node") {
      switch (depth) {
      case 0:
        return "Root";
      case 1:
        return "Child";
      case 2:
        return "Grandchild";
      case 3:
        return "GreatGrandchild";
      default:
        return "Level" + std::to_string(depth) + "Node";
      }
    }

    if (prefix == "Tree") {
      switch (depth) {
      case 0:
        return "Root";
      case 1:
        return "Branch";
      case 2:
        return "Leaf";
      default:
        return "Node";
      }
    }

    // For other prefixes, use depth-aware naming without hierarchy
    if (depth == 0) {
      return prefix + "Root";
    }

    return prefix;
  }

  std::string prefix_ = "Node";
  mutable int current_depth_ = 0;
  mutable int current_node_count_ = 0;
  mutable bool multiple_siblings_expected_ = false;
};

// Verify DefaultNameGenerator satisfies the concept
static_assert(ContextAwareNameGenerator<DefaultNameGenerator>);

//! Positional name generator that uses sequential names for clear test
//! identification.
class PositionalNameGenerator : public NameGenerator {
public:
  [[nodiscard]] auto GenerateName(int index) const -> std::string override
  {
    const std::string position_name = GenerateSequentialName(index);
    return prefix_.empty() ? position_name : prefix_ + position_name;
  }

  void Reset() override
  {
    // No persistent state to reset
  }

  void SetPrefix(const std::string& prefix) override { prefix_ = prefix; }

  // Note: PositionalNameGenerator intentionally does NOT implement context
  // methods to verify the concept properly excludes non-context-aware
  // generators

private:
  [[nodiscard]] auto GenerateSequentialName(int index) const -> std::string
  {
    const std::vector<std::string> names = { "First", "Second", "Third",
      "Fourth", "Fifth", "Sixth", "Seventh", "Eighth", "Ninth", "Tenth" };
    return (index < static_cast<int>(names.size()))
      ? names[index]
      : "Item" + std::to_string(index);
  }

  std::string prefix_;
};

// Verify PositionalNameGenerator does NOT satisfy the concept (as expected)
static_assert(!ContextAwareNameGenerator<PositionalNameGenerator>);

//! Singleton factory for creating test scene graphs from JSON templates or
//! common patterns.
class TestSceneFactory {
public:
  //=== Singleton Access ===--------------------------------------------------//

  //! Gets the singleton instance.
  static auto Instance() -> TestSceneFactory&;

  // Delete copy/move constructors and assignment operators
  TestSceneFactory(const TestSceneFactory&) = delete;
  TestSceneFactory(TestSceneFactory&&) = delete;
  auto operator=(const TestSceneFactory&) -> TestSceneFactory& = delete;
  auto operator=(TestSceneFactory&&) -> TestSceneFactory& = delete;

  //=== Configuration ===-----------------------------------------------------//

  //! Sets a custom name generator with full type safety.
  template <typename T>
    requires std::derived_from<T, NameGenerator>
  auto SetNameGenerator(std::unique_ptr<T> generator) -> TestSceneFactory&
  {
    name_generator_ = std::move(generator);
    return *this;
  }

  //! Creates and sets a name generator of the specified type.
  template <typename T, typename... Args>
    requires std::derived_from<T, NameGenerator>
  auto SetNameGenerator(Args&&... args) -> TestSceneFactory&
  {
    name_generator_ = std::make_unique<T>(std::forward<Args>(args)...);
    return *this;
  }

  //! Resets to default name generator.
  auto ResetNameGenerator() -> TestSceneFactory&;

  //! Gets the current name generator for modification.
  [[nodiscard]] auto GetNameGenerator() -> NameGenerator&;

  //! Gets the current name generator for read-only access.
  [[nodiscard]] auto GetNameGenerator() const -> const NameGenerator&;

  //! Sets the default capacity for scenes created by shortcut methods.
  auto SetDefaultCapacity(std::size_t capacity) -> TestSceneFactory&;

  //! Gets the current default capacity (nullopt if not set).
  [[nodiscard]] auto GetDefaultCapacity() const -> std::optional<std::size_t>;

  //! Resets the factory to default state (clears templates, resets name
  //! generator, clears capacity).
  auto Reset() -> TestSceneFactory&;

  //=== JSON-based Scene Creation ===----------------------------------------//

  //! Creates a scene graph from a JSON template string.
  //! Throws std::invalid_argument if JSON is malformed or doesn't match schema.
  [[nodiscard]] auto CreateFromJson(const std::string& json_template,
    std::string_view scene_name = "TestScene",
    std::size_t capacity = 1024) const -> std::shared_ptr<Scene>;

  //=== Common Pattern Shortcuts ===------------------------------------------//

  //! Creates a scene with a single root node.
  [[nodiscard]] auto CreateSingleNodeScene(
    std::string_view scene_name = "TestScene") const -> std::shared_ptr<Scene>;

  //! Creates a scene with a parent and single child.
  [[nodiscard]] auto CreateParentChildScene(
    std::string_view scene_name = "TestScene") const -> std::shared_ptr<Scene>;

  //! Creates a scene with a parent and multiple children.
  [[nodiscard]] auto CreateParentWithChildrenScene(
    std::string_view scene_name = "TestScene", int child_count = 2) const
    -> std::shared_ptr<Scene>;

  //! Creates a scene with a linear chain of nodes (A -> B -> C -> ...).
  [[nodiscard]] auto CreateLinearChainScene(
    std::string_view scene_name = "TestScene", int depth = 3) const
    -> std::shared_ptr<Scene>;

  //! Creates a scene with a binary tree structure.
  [[nodiscard]] auto CreateBinaryTreeScene(
    std::string_view scene_name = "TestScene", int depth = 2) const
    -> std::shared_ptr<Scene>;

  //! Creates a scene with a forest (multiple root nodes with children).
  [[nodiscard]] auto CreateForestScene(
    std::string_view scene_name = "TestScene", int root_count = 2,
    int children_per_root = 2) const -> std::shared_ptr<Scene>;

  //=== Template Management ===-----------------------------------------------//

  //! Registers a named JSON template for reuse.
  //! Throws std::invalid_argument if template is invalid.
  auto RegisterTemplate(const std::string& name,
    const std::string& json_template) -> TestSceneFactory&;

  //! Creates a scene from a registered template.
  //! Returns nullptr if template doesn't exist.
  [[nodiscard]] auto CreateFromTemplate(const std::string& template_name,
    std::string_view scene_name = "TestScene",
    std::size_t capacity = 1024) const -> std::shared_ptr<Scene>;

  //=== Schema Validation ===-------------------------------------------------//

  //! Gets the embedded JSON schema for external validation tools.
  //! The schema is compatible with JSON Schema Draft-7.
  [[nodiscard]] static auto GetJsonSchema() -> std::string_view;

  //! Validates a JSON string against the embedded schema without creating a
  //! scene. Returns nullopt if valid, or an error message if invalid.
  [[nodiscard]] static auto ValidateJson(const std::string& json_string)
    -> std::optional<std::string>;

private:
  TestSceneFactory();
  ~TestSceneFactory() = default;

  //=== Internal Implementation ===-------------------------------------------//

  //! Creates a scene with appropriate capacity based on default setting.
  [[nodiscard]] auto CreateScene(std::string_view scene_name) const
    -> std::shared_ptr<Scene>;

  //! Generates a unique name with index using current name generator.
  [[nodiscard]] auto GenerateNodeName(int index) const -> std::string;

  //! Updates name generator context.
  void UpdateNamingContext(int depth, bool multiple_siblings_expected) const;

  //=== Member Variables ===--------------------------------------------------//

  //! Registry of named JSON templates (cached and parsed).
  mutable std::unordered_map<std::string, std::unique_ptr<Template>> templates_;

  //! Current name generator (never null).
  std::unique_ptr<NameGenerator> name_generator_;

  //! Default capacity for shortcut scene creation methods.
  std::optional<std::size_t> default_capacity_;

  //! Default name generator instance.
  static std::unique_ptr<NameGenerator> CreateDefaultNameGenerator();
};

} // namespace oxygen::scene::testing
