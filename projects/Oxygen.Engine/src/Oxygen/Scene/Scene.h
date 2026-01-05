//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <bitset>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen {
template <typename T> class ResourceTable;
} // namespace oxygen

namespace oxygen::scene {

class SceneQuery;
class SceneEnvironment;
template <typename SceneT> class SceneTraversal;

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

  constexpr static size_t kInitialCapacity = 1024;

public:
  using NodeTable = ResourceTable<SceneNodeImpl>;
  using SceneId = NodeHandle::SceneId;

  using OptionalRefToImpl
    = std::optional<std::reference_wrapper<SceneNodeImpl>>;
  using OptionalConstRefToImpl
    = std::optional<std::reference_wrapper<const SceneNodeImpl>>;

  using NonMutatingTraversal = SceneTraversal<const Scene>;
  using MutatingTraversal = SceneTraversal<Scene>;

  //=== Basic API ===---------------------------------------------------------//

  //! Constructs a Scene with the given name and initial capacity hint.
  OXGN_SCN_API explicit Scene(
    const std::string& name, size_t initial_capacity = kInitialCapacity);

  OXGN_SCN_API ~Scene() override;

  OXYGEN_MAKE_NON_COPYABLE(Scene)
  OXYGEN_MAKE_NON_MOVABLE(Scene)

  OXGN_SCN_NDAPI auto GetName() const noexcept -> std::string_view;
  OXGN_SCN_API void SetName(std::string_view name) noexcept;

  //! Gets the unique ID of this scene (0-255).
  OXGN_SCN_NDAPI auto GetId() const noexcept { return scene_id_; }

  //=== Scene-Global Environment (Optional) ===------------------------------//

  //! Returns true if the scene has an environment attached.
  OXGN_SCN_NDAPI auto HasEnvironment() const noexcept -> bool
  {
    return environment_ != nullptr;
  }

  //! Gets the scene environment (non-owning), or nullptr when absent.
  OXGN_SCN_NDAPI auto GetEnvironment() noexcept
    -> observer_ptr<SceneEnvironment>
  {
    return observer_ptr<SceneEnvironment>(environment_.get());
  }

  //! Gets the scene environment (non-owning), or nullptr when absent.
  OXGN_SCN_NDAPI auto GetEnvironment() const noexcept
    -> observer_ptr<const SceneEnvironment>
  {
    return observer_ptr<const SceneEnvironment>(environment_.get());
  }

  //! Sets (replaces) the scene environment, taking ownership.
  /*!
   @param environment The new environment. Passing nullptr clears the current
     environment.
  */
  OXGN_SCN_API auto SetEnvironment(
    std::unique_ptr<SceneEnvironment> environment) noexcept -> void;

  //! Removes the current scene environment (if any).
  OXGN_SCN_API auto ClearEnvironment() noexcept -> void;

  //=== Node Factories - Creation ===-----------------------------------------//

  //! Creates a new root node with the given \p name and default flags.
  OXGN_SCN_NDAPI auto CreateNode(const std::string& name) -> SceneNode;

  //! Creates a new root node with the given \p name and \p flags.
  OXGN_SCN_NDAPI auto CreateNode(
    const std::string& name, SceneNode::Flags flags) -> SceneNode;

  //! Creates a new child node with the given \p name and default flags, under
  //! the given \p parent.
  OXGN_SCN_NDAPI auto CreateChildNode(SceneNode& parent,
    const std::string& name) noexcept -> std::optional<SceneNode>;

  //! Creates a new child node with the given \p name and \p flags, under the
  //! given \p parent.
  OXGN_SCN_NDAPI auto CreateChildNode(
    SceneNode& parent, const std::string& name, SceneNode::Flags flags) noexcept
    -> std::optional<SceneNode>;

  //! Creates a new root node by cloning the given \p original node.
  OXGN_SCN_NDAPI auto CreateNodeFrom(SceneNode& original,
    const std::string& new_name) -> std::optional<SceneNode>;

  //! Creates a new child node by cloning the given original node under
  //! the specified parent.
  OXGN_SCN_NDAPI auto CreateChildNodeFrom(
    SceneNode& parent, SceneNode& original, const std::string& new_name)
    -> std::optional<SceneNode>;

  //! Creates a new root node by cloning an entire node hierarchy from the
  //! given root.
  OXGN_SCN_NDAPI auto CreateHierarchyFrom(const SceneNode& original_root,
    const std::string& new_root_name) -> SceneNode;

  //! Creates a new child hierarchy by cloning an entire node hierarchy
  //! under the given parent.
  OXGN_SCN_NDAPI auto CreateChildHierarchyFrom(SceneNode& parent,
    const SceneNode& original_root, const std::string& new_root_name)
    -> std::optional<SceneNode>;

  //=== Node Factories - Destruction ===--------------------------------------//

  //! Destroys the given node if it has no children, and invalidates it.
  OXGN_SCN_API auto DestroyNode(SceneNode& node) noexcept -> bool;

  //! Destroys multiple leaf nodes in a batch operation. Failure of on node does
  //! not affect others.
  OXGN_SCN_API auto DestroyNodes(std::span<SceneNode> nodes) noexcept
    -> std::vector<uint8_t>;

  //! Destroys the given node and all its descendants.
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

  //=== Graph Queries ===-----------------------------------------------------//

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

  OXGN_SCN_NDAPI auto GetParent(SceneNode& node) const noexcept
    -> std::optional<SceneNode>;
  OXGN_SCN_NDAPI auto HasParent(SceneNode& node) const noexcept -> bool;
  OXGN_SCN_NDAPI auto HasChildren(SceneNode& node) const noexcept -> bool;
  OXGN_SCN_NDAPI auto GetFirstChild(SceneNode& node) const noexcept
    -> std::optional<SceneNode>;
  OXGN_SCN_NDAPI auto GetNextSibling(SceneNode& node) const noexcept
    -> std::optional<SceneNode>;
  OXGN_SCN_NDAPI auto GetPrevSibling(SceneNode& node) const noexcept
    -> std::optional<SceneNode>;

  OXGN_SCN_NDAPI auto GetParentUnsafe(SceneNode& node,
    const SceneNodeImpl* node_impl) const -> std::optional<SceneNode>;
  OXGN_SCN_NDAPI auto HasParentUnsafe(
    SceneNode& node, const SceneNodeImpl* node_impl) const -> bool;
  OXGN_SCN_NDAPI auto HasChildrenUnsafe(
    SceneNode& node, const SceneNodeImpl* node_impl) const -> bool;
  OXGN_SCN_NDAPI auto GetFirstChildUnsafe(SceneNode& node,
    const SceneNodeImpl* node_impl) const -> std::optional<SceneNode>;
  OXGN_SCN_NDAPI auto GetNextSiblingUnsafe(SceneNode& node,
    const SceneNodeImpl* node_impl) const -> std::optional<SceneNode>;
  OXGN_SCN_NDAPI auto GetPrevSiblingUnsafe(SceneNode& node,
    const SceneNodeImpl* node_impl) const -> std::optional<SceneNode>;

  OXGN_SCN_NDAPI auto GetRootNodes() const -> std::vector<SceneNode>;
  OXGN_SCN_NDAPI auto GetRootHandles() const -> std::span<const NodeHandle>;

  OXGN_SCN_NDAPI auto GetChildrenCount(const SceneNode& parent) const noexcept
    -> size_t;
  OXGN_SCN_NDAPI auto GetChildren(const SceneNode& parent) const
    -> std::vector<NodeHandle>;

  //! Creates a high-performance query interface for this scene.
  OXGN_SCN_API auto Query() const -> SceneQuery;

  //=== Node Re-parenting API (Same-Scene Only) ===-------------------------//

  //! Re-parents a node hierarchy to a new parent within this scene, preserving
  //! all internal relationships within the moved hierarchy.
  OXGN_SCN_NDAPI auto ReparentNode(SceneNode& node, SceneNode& new_parent,
    bool preserve_world_transform = true) noexcept -> bool;

  //! Re-parent multiple node hierarchies to the same new parent within this
  //! scene.
  OXGN_SCN_NDAPI auto ReparentNodes(std::span<SceneNode> nodes,
    SceneNode& new_parent, bool preserve_world_transform = true) noexcept
    -> std::vector<uint8_t>;

  //! Makes a node a root node within this scene, moving the entire hierarchy
  //! rooted at that node to become a top-level hierarchy.
  OXGN_SCN_NDAPI auto MakeNodeRoot(
    SceneNode& node, bool preserve_world_transform = true) noexcept -> bool;

  //! Make multiple nodes root nodes within this scene, moving their entire
  //! hierarchies to become top-level hierarchies.
  OXGN_SCN_NDAPI auto MakeNodesRoot(std::span<SceneNode> nodes,
    bool preserve_world_transform = true) noexcept -> std::vector<uint8_t>;

  //=== Node Adoption API (Cross-Scene Operations) ===------------------------//

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

   \note **Partial Success:** Each individual operation is atomic, but some may
         fail.
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

  // Logging for SafeCall errors using DLOG_F (debug builds only).
  OXGN_SCN_API static void LogSafeCallError(const char* reason) noexcept;

  //=== Traversal ===---------------------------------------------------------//

  // High-performance traversal access.
  OXGN_SCN_NDAPI auto Traverse() const -> NonMutatingTraversal;

  // High-performance traversal access.
  OXGN_SCN_NDAPI auto Traverse() -> MutatingTraversal;

  // TODO: Implement a proper update system for the scene graph.
  OXGN_SCN_API void Update(bool skip_dirty_flags = false) noexcept;

  //=== Low-level Access ===--------------------------------------------------//

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
  OXGN_SCN_NDAPI auto GetNodeImplRef(
    const ResourceHandle& handle) const noexcept -> const SceneNodeImpl&;
  OXGN_SCN_NDAPI auto GetNodeImplRef(const ResourceHandle& handle) noexcept
    -> SceneNodeImpl&;

  OXGN_SCN_NDAPI auto GetNodeImplRefUnsafe(const ResourceHandle& handle) const
    -> const SceneNodeImpl&;
  OXGN_SCN_NDAPI auto GetNodeImplRefUnsafe(const ResourceHandle& handle)
    -> SceneNodeImpl&;
  //! @}

  //! Provides a SceneNode for a NodeHandle, or std::nullopt if the \p
  //! handle does not correspond to an existing node.
  OXGN_SCN_NDAPI auto GetNode(const NodeHandle& handle) const noexcept
    -> std::optional<SceneNode>;

private:
  //! Check result for partial failure of a batch operation.
  void LogPartialFailure(const std::vector<uint8_t>& results,
    const std::string& operation_name) const;

  //! Creates a new node implementation with the given name and (optional)
  //! flags.
  template <typename... Args>
  auto CreateNodeImpl(Args&&... args) noexcept -> SceneNode;

  //! Creates a new node implementation with the given name and (optional)
  //! flags, then links it to the given parent node as a child.
  template <typename... Args>
  auto CreateChildNodeImpl(SceneNode& parent, Args&&... args) noexcept
    -> std::optional<SceneNode>;

  //! Clones the given original node to create an orphaned node, for later being
  //! linked to a parent node or added to the roots collection.
  auto CloneNode(SceneNode& original, const std::string& new_name)
    -> std::optional<std::pair<NodeHandle, SceneNodeImpl*>>;

  //! Clones an entire hierarchy starting from the given original node to create
  //! an orphaned sub-tree, for later being attached to a target scene as a root
  //! or a child of an existing node.
  auto CloneHierarchy(const SceneNode& starting_node)
    -> std::optional<std::pair<NodeHandle, SceneNodeImpl*>>;

  //! Links a child node to a parent node in the hierarchy. Both of them must be
  //! valid and belong to this scene. The child node must be an orphan, newly
  //! created or resulting from a prior call to UnlinkNode().
  void LinkChild(const NodeHandle& parent_handle, SceneNodeImpl* parent_impl,
    const NodeHandle& child_handle, SceneNodeImpl* child_impl) noexcept;

  //! Un-links a node_handle from its parent and siblings, making it an orphan.
  //! Follow-up action is required to destroy it, add it to the roots, or
  //! re-parent it.
  void UnlinkNode(
    const NodeHandle& node_handle, SceneNodeImpl* node_impl) noexcept;

  void AddRootNode(const NodeHandle& node);
  void RemoveRootNode(const NodeHandle& node);
  void EnsureRootNodesValid() const noexcept;

  auto RelativeIsAlive(const NodeHandle& relative,
    const SceneNode& target) const -> std::optional<SceneNode>;

  auto RelativeIsAliveOrInvalidateTarget(const NodeHandle& relative,
    SceneNode& target) const -> std::optional<SceneNode>;

  //! Check if re-parenting would create a cycle in the hierarchy
  [[nodiscard]] auto WouldCreateCycle(
    const SceneNode& node, const SceneNode& new_parent) const noexcept -> bool;

  //! Marks the transform as dirty for a node and all its descendants.
  void MarkSubtreeTransformDirty(const NodeHandle& root_handle) noexcept;

  //! Preserves the world transform of a node when re-parenting it.
  static void PreserveWorldTransform(SceneNode& node, SceneNodeImpl* node_impl,
    SceneNodeImpl* new_parent_impl = nullptr) noexcept;

  std::shared_ptr<NodeTable> nodes_;
  //!< Set of root nodes for robust, duplicate-free management.
  std::vector<NodeHandle> root_nodes_;

  std::unique_ptr<SceneEnvironment> environment_;

  //! Unique ID for this scene (0-255)
  SceneId scene_id_;

  //=== Validation Helpers ===------------------------------------------------//

  // State struct to hold validated pointers and eliminate redundant
  // lookups
  struct SafeCallState {
    SceneNode* node { nullptr };
    SceneNodeImpl* node_impl { nullptr };
  };

  //! Specialization of SafeCall for the Scene class.
  template <typename Self, typename Validator, typename Func>
  auto SafeCallImpl(
    Self* self, Validator validator, Func&& func) const noexcept;

  //! Non-mutable version of the SceneNode SafeCall.
  template <typename Validator, typename Func>
  auto SafeCall(Validator&& validator, Func&& func) const noexcept;

  //! Mutable version of the SceneNode SafeCall.
  template <typename Validator, typename Func>
  auto SafeCall(Validator&& validator, Func&& func) noexcept;

  //! Checks if the given node is owned by this scene.
  /*!
   This method checks if the given node has been created by this scene. It
   does not check if the node is valid or still alive.
  */
  [[nodiscard]] auto IsOwnerOf(const SceneNode& node) const -> bool;

  class BaseNodeValidator; // Mutating due to lazy invalidation
  class NodeIsValidValidator; // Mutating due to lazy invalidation
  class NodeIsValidAndInSceneValidator; // Mutating due to lazy invalidation
  class LeafNodeCanBeDestroyedValidator; // Mutating due to lazy invalidation

  OXGN_SCN_NDAPI auto NodeIsValidAndMine(SceneNode& node) const
    -> NodeIsValidAndInSceneValidator;
  OXGN_SCN_NDAPI auto NodeIsValidAndInScene(SceneNode& node) const
    -> NodeIsValidAndInSceneValidator;
  OXGN_SCN_NDAPI auto LeafNodeCanBeDestroyed(SceneNode& node) const
    -> LeafNodeCanBeDestroyedValidator;
};

} // namespace oxygen::scene
