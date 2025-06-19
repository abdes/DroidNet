//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>

#include "./TestSceneFactory.h"
#include "./TestSceneFactory_schema.h"

using nlohmann::json;
using nlohmann::json_schema::json_validator;
using oxygen::scene::testing::DefaultNameGenerator;
using oxygen::scene::testing::NameGenerator;
using oxygen::scene::testing::PositionalNameGenerator;
using oxygen::scene::testing::TestSceneFactory;

namespace oxygen::scene::testing {

//=== Embedded JSON Schema ===-----------------------------------------------//

namespace {

  //! Cached validator instance for performance
  class SchemaValidator {
  public:
    SchemaValidator()
    {
      try {
        auto schema_json = json::parse(kTestSceneFactorySchema);
        validator_.set_root_schema(schema_json);
      } catch (const std::exception& e) {
        throw std::runtime_error(
          "Failed to parse embedded schema: " + std::string(e.what()));
      }
    }

    auto Validate(const json& instance) const -> std::optional<std::string>
    {
      try {
        [[maybe_unused]] auto _ = validator_.validate(instance);
        return std::nullopt; // Validation successful
      } catch (const std::exception& e) {
        return std::string(e.what());
      }
    }

    static auto Instance() -> const SchemaValidator&
    {
      static SchemaValidator instance;
      return instance;
    }

  private:
    mutable json_validator validator_;
  };

  //! Validates JSON against the embedded schema using proper JSON Schema
  //! validation
  auto ValidateJsonAgainstSchema(const json& json_data)
    -> std::optional<std::string>
  {
    try {
      return SchemaValidator::Instance().Validate(json_data);
    } catch (const std::exception& e) {
      return "Schema validation setup error: " + std::string(e.what());
    }
  }

} // anonymous namespace

//=== Template Implementation ===--------------------------------------------//

struct Template {
  json parsed_json;
  std::string original_string; // Keep for debugging/logging

  Template(json json, std::string original)
    : parsed_json(std::move(json))
    , original_string(std::move(original))
  {
  }
};

//=== Type-Safe JSON Processing Functions ===--------------------------------//

namespace {

  // Forward declarations to resolve dependencies
  auto CreateSceneFromJson(const TestSceneFactory& factory,
    std::string_view scene_name, std::size_t capacity,
    const json& json_template) -> std::shared_ptr<Scene>;

  auto CreateNodeFromJson(const TestSceneFactory& factory,
    const std::shared_ptr<Scene>& scene, const json& node_spec,
    std::optional<SceneNode> parent) -> SceneNode;

  void CreateChildrenFromJson(const TestSceneFactory& factory,
    const std::shared_ptr<Scene>& scene, SceneNode& parent,
    const json& children_spec, std::vector<SceneNode>& all_nodes);

  void ApplyNodeProperties(const SceneNode& node, const json& properties);

  // Function implementations

  //! Creates a scene and populates it from JSON specification.
  auto CreateSceneFromJson(const TestSceneFactory& factory,
    std::string_view scene_name, std::size_t capacity,
    const json& json_template) -> std::shared_ptr<Scene>
  {
    auto scene = std::make_shared<Scene>(std::string(scene_name), capacity);

    // Use const_cast to reset the name generator (design limitation)
    const_cast<TestSceneFactory&>(factory).GetNameGenerator().Reset();

    if (!json_template.is_object()) {
      throw std::invalid_argument("JSON template must be an object");
    }

    // Look for "nodes" array at the root
    if (json_template.contains("nodes") && json_template["nodes"].is_array()) {
      for (const auto& node_spec : json_template["nodes"]) {
        CreateNodeFromJson(factory, scene, node_spec, std::nullopt);
      }
    }

    return scene;
  }
  //! Creates a single node from JSON specification.
  auto CreateNodeFromJson(const TestSceneFactory& factory,
    const std::shared_ptr<Scene>& scene, const json& node_spec,
    std::optional<SceneNode> parent) -> SceneNode
  {
    if (!node_spec.is_object()) {
      throw std::invalid_argument("Node specification must be an object");
    }

    // Determine node name
    std::string node_name;
    if (node_spec.contains("name") && node_spec["name"].is_string()) {
      node_name = node_spec["name"].get<std::string>();
    } else {
      // Generate name using current name generator directly
      static int auto_index = 0;
      node_name = factory.GetNameGenerator().GenerateName(auto_index++);
    }

    // Extract flags from JSON if specified
    SceneNode::Flags node_flags = SceneNodeImpl::kDefaultFlags;
    if (node_spec.contains("flags") && node_spec["flags"].is_object()) {
      const auto& flags_json = node_spec["flags"];

      // Parse visibility flag
      if (flags_json.contains("visible")
        && flags_json["visible"].is_boolean()) {
        bool visible = flags_json["visible"].get<bool>();
        node_flags = node_flags.SetFlag(
          SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(visible));
      }

      // Parse other common flags as needed
      if (flags_json.contains("static") && flags_json["static"].is_boolean()) {
        bool is_static = flags_json["static"].get<bool>();
        node_flags = node_flags.SetFlag(SceneNodeFlags::kStatic,
          SceneFlag {}.SetEffectiveValueBit(is_static));
      }

      if (flags_json.contains("castsShadows")
        && flags_json["castsShadows"].is_boolean()) {
        bool casts_shadows = flags_json["castsShadows"].get<bool>();
        node_flags = node_flags.SetFlag(SceneNodeFlags::kCastsShadows,
          SceneFlag {}.SetEffectiveValueBit(casts_shadows));
      }

      if (flags_json.contains("receivesShadows")
        && flags_json["receivesShadows"].is_boolean()) {
        bool receives_shadows = flags_json["receivesShadows"].get<bool>();
        node_flags = node_flags.SetFlag(SceneNodeFlags::kReceivesShadows,
          SceneFlag {}.SetEffectiveValueBit(receives_shadows));
      }
    }

    // Create the node with the appropriate flags
    SceneNode node;
    if (parent.has_value()) {
      auto child_opt
        = scene->CreateChildNode(parent.value(), node_name, node_flags);
      if (!child_opt.has_value()) {
        throw std::runtime_error("Failed to create child node: " + node_name);
      }
      node = *child_opt;
    } else {
      node = scene->CreateNode(node_name, node_flags);
    }

    // Apply node properties
    ApplyNodeProperties(node, node_spec);

    // Create children if specified
    if (node_spec.contains("children") && node_spec["children"].is_array()) {
      std::vector<SceneNode> children_nodes;
      CreateChildrenFromJson(
        factory, scene, node, node_spec["children"], children_nodes);
    }

    return node;
  }

  //! Recursively creates children from JSON specification.
  void CreateChildrenFromJson(const TestSceneFactory& factory,
    const std::shared_ptr<Scene>& scene, SceneNode& parent,
    const json& children_spec, std::vector<SceneNode>& all_nodes)
  {
    if (!children_spec.is_array()) {
      throw std::invalid_argument("Children specification must be an array");
    }

    for (const auto& child_spec : children_spec) {
      auto child = CreateNodeFromJson(factory, scene, child_spec, parent);
      all_nodes.push_back(child);
    }
  }

  //! Applies node properties from JSON.
  void ApplyNodeProperties(const SceneNode& node, const json& properties)
  {
    // Apply transform properties if specified
    if (properties.contains("transform")
      && properties["transform"].is_object()) {
      const auto& transform_json = properties["transform"];
      auto transform = node.GetTransform();

      // Position
      if (transform_json.contains("position")
        && transform_json["position"].is_array()) {
        const auto& pos = transform_json["position"];
        if (pos.size() >= 3) {
          const glm::vec3 position { pos[0].get<float>(), pos[1].get<float>(),
            pos[2].get<float>() };
          [[maybe_unused]] auto _ = transform.SetLocalPosition(position);
        }
      }

      // Scale
      if (transform_json.contains("scale")
        && transform_json["scale"].is_array()) {
        const auto& scale_json = transform_json["scale"];
        if (scale_json.size() >= 3) {
          const glm::vec3 scale { scale_json[0].get<float>(),
            scale_json[1].get<float>(), scale_json[2].get<float>() };
          [[maybe_unused]] auto _ = transform.SetLocalScale(scale);
        }
      }

      // Rotation (Euler angles in degrees)
      if (transform_json.contains("rotation")
        && transform_json["rotation"].is_array()) {
        const auto& rot = transform_json["rotation"];
        if (rot.size() >= 3) {
          const auto euler_degrees = glm::vec3 { rot[0].get<float>(),
            rot[1].get<float>(), rot[2].get<float>() };
          const auto euler_radians = glm::radians(euler_degrees);
          const glm::quat rotation(euler_radians);
          [[maybe_unused]] auto _ = transform.SetLocalRotation(rotation);
        }
      }
    }

    // Apply flags if specified (simplified implementation for now)
    if (properties.contains("flags") && properties["flags"].is_object()) {
      const auto& flags = properties["flags"];

      // For now, we'll just handle basic visibility flag
      // Note: Proper flag manipulation would require extending the SceneNode
      // API
      if (flags.contains("visible") && flags["visible"].is_boolean()) {
        // This would require extending SceneNode with flag manipulation methods
        // For now, this is just a placeholder for future implementation
      }
    }
  }

} // anonymous namespace

//=== TestSceneFactory Implementation ===-------------------------------------//

auto TestSceneFactory::Instance() -> TestSceneFactory&
{
  static TestSceneFactory instance;
  return instance;
}

TestSceneFactory::TestSceneFactory()
  : name_generator_(CreateDefaultNameGenerator())
  , context_updater_(CreateContextUpdater())
{
}

auto TestSceneFactory::CreateDefaultNameGenerator()
  -> std::unique_ptr<NameGenerator>
{
  return std::make_unique<DefaultNameGenerator>();
}

auto TestSceneFactory::CreateContextUpdater()
  -> std::function<void(NameGenerator*, int, bool)>
{
  return [](NameGenerator* gen, int depth, bool multiple_siblings) {
    auto* typed_gen = static_cast<DefaultNameGenerator*>(gen);
    typed_gen->SetDepth(depth);
    typed_gen->SetMultipleSiblingsExpected(multiple_siblings);
  };
}

//=== Configuration Methods ===-----------------------------------------------//

auto TestSceneFactory::ResetNameGenerator() -> TestSceneFactory&
{
  name_generator_ = CreateDefaultNameGenerator();
  context_updater_ = CreateContextUpdater();
  return *this;
}

auto TestSceneFactory::GetNameGenerator() -> NameGenerator&
{
  return *name_generator_;
}

auto TestSceneFactory::GetNameGenerator() const -> const NameGenerator&
{
  return *name_generator_;
}

auto TestSceneFactory::SetDefaultCapacity(std::size_t capacity)
  -> TestSceneFactory&
{
  default_capacity_ = capacity;
  return *this;
}

auto TestSceneFactory::GetDefaultCapacity() const -> std::optional<std::size_t>
{
  return default_capacity_;
}

auto TestSceneFactory::Reset() -> TestSceneFactory&
{
  name_generator_ = CreateDefaultNameGenerator();
  context_updater_ = CreateContextUpdater();
  default_capacity_.reset();
  templates_.clear();
  return *this;
}

//=== Scene Creation Helpers ===--------------------------------------------//

auto TestSceneFactory::CreateScene(std::string_view scene_name) const
  -> std::shared_ptr<Scene>
{
  if (default_capacity_.has_value()) {
    return std::make_shared<Scene>(std::string(scene_name), *default_capacity_);
  }
  return std::make_shared<Scene>(std::string(scene_name));
}

auto TestSceneFactory::GenerateNodeName(int index) const -> std::string
{
  return name_generator_->GenerateName(index);
}

//=== Common Pattern Shortcuts ===--------------------------------------------//

auto TestSceneFactory::CreateEmptyScene(std::string_view scene_name) const
  -> std::shared_ptr<Scene>
{
  auto scene = CreateScene(scene_name);
  return scene;
}

auto TestSceneFactory::CreateSingleNodeScene(std::string_view scene_name) const
  -> std::shared_ptr<Scene>
{
  auto scene = CreateScene(scene_name);
  name_generator_->Reset();

  const auto root_name = GenerateNodeName(0);
  [[maybe_unused]] auto _ = scene->CreateNode(root_name);

  return scene;
}

auto TestSceneFactory::CreateParentChildScene(std::string_view scene_name) const
  -> std::shared_ptr<Scene>
{
  auto scene = CreateScene(scene_name);
  name_generator_->Reset();

  const auto parent_name = GenerateNodeName(0);
  const auto child_name = GenerateNodeName(1);

  auto parent = scene->CreateNode(parent_name);
  [[maybe_unused]] auto _ = scene->CreateChildNode(parent, child_name);

  return scene;
}

auto TestSceneFactory::CreateParentWithChildrenScene(
  std::string_view scene_name, int child_count) const -> std::shared_ptr<Scene>
{
  auto scene = CreateScene(scene_name);
  name_generator_->Reset();

  const auto parent_name = GenerateNodeName(0);
  auto parent = scene->CreateNode(parent_name);

  // Set context for children level
  UpdateNamingContext(1, child_count > 1);

  for (int i = 0; i < child_count; ++i) {
    const auto child_name = GenerateNodeName(i + 1);
    [[maybe_unused]] auto _ = scene->CreateChildNode(parent, child_name);
  }

  return scene;
}

auto TestSceneFactory::CreateLinearChainScene(
  std::string_view scene_name, int depth) const -> std::shared_ptr<Scene>
{
  auto scene = CreateScene(scene_name);
  name_generator_->Reset();

  if (depth <= 0) {
    return scene;
  }

  const auto root_name = GenerateNodeName(0);
  auto current = scene->CreateNode(root_name);

  for (int i = 1; i < depth; ++i) {
    UpdateNamingContext(i, false); // Linear chain = single child at each level
    const auto child_name = GenerateNodeName(0); // index 0 for single child
    auto child_opt = scene->CreateChildNode(current, child_name);
    if (child_opt.has_value()) {
      current = *child_opt;
    }
  }

  return scene;
}

auto TestSceneFactory::CreateBinaryTreeScene(
  std::string_view scene_name, int depth) const -> std::shared_ptr<Scene>
{
  auto scene = CreateScene(scene_name);
  name_generator_->Reset();

  if (depth <= 0) {
    return scene;
  }

  const auto root_name = GenerateNodeName(0);
  auto root = scene->CreateNode(root_name);

  if (depth == 1) {
    return scene;
  }

  // Create binary tree using breadth-first approach
  std::vector<SceneNode> current_level = { root };
  int name_index = 1;

  for (int level = 1; level < depth; ++level) {
    std::vector<SceneNode> next_level;
    UpdateNamingContext(level, true); // Multiple siblings at each level

    for (auto& parent : current_level) {
      // Create left child
      const auto left_name = GenerateNodeName(name_index++);
      auto left_opt = scene->CreateChildNode(parent, left_name);
      if (left_opt.has_value()) {
        next_level.push_back(*left_opt);
      }

      // Create right child
      const auto right_name = GenerateNodeName(name_index++);
      auto right_opt = scene->CreateChildNode(parent, right_name);
      if (right_opt.has_value()) {
        next_level.push_back(*right_opt);
      }
    }

    current_level = std::move(next_level);
  }

  return scene;
}

auto TestSceneFactory::CreateForestScene(std::string_view scene_name,
  int root_count, int children_per_root) const -> std::shared_ptr<Scene>
{
  auto scene = CreateScene(scene_name);
  name_generator_->Reset();

  int name_index = 0;

  for (int i = 0; i < root_count; ++i) {
    // Set context for root level
    UpdateNamingContext(0, root_count > 1);
    const auto root_name = GenerateNodeName(name_index++);
    auto root = scene->CreateNode(root_name);

    // Set context for children level
    UpdateNamingContext(1, children_per_root > 1);
    for (int j = 0; j < children_per_root; ++j) {
      const auto child_name = GenerateNodeName(name_index++);
      [[maybe_unused]] auto _ = scene->CreateChildNode(root, child_name);
    }
  }

  return scene;
}

//=== Template Management ===-------------------------------------------------//

auto TestSceneFactory::RegisterTemplate(const std::string& name,
  const std::string& json_template) -> TestSceneFactory&
{
  try {
    // Parse and validate JSON
    auto json = json::parse(json_template);

    // Validate against schema using proper JSON Schema validation
    if (auto validation_error = ValidateJsonAgainstSchema(json)) {
      throw std::invalid_argument(
        "JSON Schema validation failed: " + *validation_error);
    }

    // Cache the parsed template
    templates_[name]
      = std::make_unique<Template>(std::move(json), json_template);
    return *this;
  } catch (const json::parse_error& e) {
    throw std::invalid_argument(
      "Invalid JSON template: " + std::string(e.what()));
  }
}

auto TestSceneFactory::CreateFromTemplate(const std::string& template_name,
  std::string_view scene_name, std::size_t capacity) const
  -> std::shared_ptr<Scene>
{
  auto it = templates_.find(template_name);
  if (it == templates_.end()) {
    return nullptr;
  }

  // Use the cached parsed JSON directly
  return CreateSceneFromJson(
    *this, scene_name, capacity, it->second->parsed_json);
}

//=== JSON Scene Creation
//===---------------------------------------------------//

auto TestSceneFactory::CreateFromJson(const std::string& json_template,
  std::string_view scene_name, std::size_t capacity) const
  -> std::shared_ptr<Scene>
{
  try {
    auto json = json::parse(json_template);

    // Validate against schema using proper JSON Schema validation
    if (auto validation_error = ValidateJsonAgainstSchema(json)) {
      throw std::invalid_argument(
        "JSON Schema validation failed: " + *validation_error);
    }

    return CreateSceneFromJson(*this, scene_name, capacity, json);
  } catch (const json::parse_error& e) {
    throw std::invalid_argument("Invalid JSON: " + std::string(e.what()));
  }
}

//=== Schema Access ===------------------------------------------------------//

//! Gets the embedded JSON schema for external validation tools
auto TestSceneFactory::GetJsonSchema() -> std::string_view
{
  return kTestSceneFactorySchema;
}

//! Validates a JSON string against the schema without creating a scene
auto TestSceneFactory::ValidateJson(const std::string& json_string)
  -> std::optional<std::string>
{
  try {
    auto json = json::parse(json_string);
    return ValidateJsonAgainstSchema(json);
  } catch (const json::parse_error& e) {
    return "JSON parsing error: " + std::string(e.what());
  }
}

//! Updates name generator context using type-erased function pointer.
void TestSceneFactory::UpdateNamingContext(
  int depth, bool multiple_siblings_expected) const
{
  // Use stored function to update context - no RTTI required!
  if (context_updater_) {
    context_updater_(name_generator_.get(), depth, multiple_siblings_expected);
  }
}

} // namespace oxygen::scene::testing
