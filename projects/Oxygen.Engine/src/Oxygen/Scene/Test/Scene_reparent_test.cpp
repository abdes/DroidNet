//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ResourceHandle.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/detail/TransformComponent.h>

#include <fmt/format.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using oxygen::ResourceHandle;
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
struct HierarchyNodeData {
  std::string name;
  std::string parent_name; // Track parent for move detection
  glm::vec3 position = { 0.0f, 0.0f, 0.0f };
  glm::vec3 scale = { 1.0f, 1.0f, 1.0f };
  bool visible = true;
  std::unordered_set<std::string>
    child_names; // Use unordered_set to ignore order

  bool operator==(const HierarchyNodeData& other) const
  {
    return name == other.name && parent_name == other.parent_name
      && position == other.position && scale == other.scale
      && visible == other.visible && child_names == other.child_names;
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
  std::string node_name;
  std::string parent_name;
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

    // Process children (sort by ResourceHandle for consistent output)
    std::vector<SceneNode> children;
    auto child_opt = node.GetFirstChild();
    while (child_opt.has_value()) {
      children.push_back(child_opt.value());
      child_opt = child_opt->GetNextSibling();
    }

    // Sort children by ResourceHandle for deterministic display order
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
  std::unordered_map<std::string, HierarchyNodeData> expected_nodes_;
  std::unordered_map<std::string, HierarchyNodeData> actual_nodes_;
  std::vector<NodeDiff> differences_;

  // Structure to represent merged hierarchy for diff display
  struct MergedNode {
    std::string name;
    std::string parent_name;
    DiffType diff_type = DiffType::Unchanged;
    std::optional<HierarchyNodeData> expected_data;
    std::optional<HierarchyNodeData> actual_data;
    std::vector<std::shared_ptr<MergedNode>> children;
  };
  std::shared_ptr<MergedNode> BuildMergedHierarchy()
  {
    // Create a map of all nodes (both expected and actual)
    std::unordered_map<std::string, std::shared_ptr<MergedNode>> all_nodes;
    std::shared_ptr<MergedNode> root_node = nullptr;

    // First pass: create all nodes from differences
    for (const auto& diff : differences_) {
      if (all_nodes.find(diff.node_name) == all_nodes.end()) {
        auto merged_node = std::make_shared<MergedNode>();
        merged_node->name = diff.node_name;
        merged_node->diff_type = diff.type;
        merged_node->expected_data = diff.expected_data;
        merged_node->actual_data = diff.actual_data;
        all_nodes[diff.node_name] = merged_node;
      }
    }

    // For moved nodes, we need to create "shadow" nodes to show them in both
    // locations
    std::unordered_map<std::string, std::shared_ptr<MergedNode>> moved_shadows;
    for (const auto& diff : differences_) {
      if (diff.type == DiffType::Moved) {
        // Create a shadow node for the old location (deletion)
        auto old_shadow = std::make_shared<MergedNode>();
        old_shadow->name = diff.node_name + "_OLD_LOCATION";
        old_shadow->diff_type = DiffType::Moved;
        old_shadow->expected_data = diff.expected_data;
        old_shadow->actual_data = diff.actual_data;
        moved_shadows[diff.node_name + "_OLD"] = old_shadow;

        // The main node will be placed in the new location (addition)
        all_nodes[diff.node_name]->diff_type = DiffType::Moved;
      }
    } // Add moved shadow nodes to all_nodes
    for (const auto& [key, shadow] : moved_shadows) {
      all_nodes[key] = shadow;
    }

    // Second pass: build parent-child relationships using ACTUAL hierarchy
    // (post-move state)
    for (const auto& [name, actual_data] : actual_nodes_) {
      for (const auto& [parent_name, parent_data] : actual_nodes_) {
        if (parent_data.child_names.find(name)
          != parent_data.child_names.end()) {
          if (all_nodes.find(name) != all_nodes.end()
            && all_nodes.find(parent_name) != all_nodes.end()) {
            all_nodes[name]->parent_name = parent_name;
            all_nodes[parent_name]->children.push_back(all_nodes[name]);
          }
          break;
        }
      }
    }

    // Third pass: place moved node shadows in their original locations
    for (const auto& diff : differences_) {
      if (diff.type == DiffType::Moved && diff.expected_data.has_value()) {
        std::string shadow_key = diff.node_name + "_OLD";
        if (moved_shadows.find(shadow_key) != moved_shadows.end()) {
          auto shadow = moved_shadows[shadow_key];

          // Find the expected parent and add shadow there
          std::string expected_parent = diff.expected_data->parent_name;
          if (expected_parent.empty()) {
            // Was a root node in expected
            if (!root_node || root_node->name != expected_parent) {
              // Need to find the expected root
              for (const auto& [node_name, node_data] : expected_nodes_) {
                if (node_data.parent_name.empty()) {
                  expected_parent = node_name;
                  break;
                }
              }
            }
          }

          if (all_nodes.find(expected_parent) != all_nodes.end()) {
            shadow->parent_name = expected_parent;
            all_nodes[expected_parent]->children.push_back(shadow);
          }
        }
      }
    }

    // Handle nodes that only exist in expected (removed nodes)
    for (const auto& [name, expected_data] : expected_nodes_) {
      if (actual_nodes_.find(name) == actual_nodes_.end()) {
        // This node was removed - place it in expected hierarchy structure
        for (const auto& [parent_name, parent_data] : expected_nodes_) {
          if (parent_data.child_names.find(name)
            != parent_data.child_names.end()) {
            if (all_nodes.find(name) != all_nodes.end()
              && all_nodes.find(parent_name) != all_nodes.end()) {
              all_nodes[name]->parent_name = parent_name;
              all_nodes[parent_name]->children.push_back(all_nodes[name]);
            }
            break;
          }
        }
      }
    }

    // Find root node (node with no parent)
    for (const auto& [name, node] : all_nodes) {
      if (node->parent_name.empty()) {
        root_node = node;
        break;
      }
    }
    return root_node;
  }

  HierarchyNodeData ExtractNodeData(
    const SceneNode& node, const std::string& parent_name = "")
  {
    HierarchyNodeData data;
    auto impl_opt = node.GetObject();
    if (!impl_opt.has_value())
      return data;

    const auto& impl = impl_opt->get();
    const auto& transform = impl.GetComponent<TransformComponent>();
    const auto& flags = impl.GetFlags();

    data.name = impl.GetName();
    data.position = transform.GetLocalPosition();
    data.scale = transform.GetLocalScale();
    data.visible = flags.GetEffectiveValue(SceneNodeFlags::kVisible);

    // Store the parent name for move detection
    data.parent_name = parent_name;

    // Collect children names (order-independent)
    auto child_opt = node.GetFirstChild();
    while (child_opt.has_value()) {
      auto child_impl_opt = child_opt->GetObject();
      if (child_impl_opt.has_value()) {
        data.child_names.insert(std::string(child_impl_opt->get().GetName()));
      }
      child_opt = child_opt->GetNextSibling();
    }

    return data;
  }

  void CollectNodesRecursive(const SceneNode& node,
    std::unordered_map<std::string, HierarchyNodeData>& node_map,
    const std::string& parent_name = "")
  {
    auto data = ExtractNodeData(node, parent_name);
    node_map[data.name] = data;

    auto child_opt = node.GetFirstChild();
    while (child_opt.has_value()) {
      CollectNodesRecursive(child_opt.value(), node_map, data.name);
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

    if (expected.child_names != actual.child_names) {
      // For better error reporting, show which children differ
      std::vector<std::string> expected_sorted(
        expected.child_names.begin(), expected.child_names.end());
      std::vector<std::string> actual_sorted(
        actual.child_names.begin(), actual.child_names.end());
      std::sort(expected_sorted.begin(), expected_sorted.end());
      std::sort(actual_sorted.begin(), actual_sorted.end());

      std::string expected_str, actual_str;
      for (size_t i = 0; i < expected_sorted.size(); ++i) {
        if (i > 0)
          expected_str += ", ";
        expected_str += expected_sorted[i];
      }
      for (size_t i = 0; i < actual_sorted.size(); ++i) {
        if (i > 0)
          actual_str += ", ";
        actual_str += actual_sorted[i];
      }

      diffs.push_back(fmt::format("Children differ: expected [{}] but was [{}]",
        expected_str, actual_str));
    }

    return diffs;
  }

public:
  void Compare(const SceneNode& expected_root, const SceneNode& actual_root)
  {
    expected_nodes_.clear();
    actual_nodes_.clear();
    differences_.clear();

    // Collect all nodes from both hierarchies
    CollectNodesRecursive(expected_root, expected_nodes_);
    CollectNodesRecursive(actual_root, actual_nodes_);

    // Find differences
    std::unordered_set<std::string> processed;

    // Check for added/modified nodes
    for (const auto& [name, actual_data] : actual_nodes_) {
      processed.insert(name);

      auto it = expected_nodes_.find(name);
      if (it == expected_nodes_.end()) {
        // Node added
        differences_.push_back(
          { DiffType::Added, name, "", std::nullopt, actual_data, {} });
      } else {
        // Node exists in both - check for modifications or moves
        const auto& expected_data = it->second;

        // Check if the node has moved (different parent)
        bool has_moved = expected_data.parent_name != actual_data.parent_name;

        // Check for other property differences
        auto prop_diffs = CompareNodeProperties(expected_data, actual_data);

        if (has_moved && !prop_diffs.empty()) {
          // Node moved AND has property changes - treat as modified for
          // simplicity
          differences_.push_back({ DiffType::Modified, name, "", expected_data,
            actual_data, prop_diffs });
        } else if (has_moved) {
          // Node moved but properties unchanged
          differences_.push_back(
            { DiffType::Moved, name, "", expected_data, actual_data, {} });
        } else if (!prop_diffs.empty()) {
          // Node has property changes but didn't move
          differences_.push_back({ DiffType::Modified, name, "", expected_data,
            actual_data, prop_diffs });
        } else {
          // Node is completely unchanged
          differences_.push_back(
            { DiffType::Unchanged, name, "", expected_data, actual_data, {} });
        }
      }
    }

    // Check for removed nodes
    for (const auto& [name, expected_data] : expected_nodes_) {
      if (processed.find(name) == processed.end()) {
        differences_.push_back(
          { DiffType::Removed, name, "", expected_data, std::nullopt, {} });
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
    std::stringstream ss;

    // Build merged hierarchy and format as tree
    auto merged_root = BuildMergedHierarchy();
    if (merged_root) {
      FormatMergedNodeRecursive(merged_root, ss, "");
    }

    return ss.str();
  }
  enum class DiffContext { Expected, Actual };

  void FormatMergedNodeRecursive(std::shared_ptr<MergedNode> node,
    std::stringstream& ss, const std::string& prefix)
  {
    if (!node)
      return;

    // Handle shadow nodes for moved items (old location)
    if (node->name.find("_OLD_LOCATION") != std::string::npos) {
      // Extract original name
      std::string original_name
        = node->name.substr(0, node->name.find("_OLD_LOCATION"));
      ss << "- " << prefix << original_name;
      if (node->expected_data.has_value()) {
        FormatNodeProperties(ss, node->expected_data->position,
          node->expected_data->scale, node->expected_data->visible);
      }
      ss << " (moved to "
         << (node->actual_data.has_value()
                  && !node->actual_data->parent_name.empty()
                ? node->actual_data->parent_name
                : "root")
         << ")\n";
      return; // Don't process children for shadow nodes
    }

    // For modified nodes, show both expected (removed) and actual (added)
    // versions
    if (node->diff_type == DiffType::Modified && node->expected_data.has_value()
      && node->actual_data.has_value()) {

      // Show the expected version (what was removed)
      ss << "- " << prefix << node->expected_data->name;
      FormatNodeProperties(ss, node->expected_data->position,
        node->expected_data->scale, node->expected_data->visible);
      ss << "\n";

      // Show the actual version (what was added)
      ss << "+ " << prefix << node->actual_data->name;
      FormatNodeProperties(ss, node->actual_data->position,
        node->actual_data->scale, node->actual_data->visible);
      ss << "\n";
    } else if (node->diff_type == DiffType::Moved
      && node->expected_data.has_value() && node->actual_data.has_value()) {

      // For moved nodes at new location, show addition with "moved from"
      // comment
      ss << "+ " << prefix << node->actual_data->name;
      FormatNodeProperties(ss, node->actual_data->position,
        node->actual_data->scale, node->actual_data->visible);
      ss << " (moved from "
         << (node->expected_data->parent_name.empty()
                ? "root"
                : node->expected_data->parent_name)
         << ")\n";
    } else {
      // Standard node line with diff symbol
      std::string diff_symbol = " ";
      switch (node->diff_type) {
      case DiffType::Added:
        diff_symbol = "+ ";
        break;
      case DiffType::Removed:
        diff_symbol = "- ";
        break;
      case DiffType::Moved:
        diff_symbol = "~ ";
        break; // Use ~ for moved nodes (fallback)
      default:
        diff_symbol = "  ";
        break;
      }

      ss << diff_symbol << prefix << node->name;

      // Use data from whichever version exists
      if (node->actual_data.has_value()) {
        FormatNodeProperties(ss, node->actual_data->position,
          node->actual_data->scale, node->actual_data->visible);
      } else if (node->expected_data.has_value()) {
        FormatNodeProperties(ss, node->expected_data->position,
          node->expected_data->scale, node->expected_data->visible);
      }
      ss << "\n";
    }

    // Sort children by name for consistent output
    std::sort(node->children.begin(), node->children.end(),
      [](const std::shared_ptr<MergedNode>& a,
        const std::shared_ptr<MergedNode>& b) { return a->name < b->name; });

    // Process children with simple indentation
    for (size_t i = 0; i < node->children.size(); ++i) {
      std::string child_prefix = prefix + "  "; // Simple 2-space indentation
      FormatMergedNodeRecursive(node->children[i], ss, child_prefix);
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
