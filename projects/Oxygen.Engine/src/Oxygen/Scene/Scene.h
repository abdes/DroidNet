//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>
#include <span>

#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen {

template <typename T>
class ResourceTable;

namespace scene {

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

        template <typename T>
        using optional_ref = std::optional<std::reference_wrapper<T>>;
        template <typename T>
        using optional_cref = std::optional<std::reference_wrapper<const T>>;

        /*!
         Constructs a Scene with the given name and initial capacity hint.
        */
        OXYGEN_SCENE_API explicit Scene(
            const std::string& name, size_t initial_capacity = 1024);

        OXYGEN_SCENE_API ~Scene() override = default;

        OXYGEN_MAKE_NON_COPYABLE(Scene)
        OXYGEN_MAKE_NON_MOVABLE(Scene)

        [[nodiscard]] OXYGEN_SCENE_API auto GetName() const noexcept -> std::string_view;
        OXYGEN_SCENE_API void SetName(std::string_view name) noexcept;

        //! Creates a new root node with the given name and default flags.
        /*!
         This method will only fail if the resource table holding scene data is
         full, which can only be remedied by increasing the initial capacity of
         the table. Therefore, a failure is a fatal error that will result in
         the application terminating.
        */
        [[nodiscard]] OXYGEN_SCENE_API auto CreateNode(const std::string& name)
            -> SceneNode;

        //! Creates a new root node with the given name and flags.
        /*!
         This method will only fail if the resource table holding scene data is
         full, which can only be remedied by increasing the initial capacity of
         the table. Therefore, a failure is a fatal error that will result in
         the application terminating.
        */
        [[nodiscard]] OXYGEN_SCENE_API auto CreateNode(
            const std::string& name, SceneNode::Flags flags)
            -> SceneNode;

        //! Creates a new child node under the given parent.
        /*!
         This method will terminate the program if the \p parent is not valid,
         as this is a programming error. It may fail if the \p parent is valid
         but its corresponding node was removed from the scene. In such as case,
         it will return std::nullopt and invalidate the \p parent node.
        */
        [[nodiscard]] OXYGEN_SCENE_API auto CreateChildNode(
            const SceneNode& parent, const std::string& name)
            -> std::optional<SceneNode>;

        //! Creates a new child node under the given parent, using the specified
        //! flags.
        /*!
         This method will terminate the program if the \p parent is not valid,
         as this is a programming error. It may fail if the \p parent is valid
         but its corresponding node was removed from the scene. In such as case,
         it will return std::nullopt and invalidate the \p parent node.
        */
        [[nodiscard]] OXYGEN_SCENE_API auto CreateChildNode(
            const SceneNode& parent, const std::string& name, SceneNode::Flags flags)
            -> std::optional<SceneNode>;

        //! Creates a new root node by cloning the given original node.
        /*!
         This method clones the original node (preserving its component data) and creates
         a new root node in this scene with the specified name. The cloned node will be
         orphaned (no hierarchy relationships) and assigned the new name.

         This method will only fail if the resource table holding scene data is
         full, which can only be remedied by increasing the initial capacity of
         the table. Therefore, a failure is a fatal error that will result in
         the application terminating.

         \param original The node to clone (can be from any scene)
         \param new_name The name to assign to the cloned node
         \return A new SceneNode handle representing the cloned node in this scene
        */
        [[nodiscard]] OXYGEN_SCENE_API auto CreateNodeFrom(
            const SceneNode& original, const std::string& new_name)
            -> SceneNode;

        //! Creates a new child node by cloning the given original node under
        //! the specified parent.
        /*!
         This method clones the original node (preserving its component data)
         and creates a new child node under the given parent in this scene with
         the specified name. The cloned node will become a child of the parent
         node.

         \param parent The parent node under which to create the cloned child
         \param original The original node to clone (can be from this or another scene)
         \param new_name The name to assign to the cloned child node

         \return An optional SceneNode handle representing the cloned child
                 node, or std::nullopt if the parent is invalid
        */
        [[nodiscard]] OXYGEN_SCENE_API auto CreateChildNodeFrom(
            const SceneNode& parent, const SceneNode& original,
            const std::string& new_name)
            -> std::optional<SceneNode>;

        //! Creates a new root node by cloning an entire node hierarchy from the
        //! given root.
        /*!
         This method recursively clones the entire subtree rooted at the
         original node, preserving all parent-child relationships within the
         cloned hierarchy. The cloned root will become a new root node in this
         scene with the specified name.

         All nodes in the original hierarchy will be cloned with their component
         data preserved, and new names will be generated based on the original
         names. The hierarchy structure is maintained exactly as in the
         original.

         This method will only fail if the resource table holding scene data is
         full, which can only be remedied by increasing the initial capacity of
         the table. Therefore, a failure is a fatal error that will result in
         the application terminating.

         \param original_root The root node of the hierarchy to clone (can be from any scene)
         \param new_root_name The name to assign to the cloned root node

         \return A new SceneNode handle representing the cloned root node in
                 this scene
        */
        [[nodiscard]] OXYGEN_SCENE_API auto CreateHierarchyFrom(
            const SceneNode& original_root, const std::string& new_root_name)
            -> SceneNode;

        //! Creates a new child hierarchy by cloning an entire node hierarchy
        //! under the given parent.
        /*!
         This method recursively clones the entire subtree rooted at the
         original node, preserving all parent-child relationships within the
         cloned hierarchy. The cloned root will become a child of the specified
         parent node.

         All nodes in the original hierarchy will be cloned with their component
         data preserved, and new names will be generated based on the original
         names. The hierarchy structure is maintained exactly as in the
         original.

         This method will terminate the program if the \p parent is not valid,
         as this is a programming error. It may fail if the \p parent is valid
         but its corresponding node was removed from the scene. In such case, it
         will return std::nullopt and invalidate the \p parent node.

         \param parent The parent node under which to create the cloned hierarchy
         \param original_root The root node of the hierarchy to clone (can be from any scene)
         \param new_root_name The name to assign to the cloned root node

         \return A new SceneNode handle representing the cloned root node, or
                 std::nullopt if parent is invalid
        */
        [[nodiscard]] OXYGEN_SCENE_API auto CreateChildHierarchyFrom(
            const SceneNode& parent, const SceneNode& original_root,
            const std::string& new_root_name)
            -> std::optional<SceneNode>;

        //! Destroys the given node.
        /*!
         \return true if the node was destroyed, false if it was not found, and
                 will always invalidate the \p node.
        */
        OXYGEN_SCENE_API auto DestroyNode(SceneNode& node) -> bool;

        // TODO: add sub-tree cloning

        //! Recursively destroys the given node and all its descendants.
        /*!
         \return true, unless \p node was not found. In such a case, \p node is
                 also invalidated. This method will silently skip descendants
                 that are not there anymore, but will invalidate them anyway.
        */
        OXYGEN_SCENE_API auto DestroyNodeHierarchy(SceneNode& root) -> bool;

        //! Checks if the data object for the given \p node is still in the
        //! scene.
        /*!
         This method will return true if the node data exists, and false if it
         does not. If the node data does not exist anymore, it will not
         invalidate the \p node, assuming that this is a genuine check for the
         node existence.
        */
        [[nodiscard]] OXYGEN_SCENE_API auto Contains(const SceneNode& node) const noexcept -> bool;

        //! Checks if the data object for the given \p handle is still in the
        //! scene.
        /*!
         This method will return true if the node data exists, and false if it
         does not. If the node data does not exist anymore, it will not
         invalidate the \p handle, assuming that this is a genuine check for the
         node existence.
        */
        [[nodiscard]] OXYGEN_SCENE_API auto Contains(const NodeHandle& handle) const noexcept -> bool;

        // Statistics and Management
        [[nodiscard]] OXYGEN_SCENE_API auto GetNodeCount() const -> size_t;
        [[nodiscard]] auto GetNodes() const -> const NodeTable&;
        [[nodiscard]] OXYGEN_SCENE_API auto IsEmpty() const -> bool;
        OXYGEN_SCENE_API void DefragmentStorage();
        OXYGEN_SCENE_API void Clear();

        // Hierarchy management

        [[nodiscard]] OXYGEN_SCENE_API auto GetParent(const SceneNode& node) const -> std::optional<SceneNode>;
        [[nodiscard]] OXYGEN_SCENE_API auto GetFirstChild(const SceneNode& node) const -> std::optional<SceneNode>;
        [[nodiscard]] OXYGEN_SCENE_API auto GetNextSibling(const SceneNode& node) const -> std::optional<SceneNode>;
        [[nodiscard]] OXYGEN_SCENE_API auto GetPrevSibling(const SceneNode& node) const -> std::optional<SceneNode>;

        [[nodiscard]] OXYGEN_SCENE_API auto GetRootNodes() const -> std::span<const NodeHandle>;
        [[nodiscard]] OXYGEN_SCENE_API auto GetRootNodes() -> std::span<NodeHandle>;
        [[nodiscard]] OXYGEN_SCENE_API auto GetChildrenCount(const SceneNode& parent) const -> size_t;
        [[nodiscard]] OXYGEN_SCENE_API auto GetChildren(const SceneNode& parent) const -> std::vector<NodeHandle>;

        // Update system
        // TODO: Implement a proper update system for the scene graph.
        OXYGEN_SCENE_API void Update(bool skip_dirty_flags = false);

        //! @{
        //! Get the SceneNodeImpl for the given SceneNode.
        /*!
         If the node data does not exist anymore, this method will return
         std::nullopt, and will invalidate the node.

         Expects the node to be valid, or it will terminate the program.
        */
        [[nodiscard]] OXYGEN_SCENE_API auto GetNodeImpl(const SceneNode& node) noexcept
            -> optional_ref<SceneNodeImpl>;
        [[nodiscard]] OXYGEN_SCENE_API auto GetNodeImpl(const SceneNode& node) const noexcept
            -> optional_cref<SceneNodeImpl>;
        //! @}

        //! @{
        //! Get the SceneNodeImpl for the given handle.
        /*!
         Expects the handle to be valid, and the node to exist, or it will
         terminate the program.
        */
        [[nodiscard]] OXYGEN_SCENE_API auto GetNodeImplRef(const NodeHandle& handle) noexcept
            -> SceneNodeImpl&;
        [[nodiscard]] OXYGEN_SCENE_API auto GetNodeImplRef(const NodeHandle& handle) const noexcept
            -> const SceneNodeImpl&;
        //! @}

        //! Provides a SceneNode for a NodeHandle, or std::nullopt if the \p handle
        //! does not correspond to an existing node.
        [[nodiscard]] OXYGEN_SCENE_API auto GetNode(const NodeHandle& handle) const noexcept
            -> std::optional<SceneNode>;

    private:
        //! Creates a new node implementation with the given name and (optional)
        //! flags.
        /*!
         This call will never fail, unless the resource table is full. In such a
         case, the application will terminate.
        */
        template <typename... Args>
        auto CreateNodeImpl(Args&&... args) -> SceneNode;

        //! Creates a new node implementation with the given name and (optional)
        //! flags, then links it to the given parent node as a child.
        /*!
         This calll will never fail, unless the resource table is full. In such
         a case, the application will terminate.

         Expects that the \p parent node is valid, and belongs to this scene, or
         it will terminate the program. This scenario is clearly a programming
         error, that must be fixed by the developer.
        */
        template <typename... Args>
        auto CreateChildNodeImpl(const SceneNode& parent, Args&&... args) -> SceneNode;

        //! Links a child node to a parent node in the hierarchy. Both of them
        //! must be valid and belong to this scene.
        void LinkChild(const SceneNode& parent, const SceneNode& child) noexcept;

        //! Un-links a node_handle from its parent and siblings, preparing it for
        //! destruction.
        /*!
         This method does not destroy the node, it only removes it from the
         hierarchy. If the node must be destroyed, DestroyNode() or
         DestroyNodeHierarchy() should be used after un-linking. If it is simply
         being detached, it needs to be added to the roots set using
         AddRootNode().
        */
        void UnlinkNode(const SceneNode& node) noexcept;

        void AddRootNode(const NodeHandle& node);
        void RemoveRootNode(const NodeHandle& node);
        void EnsureRootNodesValid() const;

        std::shared_ptr<NodeTable> nodes_;
        //!< Set of root nodes for robust, duplicate-free management.
        std::vector<NodeHandle> root_nodes_;
    };

} // namespace scene
} // namespace oxygen
