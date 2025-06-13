//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <ranges>
#include <span>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ResourceTable.h>
// ReSharper disable once CppUnusedIncludeDirective
#include <Oxygen/Scene/Detail/Scene_safecall_impl.h> // needed for validators
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/SceneTraversal.h>

using oxygen::scene::Scene;
using oxygen::scene::SceneNode;

//------------------------------------------------------------------------------
// Scene Node Creation Implementation
//------------------------------------------------------------------------------

/*!
 This method creates a new scene node and adds it to this scene as a root node.
 The created node will have no parent and will be automatically added to the
 scene's root nodes' collection.

 This call will never fail unless the resource table is full. In such a case,
 the application will terminate.

 @tparam Args Variadic template arguments forwarded to SceneNodeImpl
 constructor. Must match SceneNodeImpl constructor parameters (e.g., name,
 flags).

 @param args Arguments forwarded to SceneNodeImpl constructor. Typically, it
 includes node name and optional flags.

 @return A SceneNode wrapper around the handle of the newly created root node.

 @see CreateNode(const std::string&) for the primary public interface
 @see CreateNode(const std::string&, SceneNode::Flags) for flag-based creation
 @see SceneNodeImpl constructor for valid argument combinations
*/
template <typename... Args>
auto Scene::CreateNodeImpl(Args&&... args) noexcept -> SceneNode
{
  const NodeHandle handle { nodes_->Emplace(std::forward<Args>(args)...),
    GetId() };
  DCHECK_F(handle.IsValid(), "expecting a valid handle for a new node");

  AddRootNode(handle);
  return { shared_from_this(), handle };
}

/*!
 This method will only fail if the resource table holding scene data is full,
 which can only be remedied by increasing the initial capacity of the table.
 Therefore, a failure is a fatal error that will result in the application
 terminating.

 @param name name to give to the new node

 @return A SceneNode wrapper around the handle of the newly created node. May be
 used to obtain the underlying node implementation object.

 @see SceneNodeImpl::kDefaultFlags for default flags assigned to the new node.
*/
auto Scene::CreateNode(const std::string& name //<! name to give to the new node
  ) -> SceneNode
{
  return CreateNodeImpl(name);
}

/*!
 @copydetails CreateNode(const std::string& name)
 @param flags Flags to assign to the new node.
*/
auto Scene::CreateNode(const std::string& name, SceneNode::Flags flags)
  -> SceneNode
{
  return CreateNodeImpl(name, flags);
}

/*!
 This method creates a new scene node and links it as a child to the specified
 \p parent node. The created node will be properly inserted into the scene
 hierarchy with all parent-child relationships established.

 ### Failure Scenarios

 - If the \p parent handle is not valid (expired or invalidated).
 - If the \p parent is valid but its corresponding node was removed from the
   scene.
 - If node creation fails due to resource table being full or component
   initialization issues.

 @note This method will terminate the program if the \p parent does not belong
 to this scene. For cross-scene operations, use the appropriate re-parenting and
 adoption APIs.

 @tparam Args Variadic template arguments forwarded to SceneNodeImpl
 constructor. Must match SceneNodeImpl constructor parameters (e.g., name,
 flags).

 @param parent The parent node under which to create the new child node. Must be
 valid and belong to this scene.
 @param args Arguments forwarded to SceneNodeImpl constructor. Typically, it
 includes node name and optional flags.

 @return An optional SceneNode wrapper around the handle of the newly created
 node when successful; std::nullopt otherwise.

 @see CreateChildNode(const SceneNode&, const std::string&) for the primary
 public interface
 @see CreateChildNode(const SceneNode&, const std::string&, SceneNode::Flags)
 for flag-based creation
 @see LinkChild for the underlying hierarchy linking operation
 @see SceneNodeImpl constructor for valid argument combinations
*/
template <typename... Args>
auto Scene::CreateChildNodeImpl(SceneNode& parent, Args&&... args) noexcept
  -> std::optional<SceneNode>
{
  DLOG_SCOPE_F(3, "Create Child Node");
  return SafeCall(NodeIsValidAndMine(parent),
    [&](const SafeCallState& state) -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, &parent);
      DCHECK_NOTNULL_F(state.node_impl);

      const NodeHandle child_handle {
        nodes_->Emplace(std::forward<Args>(args)...), GetId()
      };
      DCHECK_F(
        child_handle.IsValid(), "expecting a valid handle for a new node");
      auto* node_impl = &nodes_->ItemAt(child_handle);

      LinkChild(
        state.node->GetHandle(), state.node_impl, child_handle, node_impl);

      // Mark parent transform dirty since it now has a new child
      state.node_impl->MarkTransformDirty();

      return SceneNode(shared_from_this(), child_handle);
    });
}

/*!
 This method creates a new scene node and links it as a child to the specified
 \p parent node. The created node will be properly inserted into the scene
 hierarchy with all parent-child relationships established.

 ### Failure Scenarios

 - If the \p parent handle is not valid (expired or invalidated).
 - If the \p parent is valid but its corresponding node was removed from the
   scene.
 - If node creation fails due to resource table being full or component
   initialization issues.

 @note This method will terminate the program if the \p parent does not belong
 to this scene. For cross-scene operations, use the appropriate re-parenting and
 adoption APIs.

 @param parent The parent node under which to create the new child node. Must be
 in this scene.
 @param name The name to assign to the new child node.

 @return An optional SceneNode wrapper around the handle of the newly created
 node when successful; std::nullopt otherwise.

 @see SceneNodeImpl::kDefaultFlags for default flags assigned to the new node.
*/
auto Scene::CreateChildNode(SceneNode& parent, const std::string& name) noexcept
  -> std::optional<SceneNode>
{
  return CreateChildNodeImpl(parent, name);
}

/*!
 @copydetails CreateChildNode(const SceneNode&, const std::string&)
 @param flags Flags to assign to the new node.
*/
auto Scene::CreateChildNode(SceneNode& parent, const std::string& name,
  const SceneNode::Flags flags) noexcept -> std::optional<SceneNode>
{
  return CreateChildNodeImpl(parent, name, flags);
}

/*!
 This method safely destroys a leaf node (a node with no children) from the
 scene. The node is properly unlinked from its parent and siblings, removed from
 the scene's node table, and its handle is invalidated. If the node is a root
 node, it is also removed from the scene's root nodes' collection.

 ### Failure Scenarios

 - If the node handle is invalid (not pointing to a valid node)
 - If the node is no longer in the scene (was previously destroyed)
 - If the node has children (use `DestroyNodeHierarchy()` instead)
 - If the node does not belong to this scene (fatal error)

 ### Post-conditions (on success)

 - The node is removed from the scene hierarchy
 - The node's handle is invalidated
 - Parent-child and sibling relationships are properly updated
 - If the node was a root node, it's removed from the root nodes collection

 @note For nodes with children, use `DestroyNodeHierarchy()` to destroy the
 entire subtree.
 @note If the node is no longer in the scene, its handle will be invalidated,
 ensuring lazy invalidation semantics.

 @param node Node to destroy (must be a leaf node belonging to this scene)

 @return true if the node was successfully destroyed, false if the node is
 invalid or was not found in the scene.

 @see DestroyNodeHierarchy() for destroying nodes with children
 @see UnlinkNode() for removing nodes from hierarchy without destroying them
*/
auto Scene::DestroyNode(SceneNode& node) noexcept -> bool
{
  DLOG_SCOPE_F(3, "Destroy Node");
  return SafeCall(
    LeafNodeCanBeDestroyed(node), [&](const SafeCallState& state) -> bool {
      DCHECK_EQ_F(state.node, &node);
      DCHECK_NOTNULL_F(state.node_impl);

      // Mark parent transform dirty if node has a parent (since parent is
      // losing a child)
      if (node.HasParent()) {
        if (const auto parent_opt = node.GetParent(); parent_opt.has_value()) {
          const auto parent_impl_opt = GetNodeImpl(*parent_opt);
          if (parent_impl_opt.has_value()) {
            parent_impl_opt->get().MarkTransformDirty();
          }
        }
      }

      // Properly unlink the node from its parent and siblings
      UnlinkNode(node.GetHandle(), state.node_impl);

      const auto handle = node.GetHandle();
      // Remove from root nodes set only if it's actually a root node
      // (optimization)
      if (node.IsRoot()) {
        RemoveRootNode(handle);
      }

      const auto removed = nodes_->Erase(handle);
      DCHECK_EQ_F(removed, 1);
      node.Invalidate();
      return true;
    });
}

/*!
 This method destroys multiple leaf nodes in a batch operation. Each node in the
 provided span is destroyed using the same logic as `DestroyNode()`, with the
 results collected into a vector indicating success or failure for each node.

 ### Batch Operation Behavior

 - Processes each node independently - failure of one node does not affect
   others
 - Returns a result vector with one entry per input node (true = destroyed,
   false = failed)

 @param nodes Span of leaf nodes to destroy (all must belong to this scene)

 @return Vector of boolean results, one per input node. true indicates the node
 was successfully destroyed, false indicates failure (see `DestroyNode()` for
 failure scenarios).

 @note **Partial Success:** Each individual destruction is atomic, but some may
 fail.

 @see DestroyNode() for detailed destruction logic and failure scenarios
 @see DestroyNodeHierarchy() for destroying nodes with children
*/
auto Scene::DestroyNodes(std::span<SceneNode> nodes) noexcept
  -> std::vector<uint8_t>
{
  DLOG_SCOPE_F(3, "Destroy Nodes");

  // Bailout early if no nodes to destroy
  if (nodes.empty()) {
    return {};
  }

  std::vector<uint8_t> results;
  results.reserve(nodes.size());

  for (SceneNode& node : nodes) {
    const auto result = DestroyNode(node);
    results.push_back(result); // NOLINT(*-implicit-bool-conversion)
  }

  LogPartialFailure(results, "DestroyNodes");

  return results;
}

/*!
 This method safely destroys an entire node hierarchy starting from the given
 root node. All nodes in the subtree (including the root) are destroyed, with
 the hierarchy properly unlinked from its parent before destruction begins.

 ### Failure Scenarios

 - If the node handle is invalid (not pointing to a valid node)
 - If the node is no longer in the scene (was previously destroyed)
 - If the node does not belong to this scene (fatal error)

 ### Post-conditions (on success)

 - The entire hierarchy is removed from the scene
 - The starting node's handle is invalidated
 - Parent-child relationships are properly updated for the starting node's
   parent
 - If the starting node was a root node, it's removed from the root nodes
   collection

 @note This method uses a non-recursive implementation for performance and to
 avoid stack overflow on deep hierarchies.

 @param starting_node Root of the hierarchy to destroy (must belong to this
 scene)

 @return true if the hierarchy was successfully destroyed, false if the node is
 invalid or was not found in the scene.

 @see DestroyNode() for destroying individual leaf nodes
 @see DestroyNodeHierarchies() for batch destruction of multiple hierarchies
*/
auto Scene::DestroyNodeHierarchy(SceneNode& starting_node) noexcept -> bool
{
  DLOG_SCOPE_F(3, "Destroy Node Hierarchy");
  return SafeCall(
    NodeIsValidAndMine(starting_node), [&](const SafeCallState& state) -> bool {
      DCHECK_EQ_F(state.node, &starting_node);
      DCHECK_NOTNULL_F(state.node_impl);

      // First, handle the starting node's relationship with its parent/scene
      // before beginning traversal to unlink the entire hierarchy
      if (starting_node.IsRoot()) {
        // This is an actual scene root node - remove from root nodes
        RemoveRootNode(starting_node.GetHandle());
      } else {
        // This node has a parent - unlink it from its parent
        // Mark parent transform dirty since it's losing a child hierarchy
        const auto parent_opt = starting_node.GetParent();
        if (parent_opt.has_value()) {
          const auto parent_impl_opt = GetNodeImpl(*parent_opt);
          if (parent_impl_opt.has_value()) {
            parent_impl_opt->get().MarkTransformDirty();
          }
        }

        // This is the only unlinking we need since we're destroying the entire
        // subtree
        UnlinkNode(starting_node.GetHandle(), state.node_impl);
      }

      // Use SceneTraversal with post-order to ensure children are destroyed
      // before parents
      MutatingTraversal traversal(shared_from_this());
      size_t destroyed_count = 0;

      const auto traversal_result = traversal.TraverseHierarchy(
        starting_node,
        [&](const auto& node, bool dry_run) -> VisitResult {
          if (dry_run) {
            // During dry-run, we always want to continue to process children
            // first before destroying this node (post-order behavior)
            return VisitResult::kContinue;
          }
          // Real visit: destroy the node after its children have been destroyed
          // Capture node name before destruction for logging
          const std::string node_name = std::string(node.node_impl->GetName());

          const auto removed = nodes_->Erase(node.handle);
          if (removed > 0) {
            ++destroyed_count;
            return VisitResult::kContinue;
          }
          // Node destruction failed - this shouldn't happen unless the node
          // was already destroyed or became invalid during traversal
          DLOG_F(ERROR, "Failed to destroy node: {} (handle: {})", node_name,
            nostd::to_string(node.handle));
          return VisitResult::kStop;
        },
        TraversalOrder::kPostOrder); // Post-order guarantees children destroyed
                                     // before parents

      DLOG_F(2, "Traversal result - completed: {}, visited: {}, filtered: {}",
        traversal_result.completed, traversal_result.nodes_visited,
        traversal_result.nodes_filtered);

      // Invalidate the starting node handle since it's been destroyed
      starting_node.Invalidate();
      DLOG_F(2, "Destroyed {} nodes in hierarchy", destroyed_count);

      // Return success only if traversal completed without failures and we
      // destroyed at least one node
      return traversal_result.completed && destroyed_count > 0;
    });
}

/*!
 This method destroys multiple node hierarchies in a batch operation. Each
 hierarchy root in the provided span is destroyed along with all its descendants
 using the same logic as `DestroyNodeHierarchy()`, with the results collected
 into a vector indicating success or failure for each hierarchy.

 ### Batch Operation Behavior

 - Processes each hierarchy independently - failure of one hierarchy does not
   affect others
 - Returns a result vector with one entry per input hierarchy root (true =
   destroyed, false = failed)
 - Each hierarchy is destroyed completely, including all descendants

 @param hierarchy_roots Span of hierarchy root nodes to destroy (all must belong
 to this scene)

 @return Vector of boolean results, one per input hierarchy root. true indicates
 the hierarchy was successfully destroyed, false indicates failure (see
 `DestroyNodeHierarchy()` for failure scenarios).

 @note **Partial Success: ** Each hierarchy destruction is atomic, but some may
 fail.

 @see DestroyNodeHierarchy() for detailed destruction logic and failure
 scenarios
 @see DestroyNodes() for destroying individual leaf nodes
*/
auto Scene::DestroyNodeHierarchies(
  std::span<SceneNode> hierarchy_roots) noexcept -> std::vector<uint8_t>
{
  DLOG_SCOPE_F(3, "Destroy Node Hierarchies");

  // Bailout early if no hierarchies to destroy
  if (hierarchy_roots.empty()) {
    return {};
  }

  std::vector<uint8_t> results;
  results.reserve(hierarchy_roots.size());

  for (SceneNode& hierarchy_root : hierarchy_roots) {
    const auto result = DestroyNodeHierarchy(hierarchy_root);
    results.push_back(result); // NOLINT(*-implicit-bool-conversion)
  }

  LogPartialFailure(results, "DestroyNodeHierarchies");

  return results;
}

//------------------------------------------------------------------------------
// Scene Node Cloning Implementation
//------------------------------------------------------------------------------

/*!
 This method clones the \p original node (preserving its component data) and
 creates an __orphan__ node. The cloned node will have no hierarchy
 relationships, will not be a root node, and will have \p new_name as a name.

 ### Failure Scenarios

 - If the \p original handle is not valid (expired or invalidated).
 - If the \p original node is valid but its corresponding node was removed from
   its scene.
 - If cloning the original node fails due to component issues or memory
   constraints.

 @note the cloned node __must__ be added to the roots collection or attached to
 a parent node to become part of the scene hierarchy. Failure to do so will
 result in a leaked node that will dangle until the scene is cleared.

 @note This method will terminate the program if the resource table is full,
 which can only be remedied by increasing the initial capacity of the table.

 @param original The node to clone (can be from any scene).
 @param new_name The name to assign to the cloned node.

 @return When successful, a std::pair, where the first item is the handle to the
 cloned node, and the second item is the corresponding SceneNodeImpl object;
 std::nullopt otherwise.
*/
auto Scene::CloneNode(SceneNode& original, const std::string& new_name)
  -> std::optional<std::pair<NodeHandle, SceneNodeImpl*>>
{
  DLOG_SCOPE_F(3, "Clone Node");
  return SafeCall(NodeIsValidAndInScene(original),
    [&](const SafeCallState& state)
      -> std::optional<std::pair<NodeHandle, SceneNodeImpl*>> {
      DCHECK_EQ_F(state.node, &original);
      DCHECK_NOTNULL_F(state.node_impl);

      // Clone the original node implementation
      const auto cloned_impl = state.node_impl->Clone();
      cloned_impl->SetName(new_name);

      // Add the cloned implementation to this scene's node table
      const NodeHandle cloned_handle { nodes_->Insert(std::move(*cloned_impl)),
        GetId() };
      DCHECK_F(
        cloned_handle.IsValid(), "expecting a valid handle for cloned node");

      return { { cloned_handle, &nodes_->ItemAt(cloned_handle) } };
    });
}

/*!
 This method clones the \p original node (preserving its component data) and
 creates a new root node in this scene. The cloned node will be a root node,
 with no hierarchy relationships, and will have \p new_name as a name.

 ### Failure Scenarios

 - If the \p original handle is not valid (expired or invalidated).
 - If the \p original node is valid but its corresponding node was removed from
   its scene.
 - If cloning the original node fails due to component issues or memory
   constraints.

 @note This method will terminate the program if the resource table is full,
 which can only be remedied by increasing the initial capacity of the table.

 @param original The node to clone (can be from any scene).
 @param new_name The name to assign to the cloned node.

 @return An optional SceneNode wrapper around the handle of the newly created
 node when successful; std::nullopt otherwise.
*/
auto Scene::CreateNodeFrom(SceneNode& original, const std::string& new_name)
  -> std::optional<SceneNode>
{
  DLOG_SCOPE_F(3, "Clone Into Parent");
  return SafeCall(NodeIsValidAndInScene(original),
    [&](const SafeCallState& state) -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, &original);
      DCHECK_NOTNULL_F(state.node_impl);

      // Create the cloned node as a root first
      auto clone = CloneNode(original, new_name);
      if (!clone) {
        return std::nullopt; // Cloning failed
      }
      auto [cloned_handle, cloned_node_impl] = *clone;

      // Add as root node since clones are orphaned
      AddRootNode(cloned_handle);

      return SceneNode(shared_from_this(), cloned_handle);
    });
}

/*!
 This method clones the \p original node (preserving its component data) and
 creates a new node under the given \p parent in this scene. The cloned node
 will become a child of the \p parent node, and will have \p new_name as a name.

 ### Failure Scenarios

 - If the \p parent handle is not valid (expired or invalidated).
 - If the \p parent is valid but its corresponding node was removed from the
   scene.
 - If the \p original node handle is not valid (expired or invalidated).
 - If the \p original node is valid but its corresponding node was removed from
   its scene.
 - If cloning the original node fails due to component issues or memory
   constraints.

 @note This method will terminate the program if the \p parent does not belong
 to this scene. For cross-scene operations, use the appropriate adoption APIs.

 @note This method will terminate the program if the resource table is full,
 which can only be remedied by increasing the initial capacity of the table.

 @param parent The parent node under which to create the cloned child (must be
 in this scene).
 @param original The original node to clone (can be from this or another scene).
 @param new_name The name to assign to the cloned child node.

 @return An optional SceneNode wrapper around the handle of the newly created
 node when successful; std::nullopt otherwise.
*/
auto Scene::CreateChildNodeFrom(SceneNode& parent, SceneNode& original,
  const std::string& new_name) -> std::optional<SceneNode>
{
  DLOG_SCOPE_F(3, "Clone Into Parent");
  return SafeCall(NodeIsValidAndMine(parent),
    [&](const SafeCallState& state) -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, &parent);
      DCHECK_NOTNULL_F(state.node_impl);

      // Create the cloned node as a root first
      auto clone = CloneNode(original, new_name);
      if (!clone) {
        return std::nullopt; // Cloning failed
      }
      auto [cloned_handle, cloned_node_impl] = *clone;
      LinkChild(
        parent.GetHandle(), state.node_impl, cloned_handle, cloned_node_impl);

      // Mark parent transform dirty since it now has a new child
      state.node_impl->MarkTransformDirty();

      return SceneNode(shared_from_this(), cloned_handle);
    });
}

/*!
 Traverses the hierarchy to be cloned starting from \p starting_node, in a
 non-recursive way, cloning each node and properly linking it to the hierarchy
 under construction.

 This method performs a complete hierarchy clone, creating an exact structural
 copy of the original hierarchy in this scene. All parent-child relationships
 are preserved, and component data is fully copied. The cloned hierarchy is
 independent of the original and can be modified without affecting the source.

 The cloning process uses pre-order traversal to ensure parent nodes are created
 before their children, allowing proper hierarchy linking. If any node in the
 hierarchy fails to clone, the entire operation is rolled back to maintain scene
 consistency.

 ### Failure Scenarios

 - If the \p starting_node handle is not valid (expired or invalidated)
 - If the \p starting_node is valid but its corresponding node was removed from
   its scene
 - If any individual node cloning fails due to component issues or memory
   constraints
 - If the resource table is full and cannot accommodate new nodes
 - If hierarchy corruption is detected during traversal

 ### Post-Conditions

 - On success: Complete hierarchy is cloned with all relationships intact as an
   orphan hierarchy
 - On failure: No nodes are added to this scene (complete rollback)
 - Original hierarchy remains unmodified in all cases

 @note This method creates an __orphan__ hierarchy. The cloned hierarchy root
 will have no parent and will not be added to the root nodes collection. The
 caller must either add it to the root nodes collection or link it to a parent
 node to make it part of the scene hierarchy.

 @note This method will terminate the program if the resource table is full,
 which can only be remedied by increasing the initial capacity of the table.

 @note **Performance:** Uses pre-order traversal for optimal memory locality
 during hierarchy construction. Maintains handle mapping for efficient
 parent-child linking.

 @param starting_node The node from which to start cloning the hierarchy (can be
 from any scene)

 @return When successful, a std::pair containing the handle to the root node of
 the cloned orphan hierarchy and the corresponding SceneNodeImpl object;
 std::nullopt if any failure occurs

 @see CreateHierarchyFrom() for the public interface that uses this method
 @see CloneNode() for cloning individual nodes without hierarchy
*/
auto Scene::CloneHierarchy(const SceneNode& starting_node)
  -> std::optional<std::pair<NodeHandle, SceneNodeImpl*>>
{
  DLOG_SCOPE_F(3, "Clone Hierarchy");

  // Validate starting node using the SafeCall pattern for consistency
  if (!starting_node.IsValid()) {
    DLOG_F(WARNING, "CloneHierarchy starting from an invalid node.");
    return std::nullopt;
  }

  std::unordered_map<NodeHandle, NodeHandle> handle_map;
  NodeHandle root_cloned_handle;
  SceneNodeImpl* root_cloned_impl = nullptr;
  bool root_cloned = false;
  std::vector<NodeHandle> cloned_nodes; // Track for cleanup on failure
  const NonMutatingTraversal traversal(starting_node.scene_weak_.lock());
  const auto traversal_result = traversal.TraverseHierarchy(
    starting_node,
    [&](const auto& node, bool dry_run) -> VisitResult {
      DCHECK_F(!dry_run,
        "CloneHierarchy uses kPreOrder and should never receive dry_run=true");

      auto orig_parent_handle = node.node_impl->AsGraphNode().GetParent();
      const std::string name { node.node_impl->GetName() };

      try {
        // Clone the node directly from impl
        const auto cloned_impl = node.node_impl->Clone();
        cloned_impl->SetName(name);
        const NodeHandle cloned_handle {
          nodes_->Insert(std::move(*cloned_impl)), GetId()
        };
        DCHECK_F(
          cloned_handle.IsValid(), "expecting a valid handle for cloned node");

        cloned_nodes.push_back(cloned_handle);
        handle_map[node.handle] = cloned_handle;
        if (!orig_parent_handle.IsValid()) {
          // Root node of the hierarchy being cloned - keep as orphan
          root_cloned_handle = cloned_handle;
          root_cloned_impl = &nodes_->ItemAt(cloned_handle);
          root_cloned = true;
          // Do NOT add to root nodes - create as orphan hierarchy
        } else {
          // Link to already-cloned parent
          const auto it = handle_map.find(orig_parent_handle);
          if (it == handle_map.end()) {
            // This should never happen with depth-first traversal and a valid
            // hierarchy If it does, it indicates corruption in the source
            // hierarchy
            DLOG_F(ERROR,
              "Parent handle {} not found in handle map for node {} - "
              "hierarchy corruption detected",
              nostd::to_string(orig_parent_handle), name);
            return VisitResult::kStop;
          }
          const NodeHandle cloned_parent_handle = it->second;
          SceneNodeImpl* cloned_parent_impl
            = &GetNodeImplRefUnsafe(cloned_parent_handle);
          LinkChild(cloned_parent_handle, cloned_parent_impl, cloned_handle,
            &nodes_->ItemAt(cloned_handle));

          // Mark parent transform dirty since it now has a new child
          cloned_parent_impl->MarkTransformDirty();
        }
        return VisitResult::kContinue;
      } catch (const std::exception& ex) {
        DLOG_F(ERROR, "Failed to clone node {}: {}", name, ex.what());
        // Clean up any nodes we've created so far
        for (const auto& handle : cloned_nodes) {
          if (nodes_->Contains(handle)) {
            nodes_->Erase(handle);
          }
        }
        if (root_cloned_handle.IsValid()) {
          // No need to remove from root nodes since we never added it
        }
        return VisitResult::kStop;
      }
    },
    TraversalOrder::kPreOrder); // Pre-order guarantees parent visited
                                // before children

  LOG_SCOPE_F(INFO, "Traversal result");
  LOG_F(INFO, "traversal completed: {}", traversal_result.completed);
  LOG_F(INFO, "visited nodes: {}", traversal_result.nodes_visited);
  LOG_F(INFO, "filtered nodes: {}", traversal_result.nodes_filtered);

  if (!traversal_result.completed || !root_cloned) {
    DLOG_F(WARNING, "Hierarchy cloning failed or incomplete");
    return std::nullopt;
  }
  return std::make_pair(root_cloned_handle, root_cloned_impl);
}

/*!
 This method clones an entire node hierarchy, creating a new root node in this
 scene that preserves the complete structure and component data of the original
 hierarchy. The cloned hierarchy maintains all parent-child relationships and
 component configurations from the original.

 The operation creates a completely independent copy of the source hierarchy,
 allowing modifications to either the original or cloned hierarchies without
 affecting each other. All nodes in the hierarchy are cloned with their full
 component data, preserving transforms, flags, and other properties.

 ### Failure Scenarios

 - If the resource table is full and cannot accommodate the cloned nodes
 - If the \p starting_node handle is invalid (expired or invalidated)
 - If the \p starting_node is valid but its corresponding node was removed from
   its scene
 - If any individual node cloning fails due to component issues or memory
   constraints
 - If hierarchy corruption is detected in the source during traversal

 ### Post-Conditions

 - On success: Complete hierarchy exists as new root in this scene
 - On failure: Program terminates (resource exhaustion) or no changes made
 - Original hierarchy remains unmodified in all cases

 @note This method will terminate the program if the resource table is full,
 which can only be remedied by increasing the initial capacity of the table.

 @note **Cross-Scene Safety:** Source hierarchy can be from any scene and
 remains unaffected by the cloning operation.

 @note **Performance:** Efficiently handles large hierarchies through optimized
 traversal and bulk operations.

 @param starting_node The root node of the hierarchy to clone (can be from any
 scene)
 @param new_root_name The name to assign to the cloned root node

 @return A SceneNode wrapper around the handle of the newly created hierarchy
 root

 @see CloneHierarchy() for the underlying cloning implementation
 @see CreateNodeFrom() for cloning individual nodes without hierarchy
 @see CreateChildHierarchyFrom() for cloning a hierarchy as a child node
*/
auto Scene::CreateHierarchyFrom(
  const SceneNode& starting_node, const std::string& new_root_name) -> SceneNode
{
  DLOG_SCOPE_F(3, "Create Hierarchy From");

  // Use the private CloneHierarchy method to do the heavy lifting
  auto clone_result = CloneHierarchy(starting_node);
  if (!clone_result.has_value()) {
    // CloneHierarchy failed - this should not happen with a valid hierarchy
    // and sufficient capacity, so terminate the program as documented
    ABORT_F("Failed to clone hierarchy from node '{}' - this indicates either "
            "an invalid source hierarchy or insufficient scene capacity",
      starting_node.IsValid() ? "unknown" : "invalid");
  }
  auto [cloned_root_handle, cloned_root_impl] = clone_result.value();

  // Update the root node's name as requested
  cloned_root_impl->SetName(new_root_name);

  // CloneHierarchy creates an orphan hierarchy - add it as a root node
  AddRootNode(cloned_root_handle);

  // Return the cloned root as a SceneNode
  return { shared_from_this(), cloned_root_handle };
}

/*!
 This method clones the entire subtree rooted at the original node, preserving
 all parent-child relationships within the cloned hierarchy. The cloned root
 will become a child of the specified parent node in this scene.

 All nodes in the original hierarchy will be cloned with their component data
 preserved, maintaining the exact structure and properties. The hierarchy
 becomes a complete subtree under the specified parent, allowing complex scene
 composition through hierarchy grafting.

 ### Failure Scenarios

 - If the \p parent handle is not valid (expired or invalidated)
 - If the \p parent is valid but its corresponding node was removed from the
   scene
 - If the \p original_root handle is invalid or its node was removed
 - If hierarchy cloning fails due to resource constraints or corruption
 - If linking the cloned hierarchy to the parent fails

 ### Post-Conditions

 - On success: Complete hierarchy exists as subtree under specified parent
 - On failure: No changes made to this scene, parent remains unmodified
 - Original hierarchy remains unmodified in all cases

 @note This method will terminate the program if the \p parent does not belong
 to this scene. For cross-scene operations, use the appropriate adoption APIs.

 @note **Cross-Scene Safety:** Original hierarchy can be from any scene and
 remains unaffected by the cloning operation.

 @note **Atomicity:** Either the complete hierarchy is successfully cloned and
 linked, or no changes are made to the scene.

 @param parent The parent node under which to create the cloned hierarchy (must
 be in this scene)
 @param original_root The root node of the hierarchy to clone (can be from any
 scene)
 @param new_root_name The name to assign to the cloned root node

 @return A new SceneNode handle representing the cloned root node, or
 std::nullopt if any failure occurs

 @see CloneHierarchy() for the underlying cloning implementation
 @see CreateHierarchyFrom() for cloning a hierarchy as a new root
 @see CreateChildNodeFrom() for cloning individual nodes as children
*/
auto Scene::CreateChildHierarchyFrom(SceneNode& parent,
  const SceneNode& original_root, const std::string& new_root_name)
  -> std::optional<SceneNode>
{
  DLOG_SCOPE_F(3, "Create Child Hierarchy From");
  return SafeCall(NodeIsValidAndMine(parent),
    [&](const SafeCallState& state) -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, &parent);
      DCHECK_NOTNULL_F(state.node_impl);

      // Clone the entire hierarchy first
      auto clone_result = CloneHierarchy(original_root);
      if (!clone_result) {
        return std::nullopt; // Hierarchy cloning failed
      }
      auto [cloned_root_handle, cloned_root_impl] = *clone_result;
      // Update the cloned root's name as requested
      cloned_root_impl->SetName(
        new_root_name); // CloneHierarchy creates an orphan hierarchy - link it
                        // as a child
      LinkChild(parent.GetHandle(), state.node_impl, cloned_root_handle,
        cloned_root_impl);

      // Mark parent transform dirty since it now has a new child hierarchy
      state.node_impl->MarkTransformDirty();

      return SceneNode(shared_from_this(), cloned_root_handle);
    });
}
