//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

/*!
 \file SceneNode.h
 \brief Defines SceneNode, SceneNodeImpl, and related types for the Oxygen scene graph.

 SceneNode is a lightweight, non-owning handle/view to a node in a Scene.
 All node data and hierarchy are managed by Scene via a resource table of SceneNodeImpl.

 - SceneNode provides navigation and logical queries, using Scene as context.
 - All mutations (creation, destruction, reparenting, etc.) are routed through Scene.
 - SceneNodeImpl is an internal, implementation-only class; users interact with SceneNode.
 - Node handles are lazily invalidated; IsValid() and IsSceneAlive() are provided for safety.
 - API is const-correct and uses std::optional for operations that may fail.
 - See README.md for migration strategy and best practices.

 Usage:
   SceneNode node = scene->CreateNode("NodeA");
   if (node.IsValid()) { ... }

 \see Scene, SceneNodeImpl
*/

#include <optional>
#include <string_view>

#include <Oxygen/Base/Resource.h>
#include <Oxygen/Base/ResourceHandle.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Core/Resources.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene {

class Scene; // Forward declaration

/*!
 SceneNodeFlags are symbolic flags for node state and optimization hints.
*/
enum class SceneNodeFlags : uint8_t {
    kVisible, //!< Node is visible for rendering
    kStatic, //!< Node transform won't change (optimization hint)
    kCastsShadows, //!< Node casts shadows
    kReceivesShadows, //!< Node receives shadows
    kRayCastingSelectable, //!< Node can be selected via ray casting
    kIgnoreParentTransform, //!< Ignore parent transform (use only local transform)

    kCount, //!< Sentinel value required for TernaryFlagEnum concept
};
OXYGEN_SCENE_API auto constexpr to_string(const SceneNodeFlags& value) noexcept -> const char*;

static_assert(SceneFlagEnum<SceneNodeFlags>,
    "SceneNodeFlags must satisfy TernaryFlagEnum concept requirements");

/*!
 Data-oriented component to store scene node flags and state.
*/
class SceneNodeData : public Component {
    OXYGEN_COMPONENT(SceneNodeData)
public:
    using Flags = SceneFlags<SceneNodeFlags>;

    OXYGEN_SCENE_API explicit SceneNodeData(Flags flags);

    OXYGEN_SCENE_API ~SceneNodeData() override = default;

    OXYGEN_DEFAULT_COPYABLE(SceneNodeData)
    OXYGEN_DEFAULT_MOVABLE(SceneNodeData)

    [[nodiscard]] auto GetFlags() const noexcept -> const Flags& { return flags_; }
    [[nodiscard]] auto GetFlags() noexcept -> Flags& { return flags_; }

private:
    Flags flags_; //!< Node state and optimization flags.
};

/*!
 Concrete implementation of a scene node, stored in a resource table and accessible via a handle.

 SceneNodeImpl decouples the data of a scene node from its handle, allowing for efficient resource management and retrieval.
 It extends oxygen::Composition, giving it the flexibility to compose itself with various components and functionality.
 At a minimum, it has an ObjectMetaData component to store the node name and any other custom properties, a SceneNodeData component to store the actual scene data and its state, and a TransformComponent to manage the node's position, rotation, and scale in the scene.

 Note: SceneNodeImpl is internal to the Scene module. Users should interact with SceneNode instead.
*/
class SceneNodeImpl : public Composition {
    OXYGEN_TYPED(SceneNodeImpl)

public:
    using Flags = SceneNodeData::Flags;

    using optional_ref = std::optional<std::reference_wrapper<SceneNodeImpl>>;
    using optional_cref = std::optional<std::reference_wrapper<const SceneNodeImpl>>;

private:
    static constexpr auto kDefaultFlags
        = Flags {}
              .SetFlag(SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(true))
              .SetFlag(SceneNodeFlags::kCastsShadows, SceneFlag {}.SetInheritedBit(true))
              .SetFlag(SceneNodeFlags::kReceivesShadows, SceneFlag {}.SetInheritedBit(true))
              .SetFlag(SceneNodeFlags::kRayCastingSelectable, SceneFlag {}.SetInheritedBit(true));

public:
    /*!
     Constructs a SceneNodeImpl with the given name and flags.
     Only Scene should create SceneNodeImpl instances.
    */
    OXYGEN_SCENE_API explicit SceneNodeImpl(const std::string& name, Flags flags = kDefaultFlags);

    [[nodiscard]] OXYGEN_SCENE_API auto GetName() const noexcept -> std::string_view;
    OXYGEN_SCENE_API void SetName(std::string_view name) noexcept;

    [[nodiscard]] OXYGEN_SCENE_API auto GetFlags() const noexcept -> const Flags&;
    [[nodiscard]] OXYGEN_SCENE_API auto GetFlags() noexcept -> Flags&;

    // Hierarchy accessors (used by Scene for graph management)
    [[nodiscard]] OXYGEN_SCENE_API auto GetParent() const noexcept -> ResourceHandle;
    [[nodiscard]] OXYGEN_SCENE_API auto GetFirstChild() const noexcept -> ResourceHandle;
    [[nodiscard]] OXYGEN_SCENE_API auto GetNextSibling() const noexcept -> ResourceHandle;
    [[nodiscard]] OXYGEN_SCENE_API auto GetPrevSibling() const noexcept -> ResourceHandle;

    // Internal hierarchy management (Scene class only)
    OXYGEN_SCENE_API void SetParent(ResourceHandle parent) noexcept;
    OXYGEN_SCENE_API void SetFirstChild(ResourceHandle child) noexcept;
    OXYGEN_SCENE_API void SetNextSibling(ResourceHandle sibling) noexcept;
    OXYGEN_SCENE_API void SetPrevSibling(ResourceHandle sibling) noexcept;

    /*!
     Marks the node's transform as dirty, requiring an update.
    */
    OXYGEN_SCENE_API void MarkTransformDirty() noexcept;
    [[nodiscard]] OXYGEN_SCENE_API auto IsTransformDirty() const noexcept -> bool;
    OXYGEN_SCENE_API void ClearTransformDirty() noexcept;

    /*!
     Updates world transforms for this node and its children.
    */
    OXYGEN_SCENE_API void UpdateTransforms(const Scene& scene);

private:
    [[nodiscard]] constexpr auto ShouldIgnoreParentTransform() const
    {
        return GetFlags().GetEffectiveValue(SceneNodeFlags::kIgnoreParentTransform);
    }

    // Hierarchy data - intrusive sibling linked list
    ResourceHandle parent_;
    ResourceHandle first_child_;
    ResourceHandle next_sibling_;
    ResourceHandle prev_sibling_;
    bool transform_dirty_ = true;
};

/*!
 A Resource wrapper for a scene node, the primary object in the scene hierarchy.

 SceneNode is a high-level, lightweight, non-owning handle/view to a node in a Scene. All node data and hierarchy are managed by Scene via a resource table of SceneNodeImpl. SceneNode provides navigation and logical queries, using Scene as context. All mutations (creation, destruction, reparenting, etc.) are routed through Scene. Node handles are lazily invalidated; IsValid() and IsSceneAlive() are provided for safety. API is const-correct and uses std::optional for operations that may fail.

 Note: SceneNode is the main user-facing API for scene graph navigation and queries. Mutations must be performed via Scene.
*/
class SceneNode : public Object, public Resource<resources::kSceneNode> {
    OXYGEN_TYPED(SceneNode)
public:
    using Flags = SceneNodeData::Flags;
    using NodeHandle = ResourceHandle;

    // We make the Scene a friend, so it can invalidate a SceneNode when its
    // data is erased.
    friend class Scene;

    /*!
     Constructs a SceneNode handle for the given resource handle and scene.
     Only Scene should create SceneNode instances.
    */
    OXYGEN_SCENE_API explicit SceneNode(const ResourceHandle& handle, std::weak_ptr<Scene> scene_weak);
    OXYGEN_SCENE_API ~SceneNode() override = default;
    OXYGEN_DEFAULT_COPYABLE(SceneNode)
    OXYGEN_DEFAULT_MOVABLE(SceneNode)

    [[nodiscard]] OXYGEN_SCENE_API auto GetObject() const noexcept -> SceneNodeImpl::optional_cref;
    [[nodiscard]] OXYGEN_SCENE_API auto GetObject() noexcept -> SceneNodeImpl::optional_ref;

    [[nodiscard]] OXYGEN_SCENE_API auto GetParent() const noexcept -> std::optional<SceneNode>;
    [[nodiscard]] OXYGEN_SCENE_API auto GetFirstChild() const noexcept -> std::optional<SceneNode>;
    [[nodiscard]] OXYGEN_SCENE_API auto GetNextSibling() const noexcept -> std::optional<SceneNode>;
    [[nodiscard]] OXYGEN_SCENE_API auto GetPrevSibling() const noexcept -> std::optional<SceneNode>;
    [[nodiscard]] OXYGEN_SCENE_API auto HasParent() const noexcept -> bool;
    [[nodiscard]] OXYGEN_SCENE_API auto HasChildren() const noexcept -> bool;
    [[nodiscard]] OXYGEN_SCENE_API auto IsRoot() const noexcept -> bool;

private:
    // We need this to allow Scene to lazily invalidate the node even when it is const.
    void Invalidate() const
    {
        const_cast<SceneNode*>(this)->Resource::Invalidate();
    }

    std::weak_ptr<Scene> scene_weak_;
};

} // namespace oxygen::scene
