//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/Types/Traversal.h>

namespace oxygen::scene::detail {

template <typename SceneT> class SceneTraversalBase {
public:
  using SceneType = SceneT;
  using Node = add_const_if_t<SceneNode, std::is_const_v<SceneT>>;
  using NodeImpl = add_const_if_t<SceneNodeImpl, std::is_const_v<SceneT>>;
  using VisitedNode = VisitedNodeT<std::is_const_v<SceneT>>;

  virtual ~SceneTraversalBase() = default;

protected:
  constexpr static size_t kChildrenBufferCapacity = 8;

  /*
   Traversal contract:

   - The scene must remain alive for the entire lifetime of the traversal
     instance.
   - Traversal code may revisit node handles and refresh `SceneNodeImpl*` from
     the scene, as node implementations can become invalidated by mutations.
  */
  explicit SceneTraversalBase(const std::shared_ptr<SceneT>& scene)
  {
    CHECK_F(scene != nullptr, "scene cannot be null");
    scene_weak_ = scene;

    // Pre-allocate children buffer to avoid repeated small reservations
    children_buffer_.reserve(kChildrenBufferCapacity);
  }

  SceneTraversalBase(const SceneTraversalBase& other)
    : scene_weak_(other.scene_weak_)
  {
    // Children buffer is a temporary storage that should not be copied
    children_buffer_.reserve(kChildrenBufferCapacity);
  }

  SceneTraversalBase(SceneTraversalBase&& other) noexcept
    : scene_weak_(std::move(other.scene_weak_))
  {
    // Children buffer is a temporary storage that should not be copied
    children_buffer_.reserve(kChildrenBufferCapacity);
    other.children_buffer_.clear(); // Clear the moved-from buffer
  }

  auto operator=(const SceneTraversalBase& other) -> SceneTraversalBase&
  {
    if (this != &other) {
      scene_weak_ = other.scene_weak_;
      // Children buffer is a temporary storage that should not be copied
      children_buffer_.reserve(kChildrenBufferCapacity); // Pre-allocate buffer
    }
    return *this;
  }

  auto operator=(SceneTraversalBase&& other) noexcept -> SceneTraversalBase&
  {
    if (this != &other) {
      scene_weak_ = std::move(other.scene_weak_);
      // Children buffer is a temporary storage that should not be copied
      children_buffer_.reserve(kChildrenBufferCapacity);
      other.children_buffer_.clear(); // Clear the moved-from buffer
    }
    return *this;
  }

  auto GetScene() const -> SceneT&
  {
    CHECK_F(!scene_weak_.expired());
    return *scene_weak_.lock();
  }

  auto GetScene() -> SceneT&
  {
    CHECK_F(!scene_weak_.expired());
    return *scene_weak_.lock();
  }

  //=== Helper Methods ===----------------------------------------------------//

  auto IsSceneExpired() const { return scene_weak_.expired(); }

  struct TraversalEntry {
    VisitedNode visited_node;
    FilterResult parent_filter_result { FilterResult::kAccept };

    // Used by PostOrder to track if children have been processed
    enum class ProcessingState : uint8_t {
      kPending, //!< Node children not yet processed
      kChildrenProcessed //!< Node children already visited (post-order only)
    } state { ProcessingState::kPending };
  };

  [[nodiscard]] auto GetNodeImpl(const NodeHandle& handle) const
  {
    DCHECK_F(!scene_weak_.expired());
    DCHECK_F(handle.IsValid());

    // Define an alias for the return type to improve readability
    using ReturnType = add_const_if_t<SceneNodeImpl, std::is_const_v<SceneT>>*;
    const auto scene = scene_weak_.lock();
    if (!scene) {
      return ReturnType { nullptr };
    }
    return static_cast<ReturnType>(scene->TryGetNodeImpl(handle));
  }

  auto GetOptimalStackCapacity() const -> std::size_t
  {
    DCHECK_F(!scene_weak_.expired());
    // NOLINTBEGIN(*-magic-numbers)
    const auto node_count = scene_weak_.lock()->GetNodeCount();
    if (node_count <= 64) {
      return 32;
    }
    if (node_count <= 256) {
      return 64;
    }
    if (node_count <= 1024) {
      return 128;
    }
    return 256; // Cap at reasonable size for deep scenes
    // NOLINTEND(*-magic-numbers)
  }

  void CollectChildrenToBuffer(NodeImpl* node, std::size_t parent_depth) const
  {
    children_buffer_.clear(); // Fast - just resets size for pointer vector

    LOG_SCOPE_F(2, "Collect Children");
    DLOG_F(2, "node: {}", node->GetName());

    auto child_handle = node->AsGraphNode().GetFirstChild();
    if (!child_handle.IsValid()) {
      DLOG_F(2, "no children");
      return; // Early exit for leaf nodes
    }

    const auto child_depth = parent_depth + 1;

    // Collect all children in a single pass.
    while (child_handle.IsValid()) {
      auto* child_node = this->GetNodeImpl(child_handle);

      // Sanity checks
      DCHECK_NOTNULL_F(child_node,
        "corrupted scene graph, child `{}` of `{}` is no longer in the scene",
        to_string_compact(child_handle), node->GetName());
      DLOG_F(2, " + {}", child_node->GetName());

      children_buffer_.push_back(VisitedNode {
        .handle = child_handle, // The handle is the only stable thing
        .node_impl = nullptr, // Do not update the node_impl here, it will be
        // updated during traversal because the table may
        // change
        .depth = child_depth // Set the correct depth for children
      });
      child_handle = child_node->AsGraphNode().GetNextSibling();
    }

    DLOG_F(2, "total: {}", children_buffer_.size());
  }

  template <TraversalOrder Order, typename Container>
  void QueueChildrenForTraversal(
    const FilterResult parent_filter_result, Container& container) const
  {
    using Traits = ContainerTraits<Order>;

    if (!children_buffer_.empty()) {
      for (auto& child : children_buffer_) {
        Traits::push(container,
          TraversalEntry {
            .visited_node = child,
            .parent_filter_result = parent_filter_result,
            .state = TraversalEntry::ProcessingState::kPending,
          });
      }
    }
    DLOG_F(2, "queued: {}", container.size());
  }

  //! Initialize traversal container with roots and optimal capacity
  template <TraversalOrder Order, typename Container>
  void InitializeTraversal(
    std::span<VisitedNode> roots, Container& container) const
  {
    using Traits = ContainerTraits<Order>;

    // Optimize for stack-based traversals with pre-allocation
    if constexpr ((Order == TraversalOrder::kPreOrder)
      || Order == TraversalOrder::kPostOrder) {
      container.reserve(GetOptimalStackCapacity());
    }

    // Add root nodes to container
    for (const auto& root : roots) {
      Traits::push(container,
        TraversalEntry {
          .visited_node = root,
          // For consistency, we set the parent result for root nodes as
          // 'accepted'. Filters should handle any additional logic for
          // determining if the node should accept appropriately.
          .parent_filter_result = FilterResult::kAccept,
          .state = TraversalEntry::ProcessingState::kPending,
        });
    }
  }

  /*!
   The implementation for the node being traversed is updated before a node is
   visited (or revisited in post-order traversal) to make the traversal
   algorithm resilient to visitors that mutate the scene graph during traversal.
  */
  auto UpdateNodeImpl(TraversalEntry& entry) const -> bool
  {
    // Refresh the node impl from handle ALWAYS even if it is not null.
    // Mutations during child visits will invalidate the pointers
    entry.visited_node.node_impl = this->GetNodeImpl(entry.visited_node.handle);
    return entry.visited_node.node_impl != nullptr;
  }

  template <typename FilterFunc>
  auto ApplyNodeFilter(FilterFunc& filter, const TraversalEntry& entry,
    TraversalResult& result) const -> FilterResult
  {
    DLOG_SCOPE_FUNCTION(2);

    DCHECK_NOTNULL_F(entry.visited_node.node_impl);
    DLOG_F(2, "node: {}", entry.visited_node.node_impl->GetName());

    const auto filter_result
      = filter(entry.visited_node, entry.parent_filter_result);
    if (filter_result != FilterResult::kAccept) {
      ++result.nodes_filtered;
    }
    DLOG_F(2, "-> {}", nostd::to_string(filter_result));
    return filter_result;
  }

private:
  std::weak_ptr<SceneT> scene_weak_;
  mutable std::vector<VisitedNode> children_buffer_;
};

} // namespace oxygen::scene::detail
