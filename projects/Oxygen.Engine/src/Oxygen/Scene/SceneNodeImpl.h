//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string>

#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/Types/Flags.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene {

class Scene;

// Forward declare for GraphNode
namespace detail {
  class GraphData;
}

//! Internal implementation of scene nodes using component composition
//! architecture.
/*!
 SceneNodeImpl serves as the actual data container for scene nodes, employing a
 component-based design for optimal performance and modularity. This class
 stores object metadata, hierarchy relationships, transform data, and scene
 flags as separate components, enabling efficient batch processing and
 cache-friendly memory access patterns. This class is not intended for direct
 public use - access is provided through the SceneNode handle/view pattern which
 ensures resource safety and provides a stable API surface.
 */
class SceneNodeImpl : public Composition, public CloneableMixin<SceneNodeImpl> {
  OXYGEN_TYPED(SceneNodeImpl)
public:
  using Flags = SceneFlags<SceneNodeFlags>;

  //! Default flags for scene nodes, providing a sensible starting point.
  static constexpr auto kDefaultFlags
    = Flags {}
        .SetFlag(
          SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(true))
        .SetFlag(
          SceneNodeFlags::kCastsShadows, SceneFlag {}.SetInheritedBit(true))
        .SetFlag(
          SceneNodeFlags::kReceivesShadows, SceneFlag {}.SetInheritedBit(true))
        .SetFlag(SceneNodeFlags::kRayCastingSelectable,
          SceneFlag {}.SetInheritedBit(true));

  //! Efficient graph node view over a SceneNodeImpl, for hierarchy traversal
  //! and manipulation.
  /*!
   GraphNode provides a cached, high-performance interface for accessing and
   modifying the hierarchical structure of scene nodes. This nested class acts
   as a view into the graph data component, caching pointers to avoid repeated
   component lookups during tree traversal operations.

   The design employs pointer caching to eliminate the expensive component
   lookup costs that would otherwise occur on every hierarchy operation. Move
   semantics ensure proper invalidation during SceneNodeImpl lifecycle events,
   preventing dangling pointer access while maintaining optimal performance for
   valid operations.

   All hierarchy queries and modifications are validated through the IsValid()
   check, providing graceful degradation when accessing invalidated nodes.

   @note Semantic validation of graph operations, such as preventing cycles or
   self-parenting, cannot be enforced at this level. It is the responsibility of
   the scene graph manager or higher-level API to ensure that.
   */
  class GraphNode {
  public:
    friend class SceneNodeImpl;

    ~GraphNode() = default;

    // Not copyable to prevent dangling pointers
    OXYGEN_MAKE_NON_COPYABLE(GraphNode)

    // Move constructor
    GraphNode(GraphNode&& other) noexcept
      : impl_(std::exchange(other.impl_, nullptr))
      , graph_data_(std::exchange(other.graph_data_, nullptr))
    {
    }

    // Move assignment
    auto operator=(GraphNode&& other) noexcept -> GraphNode&
    {
      if (this != &other) {
        impl_ = std::exchange(other.impl_, nullptr);
        graph_data_ = std::exchange(other.graph_data_, nullptr);
      }
      return *this;
    }

    OXGN_SCN_NDAPI auto GetParent() const noexcept -> const NodeHandle&;
    OXGN_SCN_NDAPI auto GetFirstChild() const noexcept -> const NodeHandle&;
    OXGN_SCN_NDAPI auto GetNextSibling() const noexcept -> const NodeHandle&;
    OXGN_SCN_NDAPI auto GetPrevSibling() const noexcept -> const NodeHandle&;

    OXGN_SCN_API auto SetParent(const NodeHandle& parent) noexcept -> void;
    OXGN_SCN_API auto SetFirstChild(const NodeHandle& child) noexcept -> void;
    OXGN_SCN_API auto SetNextSibling(const NodeHandle& sibling) noexcept
      -> void;
    OXGN_SCN_API auto SetPrevSibling(const NodeHandle& sibling) noexcept
      -> void;

    OXGN_SCN_NDAPI auto IsRoot() const noexcept -> bool;
    OXGN_SCN_NDAPI auto IsValid() const noexcept -> bool
    {
      return impl_ != nullptr && graph_data_ != nullptr;
    }

  private:
    GraphNode(SceneNodeImpl* impl, detail::GraphData* graph_data)
      : impl_(impl)
      , graph_data_(graph_data)
    {
    }

    auto Invalidate() noexcept -> void
    {
      impl_ = nullptr;
      graph_data_ = nullptr;
    }

    SceneNodeImpl* impl_; //!< Back pointer to the SceneNodeImpl instance
    detail::GraphData*
      graph_data_; //!< Cached pointer to the GraphData component
  };

  OXGN_SCN_API explicit SceneNodeImpl(
    const std::string& name, Flags flags = kDefaultFlags);

  ~SceneNodeImpl() override = default;

  OXGN_SCN_API SceneNodeImpl(const SceneNodeImpl& other);
  OXGN_SCN_API auto operator=(const SceneNodeImpl& other) -> SceneNodeImpl&;
  OXGN_SCN_API SceneNodeImpl(SceneNodeImpl&& other) noexcept;
  OXGN_SCN_API auto operator=(SceneNodeImpl&& other) noexcept -> SceneNodeImpl&;

  OXGN_SCN_NDAPI auto GetName() const noexcept -> std::string_view;
  OXGN_SCN_API auto SetName(std::string_view name) noexcept -> void;

  //=== Node Flags Accessors ===----------------------------------------------//

  OXGN_SCN_NDAPI auto GetFlags() const noexcept -> const Flags&;
  OXGN_SCN_NDAPI auto GetFlags() noexcept -> Flags&;

  //=== Graph Aware View ===--------------------------------------------------//

  OXGN_SCN_API auto AsGraphNode() noexcept -> GraphNode&;
  OXGN_SCN_API auto AsGraphNode() const noexcept -> const GraphNode&;

  //=== Transform management ===----------------------------------------------//

  //! Marks the node's transform as requiring recalculation.
  OXGN_SCN_API auto MarkTransformDirty() noexcept -> void;

  //! Checks whether the node's transform requires recalculation.
  OXGN_SCN_NDAPI auto IsTransformDirty() const noexcept -> bool;

  //! Updates the node's world transformation matrices.
  OXGN_SCN_API auto UpdateTransforms(const Scene& scene) -> void;

  //=== Cloning Support ===---------------------------------------------------//

  [[nodiscard]] static auto IsCloneable() noexcept -> bool { return true; }
  // ReSharper disable once CppHidingFunction
  OXGN_SCN_NDAPI auto Clone() const -> std::unique_ptr<SceneNodeImpl>;

  //=== Dynamic Components Support ===----------------------------------------//

  // SceneNodeImpl supports adding, removing and replacing certain components
  // after construction.
  using Composition::AddComponent;
  using Composition::RemoveComponent;
  using Composition::ReplaceComponent;

protected:
  // Used primarily for testing, and for creating an empty object when cloning
  // from another one.
  SceneNodeImpl() = default;
  friend class CloneableMixin;

  //! Marks the node transform matrices as clean, without updating them.
  /*!
   This method is used to reset the dirty state of the node's transform
   matrices without recalculating them. It is typically called after the
   transforms have been updated through other means, such as during scene
   initialization or when the node's transform has been manually set, or when
   testing.

   The proper way remains to call UpdateTransforms() to ensure the transform
   matrices are up to date.
  */
  OXGN_SCN_API auto ClearTransformDirty() noexcept -> void;

private:
  [[nodiscard]] auto ShouldIgnoreParentTransform() const
  {
    return GetFlags().GetEffectiveValue(SceneNodeFlags::kIgnoreParentTransform);
  }

  // Cached GraphNode for efficient access - always initialized, using
  // std::optional for efficiency
  mutable std::optional<GraphNode> cached_graph_node_ {};
};

} // namespace oxygen::scene
