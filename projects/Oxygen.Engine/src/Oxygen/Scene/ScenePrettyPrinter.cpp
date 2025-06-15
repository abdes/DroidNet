//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>
#include <ranges>
#include <sstream>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/ScenePrettyPrinter.h>
#include <Oxygen/Scene/SceneTraversal.h>

namespace oxygen::scene {

namespace {

  //! Character sets for tree visualization
  struct TreeChars {
    const char* branch; // ├──
    const char* last_child; // └──
    const char* continuation; // │
    const char* spacing; // (spaces)
  };

  constexpr TreeChars kAsciiChars = {
    .branch = "|-- ", // branch
    .last_child = "`-- ", // last child
    .continuation = "|   ", // continuation
    .spacing = "    " // no continuation
  };

  constexpr TreeChars kUnicodeChars = {
    .branch = "├── ", // branch (U+251C, U+2500, U+2500, space)
    .last_child = "└── ", // last child (U+2514, U+2500, U+2500, space)
    .continuation = "│   ", // continuation (U+2502, space, space, space)
    .spacing = "    " // no continuation
  };

  //! Get character set for tree drawing
  auto GetTreeChars(const CharacterSet charset) -> const TreeChars&
  {
    switch (charset) {
    case CharacterSet::kAscii:
      return kAsciiChars;
    case CharacterSet::kUnicode:
      return kUnicodeChars;
    }
    return kAsciiChars;
  }

  //! Format transform information using SceneNode API
  auto FormatTransform(SceneNode& node, VerbosityLevel verbosity) -> std::string
  {
    if (verbosity == VerbosityLevel::kNone) {
      return "";
    }

    auto transform = node.GetTransform();

    switch (verbosity) {
    case VerbosityLevel::kCompact: {
      std::vector<std::string> parts;

      // Check for non-default transform values
      if (auto pos = transform.GetLocalPosition();
        pos && (pos->x != 0.0f || pos->y != 0.0f || pos->z != 0.0f)) {
        parts.emplace_back("T");
      }

      if (auto rot = transform.GetLocalRotation(); rot) {
        // Check if rotation is not identity quaternion (0,0,0,1)
        if (rot->x != 0.0f || rot->y != 0.0f || rot->z != 0.0f
          || rot->w != 1.0f) {
          parts.emplace_back("R");
        }
      }

      if (auto scale = transform.GetLocalScale();
        scale && (scale->x != 1.0f || scale->y != 1.0f || scale->z != 1.0f)) {
        parts.emplace_back("S");
      }

      if (parts.empty()) {
        return "";
      }

      std::ostringstream oss;
      oss << " [";
      for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0)
          oss << ",";
        oss << parts[i];
      }
      oss << "]";
      return oss.str();
    }

    case VerbosityLevel::kDetailed: {
      std::ostringstream oss;
      oss << " [";

      if (auto pos = transform.GetLocalPosition()) {
        oss << "pos:(" << pos->x << "," << pos->y << "," << pos->z << ") ";
      }

      if (auto rot = transform.GetLocalRotation()) {
        oss << "rot:(" << rot->x << "," << rot->y << "," << rot->z << ","
            << rot->w << ") ";
      }

      if (auto scale = transform.GetLocalScale()) {
        oss << "scale:(" << scale->x << "," << scale->y << "," << scale->z
            << ")";
      }

      oss << "]";
      return oss.str();
    }

    case VerbosityLevel::kNone:
      return "";
    }

    return "";
  }

  //! Format flags information using SceneFlags ranges API
  auto FormatFlags(SceneNode& node, VerbosityLevel verbosity) -> std::string
  {
    if (verbosity == VerbosityLevel::kNone) {
      return "";
    }

    auto flags_opt = node.GetFlags();
    if (!flags_opt) {
      return "";
    }

    const auto& flags = flags_opt->get();
    std::ostringstream oss;

    // Use the ranges API to get flags with effective value = true
    auto true_flags = flags.effective_true_flags();
    bool first = true;

    for (auto flag : true_flags) {
      if (first) {
        oss << " [flags:";
        first = false;
      } else {
        oss << ",";
      }
      oss << nostd::to_string(flag);
    }

    if (!first) {
      oss << "]";
    }

    return oss.str();
  } //! Format node line using engine APIs
  auto FormatNodeLine(SceneNode& node, const detail::PrintOptions& options,
    std::ptrdiff_t depth) -> std::string
  {
    std::ostringstream oss;

    // Choose output format based on verbosity level
    if (options.verbosity == VerbosityLevel::kCompact) {
      // In compact mode, show only the node name
      oss << node.GetName();
    } else { // In detailed mode (or none), use SceneNode's to_string for full
             // handle
      // and name formatting
      oss << to_string(node);
    }

    // Add depth info for detailed output
    if (options.verbosity == VerbosityLevel::kDetailed) {
      oss << " (d:" << depth << ")";
    }

    // Add transform information
    if (options.show_transforms) {
      oss << FormatTransform(node, options.verbosity);
    }

    // Add flags information using the ranges API
    if (options.show_flags) {
      oss << FormatFlags(node, options.verbosity);
    }

    return oss.str();
  } //! Visitor for breadth-first scene traversal
  class ScenePrintVisitor {
  public:
    explicit ScenePrintVisitor(const detail::PrintOptions& options)
      : options_(options)
    {
    }

    auto operator()(const ConstVisitedNode& visited_node, bool /*dry_run*/)
      -> VisitResult
    {
      // Check depth limit
      if (options_.max_depth >= 0
        && std::cmp_greater(visited_node.depth, options_.max_depth)) {
        return VisitResult::kSkipSubtree;
      }

      // Create SceneNode wrapper for the visited node
      SceneNode node(scene_weak_, visited_node.handle);

      if (!node.IsValid()) {
        return VisitResult::kContinue;
      }

      nodes_with_depth_.emplace_back(std::move(node), visited_node.depth);

      return VisitResult::kContinue;
    }

    //! Format the collected nodes into proper tree structure
    auto FormatCollectedNodes() -> std::vector<std::string>
    {
      std::vector<std::string> output_lines;

      if (nodes_with_depth_.empty()) {
        return output_lines;
      }

      const auto& chars = GetTreeChars(options_.charset);

      // Since we collected in breadth-first order, we need to determine
      // tree structure relationships for proper formatting
      std::vector<bool> has_more_at_depth(
        10, false); // Start with reasonable size

      for (std::size_t i = 0; i < nodes_with_depth_.size(); ++i) {
        auto& [node, depth] = nodes_with_depth_[i];
        std::ptrdiff_t depth_val = depth;

        // Ensure we have enough space in our tracking vector
        if (std::cmp_less(has_more_at_depth.size(),
              static_cast<std::size_t>(depth_val + 1))) {
          has_more_at_depth.resize(
            static_cast<std::size_t>(depth_val + 1), false);
        }

        // Determine if there are more siblings at this depth after this node
        bool has_more_siblings = false;
        for (std::size_t j = i + 1; j < nodes_with_depth_.size(); ++j) {
          if (nodes_with_depth_[j].second == depth_val) {
            has_more_siblings = true;
            break;
          } else if (std::cmp_less(nodes_with_depth_[j].second, depth_val)) {
            // We've gone back up the tree, no more siblings at this level
            break;
          }
        }
        has_more_at_depth[static_cast<std::size_t>(depth_val)]
          = has_more_siblings;

        // Build prefix based on ancestry
        std::ostringstream prefix;
        for (std::size_t d = 0; d < static_cast<std::size_t>(depth_val); ++d) {
          if (std::cmp_less(d, has_more_at_depth.size())
            && has_more_at_depth[d]) {
            prefix << chars.continuation;
          } else {
            prefix << chars.spacing;
          }
        }

        // Add branch character for non-root nodes
        if (depth_val > 0) {
          prefix << (has_more_siblings ? chars.branch : chars.last_child);
        }

        // Format the complete line
        std::string line
          = prefix.str() + FormatNodeLine(node, options_, depth_val);
        output_lines.push_back(std::move(line));
      }

      return output_lines;
    }

    void SetScene(std::weak_ptr<const Scene> scene)
    {
      scene_weak_ = std::move(scene);
    }

  private:
    const detail::PrintOptions& options_;
    std::weak_ptr<const Scene> scene_weak_;
    std::vector<std::pair<SceneNode, std::ptrdiff_t>> nodes_with_depth_;
  };

} // anonymous namespace

//! Core implementation using SceneTraversal for breadth-first traversal
auto detail::FormatSceneTreeCore(const Scene& scene,
  const PrintOptions& options, const std::vector<SceneNode>& roots)
  -> std::vector<std::string>
{
  // Create traversal instance
  const auto scene_ptr = scene.shared_from_this();
  const SceneTraversal traversal(
    std::const_pointer_cast<const Scene>(scene_ptr));

  // Create visitor
  ScenePrintVisitor visitor(options);
  visitor.SetScene(scene_ptr); // Perform depth-first traversal for proper tree
                               // structure display
  [[maybe_unused]] auto result = traversal.TraverseHierarchies(
    std::span(roots.data(), roots.size()), visitor,
    TraversalOrder::kPreOrder // Pre-order depth-first for proper tree display!
  );

  // Format the collected nodes
  return visitor.FormatCollectedNodes();
}

} // namespace oxygen::scene
