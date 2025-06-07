//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string>

#include <Oxygen/Base/ResourceHandle.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/Types/Flags.h>
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
 cache-friendly memory access patterns.
 This class is not intended for direct public use - access is provided through
 the SceneNode handle/view pattern which ensures resource safety and provides a
 stable API surface.
 */
class SceneNodeImpl : public Composition, public CloneableMixin<SceneNodeImpl> {
    OXYGEN_TYPED(SceneNodeImpl)
public:
    using Flags = SceneFlags<SceneNodeFlags>;

    static constexpr auto kDefaultFlags
        = Flags {}
              .SetFlag(SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(true))
              .SetFlag(SceneNodeFlags::kCastsShadows, SceneFlag {}.SetInheritedBit(true))
              .SetFlag(SceneNodeFlags::kReceivesShadows, SceneFlag {}.SetInheritedBit(true))
              .SetFlag(SceneNodeFlags::kRayCastingSelectable, SceneFlag {}.SetInheritedBit(true));

    //! Efficient graph node view over a SceneNodeImpl, for hierarchy traversal
    //! and manipulation.
    /*!
     GraphNode provides a cached, high-performance interface for accessing and
     modifying the hierarchical structure of scene nodes. This nested class acts
     as a view into the graph data component, caching pointers to avoid repeated
     component lookups during tree traversal operations.

     The design employs pointer caching to eliminate the expensive component lookup
     costs that would otherwise occur on every hierarchy operation. Move semantics
     ensure proper invalidation during SceneNodeImpl lifecycle events, preventing
     dangling pointer access while maintaining optimal performance for valid operations.

     All hierarchy queries and modifications are validated through the IsValid()
     check, providing graceful degradation when accessing invalidated nodes.
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

        [[nodiscard]] OXYGEN_SCENE_API auto GetParent() const noexcept -> const ResourceHandle&;
        [[nodiscard]] OXYGEN_SCENE_API auto GetFirstChild() const noexcept -> const ResourceHandle&;
        [[nodiscard]] OXYGEN_SCENE_API auto GetNextSibling() const noexcept -> const ResourceHandle&;
        [[nodiscard]] OXYGEN_SCENE_API auto GetPrevSibling() const noexcept -> const ResourceHandle&;

        OXYGEN_SCENE_API void SetParent(const ResourceHandle& parent) noexcept;
        OXYGEN_SCENE_API void SetFirstChild(const ResourceHandle& child) noexcept;
        OXYGEN_SCENE_API void SetNextSibling(const ResourceHandle& sibling) noexcept;
        OXYGEN_SCENE_API void SetPrevSibling(const ResourceHandle& sibling) noexcept;

        [[nodiscard]] OXYGEN_SCENE_API auto IsRoot() const noexcept -> bool;
        [[nodiscard]] OXYGEN_SCENE_API auto IsValid() const noexcept -> bool
        {
            return impl_ != nullptr && graph_data_ != nullptr;
        }

    private:
        GraphNode(SceneNodeImpl* impl, detail::GraphData* graph_data)
            : impl_(impl)
            , graph_data_(graph_data)
        {
        }

        void Invalidate() noexcept
        {
            impl_ = nullptr;
            graph_data_ = nullptr;
        }

        SceneNodeImpl* impl_; //!< Back pointer to the SceneNodeImpl instance
        detail::GraphData* graph_data_; //!< Cached pointer to the GraphData component
    };

    OXYGEN_SCENE_API explicit SceneNodeImpl(const std::string& name, Flags flags = kDefaultFlags);
    OXYGEN_SCENE_API ~SceneNodeImpl() override;

    SceneNodeImpl(const SceneNodeImpl& other);
    auto operator=(const SceneNodeImpl& other) -> SceneNodeImpl&;
    SceneNodeImpl(SceneNodeImpl&& other) noexcept;
    auto operator=(SceneNodeImpl&& other) noexcept -> SceneNodeImpl&;

    [[nodiscard]] OXYGEN_SCENE_API auto GetName() const noexcept -> std::string_view;
    OXYGEN_SCENE_API void SetName(std::string_view name) noexcept;

    [[nodiscard]] OXYGEN_SCENE_API auto GetFlags() const noexcept -> const Flags&;
    [[nodiscard]] OXYGEN_SCENE_API auto GetFlags() noexcept -> Flags&;

    OXYGEN_SCENE_API auto AsGraphNode() noexcept -> GraphNode&;
    OXYGEN_SCENE_API auto AsGraphNode() const noexcept -> const GraphNode&;

    OXYGEN_SCENE_API void MarkTransformDirty() noexcept;
    [[nodiscard]] OXYGEN_SCENE_API auto IsTransformDirty() const noexcept -> bool;
    OXYGEN_SCENE_API void UpdateTransforms(const Scene& scene); // Cloning support
    [[nodiscard]] static auto IsCloneable() noexcept -> bool { return true; }
    [[nodiscard]] OXYGEN_SCENE_API auto Clone() const -> std::unique_ptr<SceneNodeImpl> override;

protected:
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
    OXYGEN_SCENE_API void ClearTransformDirty() noexcept;

private:
    [[nodiscard]] constexpr auto ShouldIgnoreParentTransform() const
    {
        return GetFlags().GetEffectiveValue(SceneNodeFlags::kIgnoreParentTransform);
    }

    // Cached GraphNode for efficient access - always initialized, using std::optional for efficiency
    mutable std::optional<GraphNode> cached_graph_node_;
};

} // namespace oxygen::scene
