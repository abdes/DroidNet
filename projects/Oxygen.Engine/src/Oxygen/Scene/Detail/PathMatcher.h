//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cctype>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <utility> // for std::cmp_greater
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Scene/Detail/PathParser.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene::detail::query {

//! Concept defining requirements for nodes that can be used in path matching.
/*!
 Any type that provides a way to access its name as a string-like object
 and its hierarchical depth can be used with PathMatcher. This eliminates
 the need for adapter types.

 The type must support ADL-compatible GetNodeName() and GetDepth() functions.
*/
template <typename T>
concept TraversalNode = requires(const T& node) {
  // Must provide access to node name as string-like type via ADL
  { GetNodeName(node) } -> std::convertible_to<std::string_view>;
  // Must provide access to hierarchical depth via ADL (depth is always
  // non-negative)
  {
    GetDepth(node)
  } -> std::convertible_to<std::vector<std::string>::size_type>;
};

//! Result of streaming pattern matching against a single node.
/*!
 Indicates the current match status when a node is tested against the pattern
 during depth-first traversal. Used by caller to decide traversal continuation.
*/
enum class MatchResult {
  //! Current path doesn't match the pattern
  kNoMatch,
  //! Pattern partially matched, continue deeper in this subtree
  kPartialMatch,
  //! Pattern completely matched - this node is a target!
  kCompleteMatch
};

//! Concept defining requirements for string matching strategies.
/*!
 String matchers must be callable with two string_view parameters and return
 a boolean result indicating match success. Must be nothrow invocable.
*/
template <typename T>
concept StringMatcher = requires(
  T matcher, std::string_view a, std::string_view b) {
  { matcher(a, b) } -> std::same_as<bool>;
  requires std::is_nothrow_invocable_v<T, std::string_view, std::string_view>;
};

//! Case-sensitive string matcher for exact comparisons.
struct CaseSensitiveMatcher {
  //! Performs case-sensitive string comparison.
  /*!
   @param a First string to compare
   @param b Second string to compare
   @return True if strings are identical
  */
  [[nodiscard]] constexpr auto operator()(
    std::string_view a, std::string_view b) const noexcept -> bool
  {
    return a == b;
  }
};

//! Case-insensitive string matcher using locale-independent comparison.
struct CaseInsensitiveMatcher {
  //! Performs case-insensitive string comparison.
  /*!
   @param a First string to compare
   @param b Second string to compare
   @return True if strings match ignoring case differences
  */
  [[nodiscard]] auto operator()(
    std::string_view a, std::string_view b) const noexcept -> bool
  {
    return a.size() == b.size()
      && std::equal(
        a.begin(), a.end(), b.begin(), b.end(), [](char ca, char cb) {
          return std::tolower(static_cast<unsigned char>(ca))
            == std::tolower(static_cast<unsigned char>(cb));
        });
  }
};

//! Stack-allocatable state for streaming pattern matching during DFS traversal.
/*!
 Maintains the current path from root to current node and tracks depth changes
 to handle backtracking during pre-order depth-first scene traversal.
 All state is explicitly managed by the caller for thread safety.
*/
struct PatternMatchState {
  //! Current path from root to current node (built during traversal)
  std::vector<std::string> path_stack;
  //! Last seen depth to detect backtracking (-1 means no previous depth)
  std::vector<std::string>::difference_type last_depth = -1;

  //! Adjusts path stack when depth decreases (backtracking detected)
  auto AdjustForDepth(std::vector<std::string>::size_type new_depth) -> void
  {
    // Pop stack back to the appropriate depth level
    while (path_stack.size() > new_depth) {
      path_stack.pop_back();
    }
  }

  //! Clears all state for reuse
  auto Reset() -> void
  {
    path_stack.clear();
    last_depth = -1;
  }
};

//! Stateful pattern matching engine for hierarchical node traversal sequences.
/*!
 Provides incremental matching of TraversalNode streams against pre-compiled
 path patterns during scene graph traversal. Designed for high-performance
 scenarios where the same pattern is evaluated against many node sequences
 in a single operation.

 ### Key Features

 - **Incremental matching**: Evaluates one node at a time during traversal
 - **Zero-allocation operation**: Uses stack-based PatternMatchState
 - **Configurable string comparison**: Template-based matching strategies
 - **Thread-safe design**: All state externally managed by caller

 @tparam MatcherType String comparison strategy ### Typical Usage Pattern

 ```cpp
 PathMatcher matcher(parsed_pattern);
 PatternMatchState state;

 for (const auto& node : scene_dfs_traversal) {
   auto result = matcher.Match(node, state);

   switch (result) {
     case MatchResult::kCompleteMatch:
       // Found target! Collect/return this node
       ProcessMatch(node);
       break;

     case MatchResult::kPartialMatch:
       // Keep going deeper in this subtree
       break;

     case MatchResult::kNoMatch:
       // This path doesn't match, continue to next nodes
       break;
   }
 }
 ```

 ### Performance Characteristics

 - **Match operation**: O(1) per node evaluation
 - **Memory usage**: O(1) - no heap allocation during matching
 - **State management**: Caller-controlled for optimal thread safety

 @see PathParser for pattern syntax, parsing, and validation
 @see StringMatcher for matcher type requirements
 @see PatternMatchState for state management details
*/
template <StringMatcher MatcherType = CaseSensitiveMatcher> class PathMatcher {
public:
  //! Constructs matcher from pre-parsed path pattern.
  /*!
   @param pattern Pre-parsed and validated path pattern
   @param matcher String matching strategy instance
   @throws std::invalid_argument if pattern is invalid
  */
  explicit PathMatcher(const ParsedPath& pattern, MatcherType matcher = {})
    : pattern_(pattern)
    , matcher_(matcher)
  {
    if (!pattern_.IsValid()) {
      throw std::invalid_argument("Invalid pattern: "
        + (pattern_.error_info ? pattern_.error_info->error_message
                               : "unknown error"));
    }
  }

  //! Constructs matcher from path string, parsing internally.
  /*!
   @param path_string Path pattern string to parse and compile
   @param matcher String matching strategy instance
   @throws std::invalid_argument if path string cannot be parsed
  */
  explicit PathMatcher(std::string_view path_string, MatcherType matcher = {})
    : pattern_(ParsePath(path_string))
    , matcher_(matcher)
  {
    if (!pattern_.IsValid()) {
      throw std::invalid_argument("Invalid pattern: "
        + (pattern_.error_info ? pattern_.error_info->error_message
                               : "unknown error"));
    }
  }

  ~PathMatcher() noexcept = default;

  OXYGEN_DEFAULT_COPYABLE(PathMatcher)
  OXYGEN_DEFAULT_MOVABLE(PathMatcher) //! Evaluates a single node against the
                                      //! pattern in streaming mode.
  /*!
   @tparam NodeType Any type satisfying TraversalNode concept
   @param node Node to evaluate (must provide GetNodeName and GetDepth via ADL)
   @param state Matching state that tracks the current path and depth
   @return MatchResult indicating whether to continue traversal or if match
   found

   Updates state.path_stack to reflect the current path from root to node.
   Handles depth-based backtracking automatically by detecting depth decreases.
  */
  template <TraversalNode NodeType>
  [[nodiscard]] auto Match(const NodeType& node, PatternMatchState& state) const
    -> MatchResult
  {
    auto node_name = GetNodeName(node); // ADL lookup
    auto node_depth = GetDepth(node); // ADL lookup (returns size_type)

    // Handle depth changes (backtracking in DFS)
    // Use std::cmp_less_equal for safe signed/unsigned comparison
    if (state.last_depth != -1
      && std::cmp_less_equal(node_depth, state.last_depth)) {
      state.AdjustForDepth(node_depth);
    }

    // Add current node to path
    if (state.path_stack.size() == node_depth) {
      // Normal case: going one level deeper
      state.path_stack.push_back(std::string(node_name));
    } else if (state.path_stack.size() < node_depth) {
      // Gap in traversal - resize and set
      state.path_stack.resize(node_depth + 1);
      state.path_stack[node_depth] = std::string(node_name);
    } else {
      // We already adjusted for depth, now set current level
      state.path_stack.resize(node_depth + 1);
      state.path_stack[node_depth] = std::string(node_name);
    }

    // Update last_depth using safe conversion
    state.last_depth
      = static_cast<std::vector<std::string>::difference_type>(node_depth);

    // Now match the current path against our pattern
    return MatchPathAgainstPattern(state.path_stack);
  }

  //! Indicates whether this pattern contains any wildcards.
  /*!
   @return True if pattern contains * or ** wildcards
   @note Used for optimization in matching algorithms
  */
  [[nodiscard]] auto HasWildcards() const noexcept -> bool
  {
    return pattern_.has_wildcards;
  }

  //! Returns the number of segments in the compiled pattern.
  /*!
   @return Count of pattern segments after parsing and simplification
  */
  [[nodiscard]] auto PatternLength() const noexcept -> size_t
  {
    return pattern_.segments.size();
  }

  //! Returns the original path string used to create this pattern.
  /*!
   @return Reference to the original path string
   @note Convenience method for pattern introspection and debugging
  */
  [[nodiscard]] auto GetOriginalPath() const noexcept -> const std::string&
  {
    return pattern_.original_path;
  }

private:
  ParsedPath pattern_;
  MatcherType matcher_;

  //! Matches a complete path against the compiled pattern.
  /*!
   @param path Current path from root to current node
   @return MatchResult indicating match status
  */
  [[nodiscard]] auto MatchPathAgainstPattern(
    const std::vector<std::string>& path) const -> MatchResult
  {
    // Empty pattern matches empty path only
    if (pattern_.segments.empty()) {
      return path.empty() ? MatchResult::kCompleteMatch : MatchResult::kNoMatch;
    }

    // Try to match path against pattern
    return MatchRecursive(path, 0, 0);
  }

  //! Recursive pattern matching implementation.
  /*!
   @param path Full path to match
   @param path_idx Current position in path
   @param pattern_idx Current position in pattern
   @return MatchResult for this matching attempt
  */
  [[nodiscard]] auto MatchRecursive(const std::vector<std::string>& path,
    size_t path_idx, size_t pattern_idx) const -> MatchResult
  {
    // If we've consumed all pattern segments
    if (pattern_idx >= pattern_.segments.size()) {
      // Complete match if we've also consumed all path elements
      return (path_idx >= path.size()) ? MatchResult::kCompleteMatch
                                       : MatchResult::kNoMatch;
    }

    // If we've consumed all path elements but pattern remains
    if (path_idx >= path.size()) {
      // This could be a partial match if we're in the middle of a pattern
      // that could continue deeper
      return MatchResult::kPartialMatch;
    }

    const auto& segment = pattern_.segments[pattern_idx];

    // Handle recursive wildcard (**)
    if (segment.is_wildcard_recursive) {
      return HandleRecursiveWildcardMatch(path, path_idx, pattern_idx);
    }

    // Handle single wildcard (*) - matches exactly one path element
    if (segment.is_wildcard_single) {
      return MatchRecursive(path, path_idx + 1, pattern_idx + 1);
    }

    // Handle literal match
    if (matcher_(path[path_idx], segment.name)) {
      return MatchRecursive(path, path_idx + 1, pattern_idx + 1);
    }

    // No match
    return MatchResult::kNoMatch;
  }

  //! Handles recursive wildcard (**) matching.
  [[nodiscard]] auto HandleRecursiveWildcardMatch(
    const std::vector<std::string>& path, size_t path_idx,
    size_t pattern_idx) const -> MatchResult
  {
    // If ** is the last pattern element, it matches everything remaining
    if (pattern_idx + 1 >= pattern_.segments.size()) {
      return MatchResult::kCompleteMatch;
    }

    // Try matching the next pattern element at each remaining path position
    for (size_t i = path_idx; i < path.size(); ++i) {
      auto result = MatchRecursive(path, i, pattern_idx + 1);
      if (result == MatchResult::kCompleteMatch) {
        return MatchResult::kCompleteMatch;
      }
    }

    // ** could still match additional nodes if we go deeper, but only if
    // the remaining pattern could potentially match
    auto result = MatchRecursive(path, path.size(), pattern_idx + 1);
    if (result == MatchResult::kCompleteMatch) {
      return MatchResult::kCompleteMatch;
    }

    // If we haven't consumed the entire path and there's still pattern left,
    // this could be a partial match
    return MatchResult::kPartialMatch;
  }
};

} // namespace oxygen::scene::detail::query
