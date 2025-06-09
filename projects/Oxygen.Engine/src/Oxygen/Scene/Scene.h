//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Concepts.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen {
template <typename T> class ResourceTable;
} // namespace oxygen

namespace oxygen::scene {

// Forward declaration of SceneTraversal.
class SceneTraversal;

//! Root of the Oxygen scene graph.
/*!
 Scene is the root of the Oxygen scene graph and the central manager for all
 scene node data, relationships, and hierarchy. It manages scene nodes via a
 resource table, where SceneNode is a lightweight handle/view to a node,
 while SceneNodeImpl holds the actual data.

 - All node data and hierarchy are owned and managed by Scene.
 - SceneNode provides navigation and logical queries, using Scene as
   context.
 - All mutating operations (creation, destruction, re-parenting, etc.) are
   routed through Scene.
 - Root node management is automated and robust; users do not manage root
   nodes directly.
 - Designed for game/3D engine use cases with hierarchy-focused operations
   that move complete subtrees to preserve relationships, safe destruction
   that prevents accidental orphaning of children, optional world transform
   preservation during hierarchy changes.
 - Atomic cross-scene operations, that either fully succeed or leave scenes
   completely unchanged.
 - Many handles or SceneNode instances may exist, referring to the same node
   data (SceneNodeImpl). The scene graph does not provide immediate
   invalidation for existing handles/SceneNode instances when a node is
   destroyed. Instead it relies on "lazy invalidation". The next time a
   handle or a SceneNode wrapper is used in a context where its underlying
   node does not exist anymore, the operation fails and the handle/SceneNode
   is invalidated.
 - API is exception free, and will return a boolean failure status when no
   return value is expected, or std::nullopt when a return value is expected
   but will not be provided.

 <b>Usage</b>:
 \code
   auto scene = std::make_shared<Scene>("MyScene");
   SceneNode node = scene->CreateNode("NodeA");
   if (node.IsValid()) {
      // ... use node
   }
 \endcode
*/
class Scene : public Composition, public std::enable_shared_from_this<Scene> {
  OXYGEN_TYPED(Scene)

  //! Implementation of a scene node, stored in a resource table.
  using SceneNodeImpl = SceneNodeImpl;

public:
  using NodeHandle = SceneNode::NodeHandle;
  using NodeTable = ResourceTable<SceneNodeImpl>;

  using OptionalRefToImpl
    = std::optional<std::reference_wrapper<SceneNodeImpl>>;
  using OptionalConstRefToImpl
    = std::optional<std::reference_wrapper<const SceneNodeImpl>>;

  /*!
   Constructs a Scene with the given name and initial capacity hint.
  */
  OXGN_SCN_API explicit Scene(
    const std::string& name, size_t initial_capacity = 1024);

  OXGN_SCN_API ~Scene() override;

  OXYGEN_MAKE_NON_COPYABLE(Scene)
  OXYGEN_MAKE_NON_MOVABLE(Scene)

  OXGN_SCN_NDAPI auto GetName() const noexcept -> std::string_view;
  OXGN_SCN_API void SetName(std::string_view name) noexcept;

  //! Creates a new root node with the given \p name and default flags.
  OXGN_SCN_NDAPI auto CreateNode(const std::string& name) -> SceneNode;

  //! Creates a new root node with the given \p name and \p flags.
  OXGN_SCN_NDAPI auto CreateNode(
    const std::string& name, SceneNode::Flags flags) -> SceneNode;

  //! Creates a new child node with the given \p name and default flags, under
  //! the given \p parent.
  OXGN_SCN_NDAPI auto CreateChildNode(const SceneNode& parent,
    const std::string& name) noexcept -> std::optional<SceneNode>;

  //! Creates a new child node with the given \p name and \p flags, under the
  //! given \p parent.
  OXGN_SCN_NDAPI auto CreateChildNode(const SceneNode& parent,
    const std::string& name, SceneNode::Flags flags) noexcept
    -> std::optional<SceneNode>;

  //! Creates a new root node by cloning the given \p original node.
  OXGN_SCN_NDAPI auto CreateNodeFrom(const SceneNode& original,
    const std::string& new_name) -> std::optional<SceneNode>;

  //! Creates a new child node by cloning the given original node under
  //! the specified parent.
  OXGN_SCN_NDAPI auto CreateChildNodeFrom(const SceneNode& parent,
    const SceneNode& original, const std::string& new_name)
    -> std::optional<SceneNode>;

  //! Creates a new root node by cloning an entire node hierarchy from the
  //! given root.
  OXGN_SCN_NDAPI auto CreateHierarchyFrom(const SceneNode& original_root,
    const std::string& new_root_name) -> SceneNode;

  //! Creates a new child hierarchy by cloning an entire node hierarchy
  //! under the given parent.
  OXGN_SCN_NDAPI auto CreateChildHierarchyFrom(const SceneNode& parent,
    const SceneNode& original_root, const std::string& new_root_name)
    -> std::optional<SceneNode>;

  //! Destroys the given node if it has no children.
  /*!
   Destroys a single node that has no children. If the node has children,
   the operation will fail and return false. Use DestroyNodeHierarchy()
   to destroy a node along with all its descendants.

   \param node Node to destroy (must have no children)
   \return true if the node was destroyed, false if the node is invalid or
           was not found in the scene.
  */
  OXGN_SCN_API auto DestroyNode(SceneNode& node) noexcept -> bool;

  //! Destroys multiple nodes that have no children.
  /*!
   Batch operation for destroying multiple nodes that have no children.
   Each node is processed individually - nodes with children are skipped.

   \param nodes Nodes to destroy (each must have no children)
   \return Vector indicating success (true) or failure (false) for each
           node at the same index

   \note **Partial Success:** Nodes with children or that are invalid are
         skipped.
  */
  OXGN_SCN_API auto DestroyNodes(std::span<SceneNode> nodes) noexcept
    -> std::vector<uint8_t>;

  //! Recursively destroys the given node and all its descendants.
  /*!
   Destroys an entire node hierarchy starting from the given root node. All
   descendants are destroyed recursively, regardless of their individual child
   counts.

   \param root Root of hierarchy to destroy
   \return true if the hierarchy was destroyed, false if root was not
   found
  */
  OXGN_SCN_API auto DestroyNodeHierarchy(SceneNode& root) noexcept -> bool;

  //! Recursively destroys multiple node hierarchies.
  /*!
   Batch operation for destroying multiple complete node hierarchies.
   Each hierarchy is destroyed entirely, including all descendants.

   \param hierarchy_roots Roots of hierarchies to destroy
   \return Vector indicating success (true) or failure (false) for each
           hierarchy at the same index

   \note **Partial Success:** Invalid roots are skipped and do not affect
         other hierarchies.
  */
  OXGN_SCN_API auto DestroyNodeHierarchies(
    std::span<SceneNode> hierarchy_roots) noexcept -> std::vector<uint8_t>;

  //! Checks if the data object for the given \p node is still in the
  //! scene.
  /*!
   This method will return true if the node data exists, and false if it
   does not. If the node data does not exist anymore, it will not
   invalidate the \p node, assuming that this is a genuine check for the
   node existence.
  */
  OXGN_SCN_NDAPI auto Contains(const SceneNode& node) const noexcept -> bool;

  // Statistics and Management
  OXGN_SCN_NDAPI auto GetNodeCount() const noexcept -> size_t;
  OXGN_SCN_NDAPI auto GetNodes() const noexcept -> const NodeTable&;
  OXGN_SCN_NDAPI auto IsEmpty() const noexcept -> bool;
  OXGN_SCN_API void DefragmentStorage();
  OXGN_SCN_API void Clear() noexcept;

  // Hierarchy management

  OXGN_SCN_NDAPI auto GetParent(const SceneNode& node) const noexcept
    -> std::optional<SceneNode>;
  OXGN_SCN_NDAPI auto GetParentUnsafe(const SceneNode& node,
    const SceneNodeImpl* node_impl) const -> std::optional<SceneNode>;
  OXGN_SCN_NDAPI auto HasParent(const SceneNode& node) const noexcept -> bool;
  OXGN_SCN_NDAPI auto HasParentUnsafe(
    const SceneNode& node, const SceneNodeImpl* node_impl) const -> bool;
  OXGN_SCN_NDAPI auto HasChildren(const SceneNode& node) const noexcept -> bool;
  OXGN_SCN_NDAPI auto HasChildrenUnsafe(
    const SceneNode& node, const SceneNodeImpl* node_impl) const -> bool;
  OXGN_SCN_NDAPI auto GetFirstChild(const SceneNode& node) const noexcept
    -> std::optional<SceneNode>;
  auto GetFirstChildUnsafe(const SceneNode& node,
    const SceneNodeImpl* node_impl) const -> std::optional<SceneNode>;
  OXGN_SCN_NDAPI auto GetNextSibling(const SceneNode& node) const noexcept
    -> std::optional<SceneNode>;
  auto GetNextSiblingUnsafe(const SceneNode& node,
    const SceneNodeImpl* node_impl) const -> std::optional<SceneNode>;
  OXGN_SCN_NDAPI auto GetPrevSibling(const SceneNode& node) const noexcept
    -> std::optional<SceneNode>;
  auto GetPrevSiblingUnsafe(const SceneNode& node,
    const SceneNodeImpl* node_impl) const -> std::optional<SceneNode>;

  OXGN_SCN_NDAPI auto GetRootNodes() const -> std::vector<SceneNode>;
  OXGN_SCN_NDAPI auto GetRootHandles() const -> std::span<const NodeHandle>;

  OXGN_SCN_NDAPI auto GetChildrenCount(const SceneNode& parent) const noexcept
    -> size_t;
  OXGN_SCN_NDAPI auto GetChildren(const SceneNode& parent) const
    -> std::vector<NodeHandle>;

  // Node search and query
  OXGN_SCN_NDAPI auto FindNodeByName(std::string_view name) const
    -> std::optional<SceneNode>;
  OXGN_SCN_NDAPI auto FindNodesByName(std::string_view name) const
    -> std::vector<SceneNode>;

  // High-performance traversal access
  OXGN_SCN_NDAPI auto Traverse() const -> const SceneTraversal&;

  // Update system
  // TODO: Implement a proper update system for the scene graph.
  OXGN_SCN_API void Update(bool skip_dirty_flags = false) noexcept;

  //! @{
  //! Get the SceneNodeImpl for the given SceneNode.
  /*!
   If the node data does not exist anymore, this method will return
   std::nullopt, and will invalidate the node.

   Expects the node to be valid, or it will terminate the program.
  */
  OXGN_SCN_NDAPI auto GetNodeImpl(const SceneNode& node) noexcept
    -> OptionalRefToImpl;
  OXGN_SCN_NDAPI auto GetNodeImpl(const SceneNode& node) const noexcept
    -> OptionalConstRefToImpl;
  //! @}

  //! @{
  //! Get the SceneNodeImpl for the given handle.
  /*!
   Expects the handle to be valid, and the node to exist, or it will
   terminate the program.
  */
  OXGN_SCN_NDAPI auto GetNodeImplRef(const NodeHandle& handle) noexcept
    -> SceneNodeImpl&;
  OXGN_SCN_NDAPI auto GetNodeImplRef(const NodeHandle& handle) const noexcept
    -> const SceneNodeImpl&;

  OXGN_SCN_NDAPI auto GetNodeImplRefUnsafe(const NodeHandle& handle)
    -> SceneNodeImpl&;
  OXGN_SCN_NDAPI auto GetNodeImplRefUnsafe(const NodeHandle& handle) const
    -> const SceneNodeImpl&;
  //! @}

  //! Provides a SceneNode for a NodeHandle, or std::nullopt if the \p
  //! handle does not correspond to an existing node.
  OXGN_SCN_NDAPI auto GetNode(const NodeHandle& handle) const noexcept
    -> std::optional<SceneNode>;

public:
  //=== Node Re-parenting API (Same-Scene Only) ===-------------------------//

  //! Re-parent a node hierarchy to a new parent within this scene
  /*!
   Changes the parent of an existing node within this scene, moving the
   entire hierarchy rooted at that node. Both nodes must belong to this
   scene.

   \return true if re-parenting succeeded, false if invalid nodes or would
   create cycle

   \note **Atomicity:** Only hierarchy pointers are modified without
   destroying/recreating node data. Either fully succeeds or leaves scene
   unchanged.
  */
  OXGN_SCN_NDAPI auto ReparentNode(
    const SceneNode& node, //!< must be in this scene
    const SceneNode& new_parent, //! must be in this scene
    bool preserve_world_transform = true //!< true to adjust local transform
                                         //!< to maintain world position
    ) noexcept -> bool;

  //! Make a node hierarchy a root hierarchy in this scene
  /*!
   Makes a node a root node within this scene, moving the entire hierarchy
   rooted at that node to become a top-level hierarchy.

   \param node Root of hierarchy to make root (must be in this scene)
   \param preserve_world_transform If true, adjusts local transform to
   maintain world position
   \return true if operation succeeded, false if invalid node

   \note **Atomicity:** Only hierarchy pointers are modified without
         destroying/recreating node data. Either fully succeeds or leaves
         scene unchanged.
  */
  OXGN_SCN_NDAPI auto MakeNodeRoot(const SceneNode& node,
    bool preserve_world_transform = true) noexcept -> bool;

  //! Re-parent multiple node hierarchies to a new parent within this
  //! scene
  /*!
   Batch operation for re-parenting multiple node hierarchies efficiently
   within this scene. Each node represents the root of a hierarchy that
   will be moved entirely.

   \param nodes Hierarchy roots to re-parent (all must be in this scene)
   \param new_parent New parent node (must be in this scene, use
   SceneNode{} for root)
   \param preserve_world_transform If true, adjusts local transform to
   maintain world position
   \return Vector indicating success (true) or failure (false) for each
   node at the same index

   \note **Partial Success:** Each individual re-parenting is atomic, but
         some may fail.
  */
  OXGN_SCN_NDAPI auto ReparentNodes(std::span<const SceneNode> nodes,
    const SceneNode& new_parent, bool preserve_world_transform = true) noexcept
    -> std::vector<uint8_t>;

  //! Re-parent multiple node hierarchies to become root hierarchies in this
  //! scene
  /*!
   Batch operation for making multiple node hierarchies root hierarchies within
   this scene. Each node represents the root of a hierarchy that will be moved
   entirely.

   \param nodes Hierarchy roots to make roots (all must be in this scene)
   \param preserve_world_transform If true, adjusts local transforms to
   maintain world position
   \return Vector indicating success (true) or failure (false) for each
   node at the same index

   \note **Partial Success:** Each individual operation is atomic, but some may
         fail.
  */
  OXGN_SCN_NDAPI auto MakeNodesRoot(std::span<const SceneNode> nodes,
    bool preserve_world_transform = true) noexcept -> std::vector<uint8_t>;

  //=== Node Adoption API (Cross-Scene Operations) ===------------------//

  //! Adopt a node from any scene and re-parent it to a new parent in this scene
  /*!
   Brings a node from any scene into this scene by cloning its data and
   sets its parent.

   \param node Node to adopt (can be from any scene, will be updated to
   point to this scene)
   \param new_parent New parent node (must be in this scene, use
   SceneNode{} for root)
   \param preserve_world_transform If true, adjusts local transform to
   maintain world position
   \return true if adoption succeeded, false if invalid nodes or would
   create cycle

   \note **Handle Update:** Only this specific SceneNode handle is updated
         to point to the new cloned data. Other copies become invalid.

   \note **Atomicity:** Target scene is modified first, then source scene
         cleanup.
  */
  OXGN_SCN_NDAPI auto AdoptNode(SceneNode& node, const SceneNode& new_parent,
    bool preserve_world_transform = true) noexcept -> bool;

  //! Adopt a node from any scene and make it a root node in this scene
  /*!
   Brings a node from any scene into this scene by cloning its data and
   makes it a root.

   \param node Node to adopt as root (can be from any scene, will be
   updated to point to this scene)
   \param preserve_world_transform If true, adjusts local transform to
   maintain world position
   \return true if adoption succeeded, false if invalid node

   \note **Handle Update:** Only this specific SceneNode handle is updated
         to point to the new cloned data. Other copies become invalid.

   \note **Atomicity:** Target scene is modified first, then source scene
         cleanup.
  */
  OXGN_SCN_NDAPI auto AdoptNodeAsRoot(
    SceneNode& node, bool preserve_world_transform = true) noexcept -> bool;

  //! Adopt multiple nodes from any scenes and re-parent them to a new
  //! parent in this scene
  /*!
   Batch operation for adopting multiple nodes from any scenes into this
   scene.

   \param nodes Nodes to adopt (can be from any scenes, each will be
   updated to point to this scene)
   \param new_parent New parent node (must be in this scene, use
   SceneNode{} for root)
   \param preserve_world_transform If true, adjusts local transforms to
   maintain world positions
   \return Vector indicating success (true) or failure (false) for each
   node at the same index

   \note **Partial Success:** Each individual adoption is atomic, but some
   may fail.
  */
  OXGN_SCN_NDAPI auto AdoptNodes(std::span<SceneNode> nodes,
    const SceneNode& new_parent, bool preserve_world_transform = true) noexcept
    -> std::vector<uint8_t>;

  //! Adopt multiple nodes from any scenes and make them root nodes in
  //! this scene
  /*!
   Batch operation for adopting multiple nodes from any scenes as roots in
   this scene.

   \param nodes Nodes to adopt as roots (can be from any scenes, each will
   be updated to point to this scene)
   \param preserve_world_transform If true, adjusts local transforms to
   maintain world positions
   \return Vector indicating success (true) or failure (false) for each
   node at the same index

   \note **Partial Success:** Each individual adoption is atomic, but some
   may fail.
  */
  OXGN_SCN_NDAPI auto AdoptNodesAsRoot(std::span<SceneNode> nodes,
    bool preserve_world_transform = true) noexcept -> std::vector<uint8_t>;

  //=== Hierarchy Adoption API (Cross-Scene Operations) ===------====-------//

  //! Adopt an entire node hierarchy from any scene and re-parent it in
  //! this scene
  /*!
   Brings a complete node hierarchy from any scene into this scene by
   cloning all nodes in the subtree and preserving their relationships.
   The root of the hierarchy gets a new parent.

   \param hierarchy_root Root of the hierarchy to adopt (can be from any
   scene, will be updated to point to this scene)
   \param new_parent New parent node (must be in this scene, use
   SceneNode{} for root)
   \param preserve_world_transform If true, adjusts local transforms to
   maintain world positions
   \return true if adoption succeeded, false if invalid nodes or would
   create cycle

   \note **Handle Update:** Only the specified SceneNode handle is
         updated. Other copies of handles within the hierarchy become
         invalid.

   \note **Atomicity:** Target scene is modified first with complete
         hierarchy, then source cleanup.
  */
  OXGN_SCN_NDAPI auto AdoptHierarchy(SceneNode& hierarchy_root,
    const SceneNode& new_parent, bool preserve_world_transform = true) noexcept
    -> bool;

  //! Adopt an entire node hierarchy from any scene as a new root
  //! hierarchy in this scene
  /*!
   Brings a complete node hierarchy from any scene into this scene by
   cloning all nodes in the subtree and making the root a top-level root
   in this scene.

   \param hierarchy_root Root of the hierarchy to adopt (can be from any
   scene, will be updated to point to this scene)
   \param preserve_world_transform If true, adjusts local transforms to
   maintain world positions
   \return true if adoption succeeded, false if invalid hierarchy root

   \note **Handle Update:** Only the specified SceneNode handle is
         updated. Other copies of handles within the hierarchy become
         invalid.

   \note **Atomicity:** Target scene is modified first with complete
         hierarchy, then source cleanup.
  */
  OXGN_SCN_NDAPI auto AdoptHierarchyAsRoot(SceneNode& hierarchy_root,
    bool preserve_world_transform = true) noexcept -> bool;

  //! Adopt multiple complete hierarchies from any scenes and re-parent
  //! them in this scene
  /*!
   Batch operation for adopting multiple complete hierarchies from any
   scenes into this scene. Each hierarchy is cloned entirely with all
   internal relationships preserved.

   \param hierarchy_roots Roots of hierarchies to adopt (can be from any
   scenes, each will be updated to point to this scene)
   \param new_parent New parent node (must be in this scene, use
   SceneNode{} for root)
   \param preserve_world_transform If true, adjusts local transforms to
   maintain world positions
   \return Vector indicating success (true) or failure (false) for each
   hierarchy at the same index

   \note **Partial Success:** Each individual hierarchy adoption is
         atomic, but some may fail.
  */
  OXGN_SCN_NDAPI auto AdoptHierarchies(std::span<SceneNode> hierarchy_roots,
    const SceneNode& new_parent, bool preserve_world_transform = true) noexcept
    -> std::vector<uint8_t>;

  //! Adopt multiple complete hierarchies from any scenes as new root
  //! hierarchies in this scene
  /*!
   Batch operation for adopting multiple complete hierarchies as roots in
   this scene. Each hierarchy is cloned entirely with all internal
   relationships preserved.

   \param hierarchy_roots Roots of hierarchies to adopt (can be from any
   scenes, each will be updated to point to this scene)
   \param preserve_world_transform If true, adjusts local transforms to
   maintain world positions
   \return Vector indicating success (true) or failure (false) for each
   hierarchy at the same index

   \note **Partial Success:** Each individual hierarchy adoption is
         atomic, but some may fail.
  */
  OXGN_SCN_NDAPI auto AdoptHierarchiesAsRoot(
    std::span<SceneNode> hierarchy_roots,
    bool preserve_world_transform = true) noexcept -> std::vector<uint8_t>;

  // Logging for SafeCall errors using DLOG_F
  OXGN_SCN_API static void LogSafeCallError(const char* reason) noexcept;

private:
  //! Creates a new node implementation with the given name and (optional)
  //! flags.
  template <typename... Args>
  auto CreateNodeImpl(Args&&... args) noexcept -> SceneNode;

  //! Creates a new node implementation with the given name and (optional)
  //! flags, then links it to the given parent node as a child.
  template <typename... Args>
  auto CreateChildNodeImpl(const SceneNode& parent, Args&&... args) noexcept
    -> std::optional<SceneNode>;

  //! Clones the given original node to create an orphaned node, for later being
  //! linked to a parent node or added to the roots collection.
  auto CloneNode(const SceneNode& original, const std::string& new_name)
    -> std::optional<std::pair<NodeHandle, SceneNodeImpl*>>;

  //! Clones an entire hierarchy starting from the given original node, for
  //! later being attached to a target scene as a root or a child of an existing
  //! node.
  auto CloneHierarchy(const SceneNode& original_node)
    -> std::optional<std::pair<NodeHandle, SceneNodeImpl*>>;

  //! Links a child node to a parent node in the hierarchy. Both of them
  //! must be valid and belong to this scene.
  void LinkChild(const NodeHandle& parent_handle, SceneNodeImpl* parent_impl,
    const NodeHandle& child_handle, SceneNodeImpl* child_impl) noexcept;

  //! Un-links a node_handle from its parent and siblings, preparing it
  //! for destruction.
  /*!
   This method does not destroy the node, it only removes it from the
   hierarchy. If the node must be destroyed, DestroyNode() or
   DestroyNodeHierarchy() should be used after un-linking. If it is simply
   being detached, it needs to be added to the roots set using
   AddRootNode().
  */
  void UnlinkNode(
    const NodeHandle& node_handle, SceneNodeImpl* node_impl) noexcept;

  void AddRootNode(const NodeHandle& node);
  void RemoveRootNode(const NodeHandle& node);
  void EnsureRootNodesValid() const noexcept;

  //! Check if re-parenting would create a cycle in the hierarchy
  [[nodiscard]] auto WouldCreateCycle(
    const SceneNode& node, const SceneNode& new_parent) const noexcept -> bool;

  //! Marks the transform as dirty for a node and all its descendants.
  void MarkSubtreeTransformDirty(const Scene::NodeHandle& root_handle) noexcept;

  std::shared_ptr<NodeTable> nodes_;
  //!< Set of root nodes for robust, duplicate-free management.
  std::vector<NodeHandle> root_nodes_;

  SceneTraversal* traversal_;

  //=== Validation Helpers ===------------------------------------------------//

  // State struct to hold validated pointers and eliminate redundant
  // lookups
  struct SafeCallState {
    SceneNode* node { nullptr };
    SceneNodeImpl* node_impl { nullptr };
  };

  template <typename Self, typename Validator, typename Func>
  auto SafeCall(Self* self, Validator validator, Func&& func) const noexcept;

  template <typename Validator, typename Func>
  auto SafeCall(Validator&& validator, Func&& func) const noexcept;

  template <typename Validator, typename Func>
  auto SafeCall(Validator&& validator, Func&& func) noexcept;

  //! Checks if the given node is owned by this scene.
  /*!
   This method checks if the given node has been created by this scene. It
   does not check if the node is valid or still alive.
  */
  [[nodiscard]] auto IsOwnerOf(const SceneNode& node) const -> bool;

  class BaseNodeValidator;
  class NodeIsValidValidator;
  class NodeIsValidAndInSceneValidator;
  class LeafNodeCanBeDestroyedValidator;

  auto NodeIsValidAndMine(const SceneNode& node) const
    -> NodeIsValidAndInSceneValidator;
  auto NodeIsValidAndInScene(const SceneNode& node) const
    -> NodeIsValidAndInSceneValidator;
  auto LeafNodeCanBeDestroyed(const SceneNode& node) const
    -> LeafNodeCanBeDestroyedValidator;
};

} // namespace oxygen::scene
