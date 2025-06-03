//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

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

        //! Destroys the given node.
        /*!
         \return true if the node was destroyed, false if it was not found, and
                 will always invalidate the \p node.
        */
        OXYGEN_SCENE_API auto DestroyNode(SceneNode& node) const -> bool;

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
        [[nodiscard]] auto GetNodeCount() const;
        [[nodiscard]] auto GetNodes() const -> const NodeTable&;
        [[nodiscard]] auto IsEmpty() const -> bool;
        void DefragmentStorage();
        void Clear();

        // Hierarchy management

        [[nodiscard]] OXYGEN_SCENE_API auto GetParent(const SceneNode& node) const -> std::optional<SceneNode>;
        [[nodiscard]] OXYGEN_SCENE_API auto GetFirstChild(const SceneNode& node) const -> std::optional<SceneNode>;
        [[nodiscard]] OXYGEN_SCENE_API auto GetNextSibling(const SceneNode& node) const -> std::optional<SceneNode>;
        [[nodiscard]] OXYGEN_SCENE_API auto GetPrevSibling(const SceneNode& node) const -> std::optional<SceneNode>;

        [[nodiscard]] OXYGEN_SCENE_API auto GetRootNodes() const -> std::vector<NodeHandle>;
        [[nodiscard]] OXYGEN_SCENE_API auto GetChildrenCount(const SceneNode& parent) const -> size_t;
        [[nodiscard]] OXYGEN_SCENE_API auto GetChildren(const SceneNode& parent) const -> std::vector<NodeHandle>;

        // Update system
        OXYGEN_SCENE_API void Update();

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
        //! Creates a new node implementation with the given name and default
        //! flags. This call will never fail, unless the resource table is full.
        //! In such a case, the application will terminate.
        [[nodiscard]] auto CreateNodeImpl(const std::string& name) noexcept
            -> NodeHandle;

        //! Creates a new node implementation with the given name and flags.
        //! This call will never fail, unless the resource table is full. In
        //! such a case, the application will terminate.
        [[nodiscard]] auto CreateNodeImpl(const std::string& name, SceneNode::Flags flags) noexcept
            -> NodeHandle;

        //! Links a child_handle node to a parent_handle in the hierarchy.
        void LinkChild(const NodeHandle& parent_handle, const NodeHandle& child_handle);

        //! Un-links a node_handle from its parent and siblings, preparing it for
        //! destruction.
        /*!
         This method does not destroy the node_handle, it only removes it from the
         hierarchy. If the node_handle must be destroyed, DestroyNode() or
         DestroyNodeHierarchy() should be used after un-linking. If it is simply
         being detached, it needs to be added to the roots set using
         AddRootNode().
        */
        void UnlinkNode(const NodeHandle& node_handle) noexcept;

        void AddRootNode(const NodeHandle& node);

        void RemoveRootNode(const NodeHandle& node);

        std::shared_ptr<NodeTable> nodes_;
        std::unordered_set<NodeHandle> root_nodes_; //!< Set of root nodes for robust, duplicate-free management
    };

} // namespace scene
} // namespace oxygen
