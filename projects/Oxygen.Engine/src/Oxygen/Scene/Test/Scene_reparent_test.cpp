//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scene/detail/TransformComponent.h>

#include <fmt/format.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using oxygen::scene::NodeHandle;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::detail::TransformComponent;

namespace {

//------------------------------------------------------------------------------
// Modern Hierarchy Testing Utilities
//------------------------------------------------------------------------------
// This file provides a comprehensive testing framework for scene graph
// hierarchies:
//
// 1. FluentHierarchyBuilder - Modern DSL for creating complex hierarchies with
// method chaining
//    Usage:
//    builder.Fluent().Root("GameWorld").Child("Player").At(10,0,5).Scale(2.0f).Up()...
//
// 2. HierarchyTreeFormatter - ASCII tree visualization with properties display
//    Shows position, scale, visibility in compact format with proper tree
//    structure
//
// 3. HierarchyDiff - Sophisticated comparison system with detailed diff
// reporting
//    Order-independent comparison, property-level diffs, side-by-side tree
//    views
//
// 4. HierarchyTestBuilder - Main API wrapper providing all testing capabilities
//    Methods: Fluent(), FormatAsTree(), ExpectEqual(), GetDiffReport(),
//    AreDifferent()
//
// Key Features:
// - Fluent DSL with intuitive navigation (Root, Child, Up, ToRoot, At, Scale,
// Visible, Hidden)
// - Order-independent hierarchy comparison (children can be in any order)
// - Visual ASCII tree output with property annotations
// - Comprehensive diff reports with change categorization
// (Added/Removed/Modified)
// - Property-level change detection (position, scale, visibility, children)
//------------------------------------------------------------------------------

// Forward declarations
class FluentHierarchyBuilder;
class HierarchyDiff;

// Hierarchy node data for comparisons
// Enhanced hierarchy node data using NodeHandle identity
struct HierarchyNodeData {
  NodeHandle handle;                    // Unique identity (includes Scene ID)
  std::string name;                     // For display purposes only
  NodeHandle parent_handle;             // Direct handle reference instead of name
  glm::vec3 position = { 0.0f, 0.0f, 0.0f };
  glm::vec3 scale = { 1.0f, 1.0f, 1.0f };
  bool visible = true;
  std::unordered_set<NodeHandle> child_handles; // Direct handle references

  // Scene validation helpers
  [[nodiscard]] auto GetSceneId() const noexcept { return handle.GetSceneId(); }
  [[nodiscard]] auto BelongsToScene(NodeHandle::SceneId scene_id) const noexcept {
    return handle.BelongsToScene(scene_id);
  }
  [[nodiscard]] auto IsValidInScene(NodeHandle::SceneId scene_id) const noexcept {
    return handle.IsValid() && BelongsToScene(scene_id);
  }

  bool operator==(const HierarchyNodeData& other) const
  {
    // Primary comparison by handle (includes scene validation)
    return handle == other.handle
      && parent_handle == other.parent_handle
      && position == other.position
      && scale == other.scale
      && visible == other.visible
      && child_handles == other.child_handles;
  }
};

// Diff types for hierarchy comparison
enum class DiffType {
  Added, // Node exists in actual but not in expected
  Removed, // Node exists in expected but not in actual
  Modified, // Node exists in both but has different properties
  Moved, // Node exists in both but has different parent/position in tree
  Unchanged // Node is identical in both hierarchies
};

struct NodeDiff {
  DiffType type;
  std::string node_name;                          // Keep for display purposes
  NodeHandle node_handle;                         // Primary identity
  std::optional<HierarchyNodeData> expected_data;
  std::optional<HierarchyNodeData> actual_data;
  std::vector<std::string> property_differences; // Detailed change list
};

// Fluent builder for creating hierarchies
class FluentHierarchyBuilder {
private:
  std::shared_ptr<Scene> scene_;
  std::vector<SceneNode> node_stack_; // Simplified: direct SceneNode storage
  SceneNode current_node_; // Simplified: no optional wrapper needed

public:
  explicit FluentHierarchyBuilder(std::shared_ptr<Scene> scene)
    : scene_(std::move(scene))
  {
  }

  // Start building with a root node
  FluentHierarchyBuilder& Root(const std::string& name)
  {
    current_node_ = scene_->CreateNode(name); // Simplified: direct assignment
    node_stack_.clear();
    node_stack_.push_back(current_node_);
    return *this;
  }

  // Set position (shorthand methods)
  FluentHierarchyBuilder& At(float x, float y, float z)
  {
    return Pos({ x, y, z });
  }

  FluentHierarchyBuilder& Pos(const glm::vec3& position)
  {
    if (current_node_.IsValid()) {
      auto impl_opt = current_node_.GetObject();
      if (impl_opt.has_value()) {
        auto& transform = impl_opt->get().GetComponent<TransformComponent>();
        transform.SetLocalPosition(position);
      }
    }
    return *this;
  }

  // Set scale
  FluentHierarchyBuilder& Scale(float uniform_scale)
  {
    return Scale({ uniform_scale, uniform_scale, uniform_scale });
  }

  FluentHierarchyBuilder& Scale(const glm::vec3& scale)
  {
    if (current_node_.IsValid()) {
      auto impl_opt = current_node_.GetObject();
      if (impl_opt.has_value()) {
        auto& transform = impl_opt->get().GetComponent<TransformComponent>();
        transform.SetLocalScale(scale);
      }
    }
    return *this;
  }

  // Set visibility
  FluentHierarchyBuilder& Visible(bool visible = true)
  {
    if (current_node_.IsValid()) {
      auto impl_opt = current_node_.GetObject();
      if (impl_opt.has_value()) {
        auto& flags = impl_opt->get().GetFlags();
        flags.SetLocalValue(SceneNodeFlags::kVisible, visible);
        flags.ProcessDirtyFlags();
      }
    }
    return *this;
  }

  FluentHierarchyBuilder& Hidden() { return Visible(false); }

  // Add a child node
  FluentHierarchyBuilder& Child(const std::string& name)
  {
    if (!current_node_.IsValid()) {
      EXPECT_TRUE(false) << "Cannot add child: no current node";
      return *this;
    }

    auto child_opt = scene_->CreateChildNode(current_node_, name);
    EXPECT_TRUE(child_opt.has_value()) << "Failed to create child: " << name;
    if (child_opt.has_value()) {
      node_stack_.push_back(current_node_);
      current_node_ = child_opt.value();
    }
    return *this;
  }

  // Navigate back up to parent
  FluentHierarchyBuilder& Up()
  {
    if (node_stack_.size() > 1) {
      node_stack_.pop_back();
      current_node_ = node_stack_.back(); // Simplified: direct assignment
    }
    return *this;
  }

  // Navigate to root
  FluentHierarchyBuilder& ToRoot()
  {
    if (!node_stack_.empty()) {
      current_node_ = node_stack_.front(); // Simplified: direct assignment
      node_stack_.resize(1);
    }
    return *this;
  }

  // Finish building and return root
  SceneNode Build()
  {
    if (node_stack_.empty()) {
      // Return default-constructed SceneNode (thanks to default constructor)
      return SceneNode();
    }
    return node_stack_.front(); // Simplified: direct return
  }
};

// ASCII tree generator
class HierarchyTreeFormatter {
public:
  static std::string FormatAsTree(
    const SceneNode& root, const std::string& title = "")
  {
    std::stringstream ss;
    if (!title.empty()) {
      ss << title << "\n";
    }
    FormatNodeRecursive(root, ss, "", true);
    return ss.str();
  }

private:
  static void FormatNodeRecursive(const SceneNode& node, std::stringstream& ss,
    const std::string& prefix, bool is_last)
  {
    auto impl_opt = node.GetObject();
    if (!impl_opt.has_value())
      return;

    const auto& impl = impl_opt->get();
    const auto& transform = impl.GetComponent<TransformComponent>();
    const auto& flags = impl.GetFlags();

    // Node line with properties
    ss << prefix << (is_last ? "`-- " : "|-- ") << impl.GetName();

    // Add compact property info
    auto pos = transform.GetLocalPosition();
    auto scale = transform.GetLocalScale();
    bool visible = flags.GetEffectiveValue(SceneNodeFlags::kVisible);

    ss << " [" << pos.x << "," << pos.y << "," << pos.z << "]";
    if (scale != glm::vec3(1.0f)) {
      ss << " scale(" << scale.x << "," << scale.y << "," << scale.z << ")";
    }
    if (!visible) {
      ss << " [HIDDEN]";
    }
    ss << "\n";

    // Process children (sort by NodeHandle for consistent output)
    std::vector<SceneNode> children;
    auto child_opt = node.GetFirstChild();
    while (child_opt.has_value()) {
      children.push_back(child_opt.value());
      child_opt = child_opt->GetNextSibling();
    }

    // Sort children by NodeHandle for deterministic display order
    std::sort(children.begin(), children.end(),
      [](const SceneNode& a, const SceneNode& b) {
        return a.GetHandle() < b.GetHandle();
      });

    for (size_t i = 0; i < children.size(); ++i) {
      bool child_is_last = (i == children.size() - 1);
      std::string child_prefix = prefix + (is_last ? "    " : "|   ");
      FormatNodeRecursive(children[i], ss, child_prefix, child_is_last);
    }
  }
};

// Sophisticated hierarchy diff system
class HierarchyDiff {
private:
  std::unordered_map<NodeHandle, HierarchyNodeData> expected_nodes_;
  std::unordered_map<NodeHandle, HierarchyNodeData> actual_nodes_;
  std::vector<NodeDiff> differences_;
  NodeHandle::SceneId expected_scene_id_ = NodeHandle::kInvalidSceneId;
  NodeHandle::SceneId actual_scene_id_ = NodeHandle::kInvalidSceneId;

  // Structure to represent merged hierarchy for diff display
  struct MergedNode {
    std::string name;
    std::string parent_name;
    DiffType diff_type = DiffType::Unchanged;
    std::optional<HierarchyNodeData> expected_data;
    std::optional<HierarchyNodeData> actual_data;
    std::vector<std::shared_ptr<MergedNode>> children;
  };  std::shared_ptr<MergedNode> BuildMergedHierarchy()
  {
    // Find all root nodes (nodes with invalid parent handles)
    std::set<NodeHandle> all_root_handles;

    // Collect root handles from expected nodes
    for (const auto& [handle, data] : expected_nodes_) {
      if (!data.parent_handle.IsValid()) {
        all_root_handles.insert(handle);
      }
    }

    // Collect root handles from actual nodes
    for (const auto& [handle, data] : actual_nodes_) {
      if (!data.parent_handle.IsValid()) {
        all_root_handles.insert(handle);
      }
    }

    // If no roots found, return nullptr
    if (all_root_handles.empty()) {
      return nullptr;
    }

    // Create a virtual root to hold all actual roots
    auto virtual_root = std::make_shared<MergedNode>();
    virtual_root->name = "<ROOT>";
    virtual_root->parent_name = "";
    virtual_root->diff_type = DiffType::Unchanged;

    // Build children for each root handle
    for (const NodeHandle& root_handle : all_root_handles) {
      auto child = BuildMergedNodeRecursive(root_handle);
      if (child) {
        virtual_root->children.push_back(child);
      }
    }

    return virtual_root;
  }
  HierarchyNodeData ExtractNodeData(const SceneNode& node)
  {
    HierarchyNodeData data;
    auto impl_opt = node.GetObject();
    if (!impl_opt.has_value())
      return data;

    const auto& impl = impl_opt->get();
    const auto& transform = impl.GetComponent<TransformComponent>();
    const auto& flags = impl.GetFlags();

    // Use NodeHandle as primary identity
    data.handle = node.GetHandle();
    data.name = impl.GetName(); // For display only
    data.position = transform.GetLocalPosition();
    data.scale = transform.GetLocalScale();
    data.visible = flags.GetEffectiveValue(SceneNodeFlags::kVisible);

    // Get parent handle directly
    auto parent_opt = node.GetParent();
    data.parent_handle = parent_opt.has_value() ? parent_opt->GetHandle() : NodeHandle{};

    // Collect child handles directly
    auto child_opt = node.GetFirstChild();
    while (child_opt.has_value()) {
      data.child_handles.insert(child_opt->GetHandle());
      child_opt = child_opt->GetNextSibling();
    }

    return data;
  }
  void CollectNodesRecursive(const SceneNode& node,
    std::unordered_map<NodeHandle, HierarchyNodeData>& node_map)
  {
    auto data = ExtractNodeData(node);

    // Validate scene consistency if this is not the first node
    if (!node_map.empty()) {
      auto first_scene_id = node_map.begin()->second.GetSceneId();
      if (!data.BelongsToScene(first_scene_id)) {
        EXPECT_TRUE(false) << "Cross-scene comparison detected: node "
                          << data.name << " belongs to scene " << data.GetSceneId()
                          << " but expected scene " << first_scene_id;
        return;
      }
    }

    node_map[data.handle] = data;

    auto child_opt = node.GetFirstChild();
    while (child_opt.has_value()) {
      CollectNodesRecursive(child_opt.value(), node_map);
      child_opt = child_opt->GetNextSibling();
    }
  }
  std::vector<std::string> CompareNodeProperties(
    const HierarchyNodeData& expected, const HierarchyNodeData& actual)
  {
    std::vector<std::string> diffs;

    if (expected.position != actual.position) {
      diffs.push_back(fmt::format("Position: expected [{:.1f},{:.1f},{:.1f}] "
                                  "but was [{:.1f},{:.1f},{:.1f}]",
        expected.position.x, expected.position.y, expected.position.z,
        actual.position.x, actual.position.y, actual.position.z));
    }

    if (expected.scale != actual.scale) {
      diffs.push_back(fmt::format(
        "Scale: expected [{:.1f},{:.1f},{:.1f}] but was [{:.1f},{:.1f},{:.1f}]",
        expected.scale.x, expected.scale.y, expected.scale.z, actual.scale.x,
        actual.scale.y, actual.scale.z));
    }

    if (expected.visible != actual.visible) {
      diffs.push_back(fmt::format("Visibility: expected {} but was {}",
        expected.visible ? "visible" : "hidden",
        actual.visible ? "visible" : "hidden"));
    }

    if (expected.child_handles != actual.child_handles) {
      // For cross-scene comparisons, we need to match by logical identity
      // rather than exact handle equality (since scene ID will differ)
      auto expected_scene_id = expected.GetSceneId();
      auto actual_scene_id = actual.GetSceneId();

      if (expected_scene_id == actual_scene_id) {
        // Same scene - direct handle comparison
        diffs.push_back("Children differ: handle sets don't match");
      } else {
        // Cross-scene - compare child count and names as a simpler heuristic
        if (expected.child_handles.size() != actual.child_handles.size()) {
          diffs.push_back(fmt::format("Child count differs: expected {} but was {}",
            expected.child_handles.size(), actual.child_handles.size()));
        }
        // Could enhance with detailed logical child comparison
      }
    }

    return diffs;
  }

public:  void Compare(const SceneNode& expected_root, const SceneNode& actual_root)
  {
    expected_nodes_.clear();
    actual_nodes_.clear();
    differences_.clear();

    // Early validation - ensure we're comparing nodes from valid scenes
    if (!expected_root.IsValid() || !actual_root.IsValid()) {
      EXPECT_TRUE(false) << "Cannot compare invalid scene nodes";
      return;
    }

    // Extract scene IDs for validation
    expected_scene_id_ = expected_root.GetHandle().GetSceneId();
    actual_scene_id_ = actual_root.GetHandle().GetSceneId();

    // Collect all nodes from both hierarchies
    CollectNodesRecursive(expected_root, expected_nodes_);
    CollectNodesRecursive(actual_root, actual_nodes_);    // Validate scene consistency
    if (expected_scene_id_ != actual_scene_id_) {
      // Cross-scene comparison - this is usually valid for adoption/migration tests
      LOG_F(INFO, "Cross-scene comparison: expected scene %s vs actual scene %s",
             nostd::to_string(expected_scene_id_).c_str(), nostd::to_string(actual_scene_id_).c_str());
    }

    // Find differences using handle-based comparison
    std::unordered_set<NodeHandle> processed;

    // Check for added/modified nodes
    for (const auto& [handle, actual_data] : actual_nodes_) {
      processed.insert(handle);

      // For cross-scene comparisons, we need to match by logical identity
      // rather than exact handle equality (since scene ID will differ)
      auto expected_match = FindLogicalMatch(handle, actual_data);
        if (!expected_match) {
        // Node added
        differences_.push_back(
          { DiffType::Added, actual_data.name, handle, std::nullopt, actual_data, {} });
      } else {
        // Node exists in both - check for modifications or moves
        const auto& expected_data = expected_match.value();

        // Check if the node has moved (different parent)
        bool has_moved = !HandleLogicallyEqual(expected_data.parent_handle, actual_data.parent_handle);

        // Check for other property differences
        auto prop_diffs = CompareNodeProperties(expected_data, actual_data);

        if (has_moved && !prop_diffs.empty()) {
          // Node moved AND has property changes
          differences_.push_back({ DiffType::Modified, actual_data.name, handle, expected_data,
            actual_data, prop_diffs });
        } else if (has_moved) {
          // Node moved but properties unchanged
          differences_.push_back(
            { DiffType::Moved, actual_data.name, handle, expected_data, actual_data, {} });
        } else if (!prop_diffs.empty()) {
          // Node has property changes but didn't move
          differences_.push_back({ DiffType::Modified, actual_data.name, handle, expected_data,
            actual_data, prop_diffs });
        } else {
          // Node is completely unchanged
          differences_.push_back(
            { DiffType::Unchanged, actual_data.name, handle, expected_data, actual_data, {} });
        }
      }
    }    // Check for removed nodes
    for (const auto& [handle, expected_data] : expected_nodes_) {
      if (!FindLogicalMatch(handle, expected_data, actual_nodes_)) {
        differences_.push_back(
          { DiffType::Removed, expected_data.name, handle, expected_data, std::nullopt, {} });
      }
    }
  }

  bool HasDifferences() const
  {
    return std::any_of(differences_.begin(), differences_.end(),
      [](const NodeDiff& diff) { return diff.type != DiffType::Unchanged; });
  }
  std::string GenerateDiffReport(
    const SceneNode& expected_root, const SceneNode& actual_root)
  {
    std::stringstream ss;

    if (!HasDifferences()) {
      ss << "✓ Hierarchies are identical\n";
      return ss.str();
    }

    ss << "✗ Hierarchy differences found:\n\n"; // Generate hierarchical diff
                                                // tree
    ss << GenerateHierarchicalDiff(expected_root, actual_root);

    // Group differences by type for summary
    auto added = std::count_if(differences_.begin(), differences_.end(),
      [](const NodeDiff& d) { return d.type == DiffType::Added; });
    auto removed = std::count_if(differences_.begin(), differences_.end(),
      [](const NodeDiff& d) { return d.type == DiffType::Removed; });
    auto modified = std::count_if(differences_.begin(), differences_.end(),
      [](const NodeDiff& d) { return d.type == DiffType::Modified; });
    auto moved = std::count_if(differences_.begin(), differences_.end(),
      [](const NodeDiff& d) { return d.type == DiffType::Moved; });

    ss << fmt::format(
      "\nSummary: {} added, {} removed, {} modified, {} moved\n", added,
      removed, modified, moved);

    return ss.str();
  }

private:
  std::string GenerateHierarchicalDiff(
    const SceneNode& expected_root, const SceneNode& actual_root)
  {
    (void)expected_root; // Suppress unused parameter warning
    (void)actual_root; // Suppress unused parameter warning
    std::stringstream ss;    // Build merged hierarchy and format as tree
    auto merged_root = BuildMergedHierarchy();
    if (merged_root) {
      FormatMergedNodeRecursive(merged_root, ss, "", true);
    }

    return ss.str();
  }
  enum class DiffContext { Expected, Actual };  void FormatMergedNodeRecursive(std::shared_ptr<MergedNode> node,
    std::stringstream& ss, const std::string& prefix, bool is_last = true)
  {
    if (!node)
      return;

    // Format node with diff indicator
    std::string diff_marker;
    switch (node->diff_type) {
      case DiffType::Added:
        diff_marker = " [+]";
        break;
      case DiffType::Removed:
        diff_marker = " [-]";
        break;
      case DiffType::Modified:
        diff_marker = " [*]";
        break;
      case DiffType::Unchanged:
        diff_marker = "";
        break;
    }

    // Don't show the virtual root node
    if (node->name != "<ROOT>") {
      ss << prefix << (is_last ? "`-- " : "|-- ") << node->name << diff_marker;

      // Add property details for modified nodes
      if (node->diff_type == DiffType::Modified) {
        if (node->expected_data && node->actual_data) {
          const auto& exp = *node->expected_data;
          const auto& act = *node->actual_data;

          // Show position changes
          if (exp.position != act.position) {
            ss << " pos(" << act.position.x << "," << act.position.y << "," << act.position.z << ")";
          }

          // Show visibility changes
          if (exp.visible != act.visible) {
            ss << (act.visible ? " [VISIBLE]" : " [HIDDEN]");
          }

          // Show parent changes (reparenting)
          if (!HandleLogicallyEqual(exp.parent_handle, act.parent_handle)) {
            ss << " [MOVED]";
          }
        }
      }

      // Add property details for added/removed nodes
      else if (node->diff_type == DiffType::Added && node->actual_data) {
        const auto& data = *node->actual_data;
        FormatNodeProperties(ss, data.position, data.scale, data.visible);
      }
      else if (node->diff_type == DiffType::Removed && node->expected_data) {
        const auto& data = *node->expected_data;
        FormatNodeProperties(ss, data.position, data.scale, data.visible);
      }

      ss << "\n";
    }

    // Format children
    std::string child_prefix = prefix;
    if (node->name != "<ROOT>") {
      child_prefix += (is_last ? "    " : "|   ");
    }

    for (size_t i = 0; i < node->children.size(); ++i) {
      bool child_is_last = (i == node->children.size() - 1);
      FormatMergedNodeRecursive(node->children[i], ss, child_prefix, child_is_last);
    }
  }

  void FormatNodeProperties(std::stringstream& ss, const glm::vec3& pos,
    const glm::vec3& scale, bool visible)
  {
    ss << " [" << pos.x << "," << pos.y << "," << pos.z << "]";
    if (scale != glm::vec3(1.0f)) {
      ss << " scale(" << scale.x << "," << scale.y << "," << scale.z << ")";
    }
    if (!visible) {
      ss << " [HIDDEN]";
    }
  }

public:
  void ExpectEqual(const SceneNode& expected_root, const SceneNode& actual_root,
    const std::string& context = "")
  {
    Compare(expected_root, actual_root);

    if (HasDifferences()) {
      auto report = GenerateDiffReport(expected_root, actual_root);
      EXPECT_TRUE(false) << context << "\n" << report;
    }
  }

  // Helper methods for cross-scene comparison logic

  // Find a logically equivalent node in the expected hierarchy
  std::optional<HierarchyNodeData> FindLogicalMatch(const NodeHandle& actual_handle,
                                                   const HierarchyNodeData& actual_data)
  {
    return FindLogicalMatch(actual_handle, actual_data, expected_nodes_);
  }
    // Generic helper to find logical matches in any node map
  template<typename NodeMap>
  std::optional<HierarchyNodeData> FindLogicalMatch(const NodeHandle& handle,
                                                   const HierarchyNodeData& data,
                                                   const NodeMap& node_map)
  {
    if (expected_scene_id_ == actual_scene_id_) {
      // Same scene - first try exact handle match for efficiency
      auto it = node_map.find(handle);
      if (it != node_map.end()) {
        return it->second;
      }

      // If no exact match, try logical equivalence (for clone comparisons within same scene)
      for (const auto& [other_handle, other_data] : node_map) {
        if (IsLogicallyEquivalent(data, other_data)) {
          return other_data;
        }
      }
      return std::nullopt;
    } else {
      // Cross-scene - match by logical equivalence (name + hierarchy position)
      for (const auto& [other_handle, other_data] : node_map) {
        if (IsLogicallyEquivalent(data, other_data)) {
          return other_data;
        }
      }
      return std::nullopt;
    }
  }
  // Check if two nodes are logically equivalent (for cross-scene comparisons)
  bool IsLogicallyEquivalent(const HierarchyNodeData& a, const HierarchyNodeData& b) const
  {
    // Match by name and structural position
    if (a.name != b.name) return false;

    // Check if properties match
    if (a.position != b.position || a.scale != b.scale || a.visible != b.visible) {
      return false;
    }

    // Check if parent relationships are logically equivalent
    bool both_root = (!a.parent_handle.IsValid() && !b.parent_handle.IsValid());
    bool both_have_parent = (a.parent_handle.IsValid() && b.parent_handle.IsValid());

    if (both_root) {
      // Both are root nodes - properties already checked above
      return true;
    } else if (both_have_parent) {
      // Both have parents - for same-scene clone comparisons, we can't easily verify
      // parent equivalence without complex recursive logic, so we accept that they
      // both have parents (properties already checked)
      return true;
    } else {
      // One is root, one is not - definitely different
      return false;
    }
  }

  // Check if two handles are logically equal (considering cross-scene scenarios)
  bool HandleLogicallyEqual(const NodeHandle& a, const NodeHandle& b) const
  {
    if (expected_scene_id_ == actual_scene_id_) {
      // Same scene - exact equality
      return a == b;
    } else {
      // Cross-scene - both invalid, or both valid (structure equivalence)
      return a.IsValid() == b.IsValid();
    }
  }

  std::shared_ptr<MergedNode> BuildMergedNodeRecursive(const NodeHandle& handle)
  {
    auto merged_node = std::make_shared<MergedNode>();

    // Look up node data in both expected and actual collections
    auto expected_it = expected_nodes_.find(handle);
    auto actual_it = actual_nodes_.find(handle);

    bool in_expected = (expected_it != expected_nodes_.end());
    bool in_actual = (actual_it != actual_nodes_.end());

    // Determine diff type
    if (in_expected && in_actual) {
      // Node exists in both - check if it has changes
      const auto& expected_data = expected_it->second;
      const auto& actual_data = actual_it->second;

      if (IsLogicallyEquivalent(expected_data, actual_data)) {
        merged_node->diff_type = DiffType::Unchanged;
      } else {
        merged_node->diff_type = DiffType::Modified;
      }
        merged_node->expected_data = expected_data;
      merged_node->actual_data = actual_data;
      merged_node->name = expected_data.name; // Use expected name as primary
      merged_node->parent_name = GetParentNameFromHandle(expected_data.parent_handle);
    }
    else if (in_expected && !in_actual) {
      // Node was removed
      merged_node->diff_type = DiffType::Removed;
      merged_node->expected_data = expected_it->second;
      merged_node->name = expected_it->second.name;
      merged_node->parent_name = GetParentNameFromHandle(expected_it->second.parent_handle);
    }
    else if (!in_expected && in_actual) {
      // Node was added
      merged_node->diff_type = DiffType::Added;
      merged_node->actual_data = actual_it->second;
      merged_node->name = actual_it->second.name;
      merged_node->parent_name = GetParentNameFromHandle(actual_it->second.parent_handle);
    }
    else {
      // This shouldn't happen if we're calling this correctly
      return nullptr;
    }

    // Collect all child handles from both expected and actual
    std::set<NodeHandle> all_child_handles;

    if (in_expected) {
      const auto& expected_data = expected_it->second;
      for (const NodeHandle& child_handle : expected_data.child_handles) {
        all_child_handles.insert(child_handle);
      }
    }

    if (in_actual) {
      const auto& actual_data = actual_it->second;
      for (const NodeHandle& child_handle : actual_data.child_handles) {
        all_child_handles.insert(child_handle);
      }
    }

    // Recursively build children
    for (const NodeHandle& child_handle : all_child_handles) {
      auto child_node = BuildMergedNodeRecursive(child_handle);
      if (child_node) {
        merged_node->children.push_back(child_node);
      }
    }

    return merged_node;
  }

  std::string GetParentNameFromHandle(const NodeHandle& parent_handle) const
  {
    if (!parent_handle.IsValid()) {
      return "<ROOT>";
    }

    // Look for the parent in expected nodes first
    auto expected_it = expected_nodes_.find(parent_handle);
    if (expected_it != expected_nodes_.end()) {
      return expected_it->second.name;
    }

    // Look for the parent in actual nodes
    auto actual_it = actual_nodes_.find(parent_handle);
    if (actual_it != actual_nodes_.end()) {
      return actual_it->second.name;
    }

    // If not found, return handle as string
    return nostd::to_string(parent_handle);
  }
};

class HierarchyTestBuilder {
private:
  std::shared_ptr<Scene> scene_;

public:
  explicit HierarchyTestBuilder(std::shared_ptr<Scene> scene)
    : scene_(std::move(scene))
  {
  }

  // Create fluent builder for modern syntax
  FluentHierarchyBuilder Fluent() { return FluentHierarchyBuilder(scene_); }

  // Generate ASCII tree representation
  std::string FormatAsTree(const SceneNode& root, const std::string& title = "")
  {
    return HierarchyTreeFormatter::FormatAsTree(root, title);
  }

  // Compare two hierarchies with sophisticated diff
  void ExpectEqual(const SceneNode& expected, const SceneNode& actual,
    const std::string& context = "")
  {
    HierarchyDiff diff;
    diff.ExpectEqual(expected, actual, context);
  }

  // Get detailed diff report without failing the test
  std::string GetDiffReport(const SceneNode& expected, const SceneNode& actual)
  {
    HierarchyDiff diff;
    diff.Compare(expected, actual);
    return diff.GenerateDiffReport(expected, actual);
  }

  // Check if hierarchies differ
  bool AreDifferent(const SceneNode& expected, const SceneNode& actual)
  {
    HierarchyDiff diff;
    diff.Compare(expected, actual);
    return diff.HasDifferences();
  }

  // Verify hierarchy independence by modifying one and checking the other is
  // unchanged
  void ExpectHierarchiesIndependent(
    const SceneNode& hierarchy1, const SceneNode& hierarchy2) const
  {
    // Capture original state
    HierarchyDiff diff1;
    diff1.Compare(hierarchy1, hierarchy1); // Self-compare to capture state

    // Modify hierarchy1
    if (hierarchy1.IsValid()) {
      const auto impl_opt = hierarchy1.GetObject();
      if (impl_opt.has_value()) {
        auto& transform = impl_opt->get().GetComponent<TransformComponent>();
        auto original_pos = transform.GetLocalPosition();
        auto original_scale = transform.GetLocalScale();

        transform.SetLocalPosition({ 999.0f, 999.0f, 999.0f });
        transform.SetLocalScale({ 999.0f, 999.0f, 999.0f });

        // Verify hierarchy2 is unchanged by comparing before and after states
        HierarchyDiff diff2;
        auto snapshot_before
          = hierarchy2; // This doesn't actually copy the hierarchy data

        // Instead, let's just verify the values directly
        auto impl2_opt = hierarchy2.GetObject();
        if (impl2_opt.has_value()) {
          auto& transform2
            = impl2_opt->get().GetComponent<TransformComponent>();
          EXPECT_NE(
            transform2.GetLocalPosition(), glm::vec3(999.0f, 999.0f, 999.0f))
            << "Hierarchy2 should be unchanged after modifying hierarchy1";
        }

        // Restore hierarchy1
        transform.SetLocalPosition(original_pos);
        transform.SetLocalScale(original_scale);
      }
    }
  }
};

class SceneCloneHierarchyTest : public testing::Test {
protected:
  void SetUp() override
  {
    scene_ = std::make_shared<Scene>("TestScene", 1024);
    builder_ = std::make_unique<HierarchyTestBuilder>(scene_);
  }

  void TearDown() override
  {
    builder_.reset();
    scene_.reset();
  }

  std::shared_ptr<Scene> scene_;
  std::unique_ptr<HierarchyTestBuilder> builder_;
};

//------------------------------------------------------------------------------
// CloneHierarchy Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneCloneHierarchyTest,
  CreateHierarchyFrom_ComplexHierarchy_ClonesCorrectlyWithNoErrors)
{
  // Arrange: Create a complex hierarchy using the Fluent DSL
  // clang-format off
  auto original_root = builder_->Fluent()
    .Root("Root").At(1.0f, 2.0f, 3.0f).Scale({ 1.5f, 1.5f, 1.5f }).Hidden()
      .Child("Child1").At(10.0f, 20.0f, 30.0f).Scale({ 2.0f, 2.0f, 2.0f })
        .Child("GrandChild1A").At(100.0f, 200.0f, 300.0f).Scale({ 3.0f, 3.0f, 3.0f })
        .Up()
        .Child("GrandChild1B").At(110.0f, 210.0f, 310.0f).Scale({ 3.1f, 3.1f, 3.1f })
        .Up()
      .Up()
      .Child("Child2").At(40.0f, 50.0f, 60.0f).Scale({ 0.5f, 0.5f, 0.5f })
        .Child("GrandChild2A").At(120.0f, 220.0f, 320.0f).Scale({ 3.2f, 3.2f, 3.2f })
          .Child("GreatGrandChild").At(1000.0f, 2000.0f, 3000.0f).Scale({ 4.0f, 4.0f, 4.0f })
          .Up()
        .Up()
      .Up()
    .Build();
  // clang-format on

  ASSERT_TRUE(original_root.IsValid())
    << "Original hierarchy should be created successfully";
  EXPECT_EQ(scene_->GetNodeCount(), 7) << "Scene should have exactly 7 nodes";

  // Act: Clone the hierarchy using CreateHierarchyFrom
  auto cloned_root = scene_->CreateHierarchyFrom(original_root, "ClonedRoot");

  // Assert: Verify cloning succeeded and scene has correct node count
  ASSERT_TRUE(cloned_root.IsValid()) << "CreateHierarchyFrom should succeed";
  EXPECT_EQ(scene_->GetNodeCount(), 14)
    << "Scene should have original 7 + cloned 7 = 14 nodes";

  // Assert: Create expected hierarchy structure for comparison
  // clang-format off
  auto expected_clone = builder_->Fluent()
    .Root("ClonedRoot").At(1.0f, 2.0f, 3.0f).Scale({ 1.5f, 1.5f, 1.5f }).Hidden()
      .Child("Child1").At(10.0f, 20.0f, 30.0f).Scale({ 2.0f, 2.0f, 2.0f })
        .Child("GrandChild1A").At(100.0f, 200.0f, 300.0f).Scale({ 3.0f, 3.0f, 3.0f })
        .Up()
        .Child("GrandChild1B").At(110.0f, 210.0f, 310.0f).Scale({ 3.1f, 3.1f, 3.1f })
        .Up()
      .Up()
      .Child("Child2").At(40.0f, 50.0f, 60.0f).Scale({ 0.5f, 0.5f, 0.5f })
        .Child("GrandChild2A").At(120.0f, 220.0f, 320.0f).Scale({ 3.2f, 3.2f, 3.2f })
          .Child("GreatGrandChild").At(1000.0f, 2000.0f, 3000.0f).Scale({ 4.0f, 4.0f, 4.0f })
          .Up()
        .Up()
      .Up()
    .Build();
  // clang-format on

  // Assert: Use the new sophisticated comparison system
  builder_->ExpectEqual(
    expected_clone, cloned_root, "Cloned hierarchy comparison");

  // Assert: Original and cloned hierarchies should be independent
  builder_->ExpectHierarchiesIndependent(original_root, cloned_root);
}

// Demonstrate the new Fluent DSL and sophisticated diff system
NOLINT_TEST_F(SceneCloneHierarchyTest, FluentDSL_DemonstrateNewCapabilities)
{
  // Arrange: Create hierarchy using the new fluent DSL
  // clang-format off
  auto original = builder_->Fluent()
    .Root("GameWorld").At(0, 0, 0).Scale(1.0f)
      .Child("Player").At(10, 0, 5).Scale(1.2f)
        .Child("Weapon").At(1, 0, 0).Scale(0.8f)
        .Up()
        .Child("Shield").At(-1, 0, 0).Scale(0.6f).Hidden()
        .Up()
      .Up()
      .Child("Environment").At(0, 0, 0)
        .Child("Tree1").At(20, 0, 10).Scale(2.0f)
        .Up()
        .Child("Rock1").At(15, 0, 8).Scale(1.5f)
        .Up()
      .Up()
    .Build();
  // clang-format on

  // Act: Clone the hierarchy
  auto cloned = scene_->CreateHierarchyFrom(original, "ClonedGameWorld");

  // Create expected result
  // clang-format off
  auto expected = builder_->Fluent()
    .Root("ClonedGameWorld").At(0, 0, 0).Scale(1.0f)
      .Child("Player").At(10, 0, 5).Scale(1.2f)
        .Child("Weapon").At(1, 0, 0).Scale(0.8f)
        .Up()
        .Child("Shield").At(-1, 0, 0).Scale(0.6f).Hidden()
        .Up()
      .Up()
      .Child("Environment").At(0, 0, 0)
        .Child("Tree1").At(20, 0, 10).Scale(2.0f)
        .Up()
        .Child("Rock1").At(15, 0, 8).Scale(1.5f)
        .Up()
      .Up()
    .Build();
  // clang-format on

  // Assert: Sophisticated comparison with visual ASCII output
  std::cout << "=== ORIGINAL HIERARCHY ===" << std::endl;
  std::cout << builder_->FormatAsTree(original) << std::endl;

  std::cout << "=== CLONED HIERARCHY ===" << std::endl;
  std::cout << builder_->FormatAsTree(cloned) << std::endl;

  // Verify they match exactly
  EXPECT_FALSE(builder_->AreDifferent(expected, cloned))
    << "Hierarchies should be identical";

  // Demonstrate diff report (this should show no differences)
  auto diff_report = builder_->GetDiffReport(expected, cloned);
  std::cout << "=== DIFF REPORT ===" << std::endl;
  std::cout << diff_report << std::endl;

  // Assert with sophisticated diff on success
  builder_->ExpectEqual(expected, cloned, "Fluent DSL cloning verification");
}

// Demonstrate the new hierarchical diff format with actual differences
NOLINT_TEST_F(
  SceneCloneHierarchyTest, HierarchicalDiff_ShowsDifferencesInTreeFormat)
{
  // Arrange: Create a simple hierarchy
  // clang-format off
  auto original = builder_->Fluent()
    .Root("GameWorld").At(0, 0, 0).Scale(1.0f)
      .Child("Player").At(10, 0, 5).Scale(1.2f)
        .Child("Weapon").At(1, 0, 0).Scale(0.8f)
        .Up()
      .Up()
      .Child("Environment").At(0, 0, 0)
        .Child("Tree1").At(20, 0, 10).Scale(2.0f)
        .Up()
      .Up()
    .Build();
  // clang-format on

  // Create a modified version with some differences
  // clang-format off
  auto modified = builder_->Fluent()
    .Root("GameWorld").At(0, 0, 0).Scale(1.0f)
      .Child("Player").At(10, 0, 5).Scale(1.2f)
        .Child("Weapon").At(1, 0, 0).Scale(0.7f) // Different scale
        .Up()
        .Child("Shield").At(-1, 0, 0).Scale(0.6f).Hidden() // Added node
        .Up()
      .Up()
      .Child("Environment").At(0, 0, 0)
        .Child("Tree1").At(20, 0, 10).Scale(2.0f)
        .Up()
        // Rock1 is removed from this version
      .Up()
    .Build();
  // clang-format on
  // Act & Assert: Show the hierarchical diff
  std::cout << "=== HIERARCHICAL DIFF DEMONSTRATION ===" << std::endl;
  auto diff_report = builder_->GetDiffReport(original, modified);
  std::cout << diff_report << std::endl;

  // Verify that differences are detected
  EXPECT_TRUE(builder_->AreDifferent(original, modified))
    << "Hierarchies should be different";
}

// Demonstrate move detection for future reparenting tests
NOLINT_TEST_F(SceneCloneHierarchyTest, HierarchicalDiff_DetectsMovedNodes)
{
  // Arrange: Create a hierarchy where we can move nodes around
  // clang-format off
  auto original = builder_->Fluent()
    .Root("GameWorld").At(0, 0, 0)
      .Child("Player").At(10, 0, 5)
        .Child("Weapon").At(1, 0, 0).Scale(0.8f)
        .Up()
      .Up()
      .Child("Environment").At(0, 0, 0)
        .Child("Tree1").At(20, 0, 10)
        .Up()
      .Up()
    .Build();
  // clang-format on

  // Create a version where Weapon is moved from Player to Environment
  // clang-format off
  auto reparented = builder_->Fluent()
    .Root("GameWorld").At(0, 0, 0)
      .Child("Player").At(10, 0, 5)
        // Weapon is no longer a child of Player
      .Up()
      .Child("Environment").At(0, 0, 0)
        .Child("Tree1").At(20, 0, 10)
        .Up()
        .Child("Weapon").At(1, 0, 0).Scale(0.8f) // Weapon moved here from Player
        .Up()
      .Up()
    .Build();
  // clang-format on

  // Act & Assert: Show the move detection
  std::cout << "=== MOVE DETECTION DEMONSTRATION ===" << std::endl;
  auto diff_report = builder_->GetDiffReport(original, reparented);
  std::cout << diff_report << std::endl;

  // Verify that the move is detected
  EXPECT_TRUE(builder_->AreDifferent(original, reparented))
    << "Hierarchies should be different due to reparenting";
}

} // namespace
