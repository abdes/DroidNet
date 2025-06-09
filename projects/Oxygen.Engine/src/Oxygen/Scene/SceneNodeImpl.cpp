//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Scene/Detail/GraphData.h>
#include <Oxygen/Scene/Detail/NodeData.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNodeImpl.h>

using oxygen::scene::SceneNodeImpl;
using oxygen::scene::detail::GraphData;
using oxygen::scene::detail::NodeData;
using oxygen::scene::detail::TransformComponent;
using GraphNode = SceneNodeImpl::GraphNode;

SceneNodeImpl::SceneNodeImpl(const std::string& name, Flags flags)
{
    LOG_SCOPE_F(2, "SceneNodeImpl creation");
    DLOG_F(2, "name: '{}'", name);
    AddComponent<ObjectMetaData>(name);
    AddComponent<NodeData>(flags);
    AddComponent<GraphData>();
    AddComponent<TransformComponent>();

    // Initialize the cached GraphNode efficiently, but after components are
    // added.
    cached_graph_node_ = GraphNode { this, &GetComponent<GraphData>() };

    // Iterate over the flags and log each one
    for (const auto [flag, flag_values] : flags) {
        DLOG_F(2, "flag `{}`: {}", nostd::to_string(flag),
            nostd::to_string(flag_values));
    }
}

SceneNodeImpl::~SceneNodeImpl() { LOG_SCOPE_F(3, "SceneNodeImpl destruction"); }

auto SceneNodeImpl::AsGraphNode() noexcept -> GraphNode&
{
    // GraphNode is always initialized, so just return it
    return *cached_graph_node_;
}

auto SceneNodeImpl::AsGraphNode() const noexcept -> const GraphNode&
{
    // GraphNode is always initialized, so just return it
    return *cached_graph_node_;
}

auto SceneNodeImpl::GetName() const noexcept -> std::string_view
{
    return GetComponent<ObjectMetaData>().GetName();
}

// ReSharper disable once CppMemberFunctionMayBeConst
void SceneNodeImpl::SetName(const std::string_view name) noexcept
{
    GetComponent<ObjectMetaData>().SetName(name);
}

auto SceneNodeImpl::GetFlags() const noexcept -> const Flags&
{
    return GetComponent<NodeData>().flags_;
}

auto SceneNodeImpl::GetFlags() noexcept -> Flags&
{
    return GetComponent<NodeData>().flags_;
}

// ReSharper disable once CppMemberFunctionMayBeConst
void SceneNodeImpl::MarkTransformDirty() noexcept
{
    GetComponent<TransformComponent>().MarkDirty();
}

// ReSharper disable once CppMemberFunctionMayBeConst
void SceneNodeImpl::ClearTransformDirty() noexcept
{
    GetComponent<TransformComponent>().ForceClearDirty();
}

auto SceneNodeImpl::IsTransformDirty() const noexcept -> bool
{
    return GetComponent<TransformComponent>().IsDirty();
}

void SceneNodeImpl::UpdateTransforms(const Scene& scene)
{
    if (!GetComponent<TransformComponent>().IsDirty()) {
        return;
    }

    auto& transform = GetComponent<TransformComponent>();
    if (const auto& parent = GetComponent<GraphData>().GetParent();
        parent.IsValid() && !ShouldIgnoreParentTransform()) {
        const auto& parent_impl = scene.GetNodeImplRef(parent);
        const auto& parent_transform
            = parent_impl.GetComponent<TransformComponent>();
        transform.UpdateWorldTransform(parent_transform.GetWorldMatrix());
    } else {
        transform.UpdateWorldTransformAsRoot();
    }
    ClearTransformDirty();
}

auto GraphNode::GetParent() const noexcept -> const ResourceHandle&
{
    DCHECK_F(IsValid(), "GraphNode is invalidated - programming error");
    return graph_data_->GetParent();
}

auto GraphNode::GetFirstChild() const noexcept -> const ResourceHandle&
{
    DCHECK_F(IsValid(), "GraphNode is invalidated - programming error");
    return graph_data_->GetFirstChild();
}

auto GraphNode::GetNextSibling() const noexcept -> const ResourceHandle&
{
    DCHECK_F(IsValid(), "GraphNode is invalidated - programming error");
    return graph_data_->GetNextSibling();
}

auto GraphNode::GetPrevSibling() const noexcept -> const ResourceHandle&
{
    DCHECK_F(IsValid(), "GraphNode is invalidated - programming error");
    return graph_data_->GetPrevSibling();
}

// ReSharper disable once CppMemberFunctionMayBeConst
void GraphNode::SetParent(const ResourceHandle& parent) noexcept
{
    DCHECK_F(IsValid(), "GraphNode is invalidated - programming error");
    graph_data_->SetParent(parent);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void GraphNode::SetFirstChild(const ResourceHandle& child) noexcept
{
    DCHECK_F(IsValid(), "GraphNode is invalidated - programming error");
    graph_data_->SetFirstChild(child);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void GraphNode::SetNextSibling(const ResourceHandle& sibling) noexcept
{
    DCHECK_F(IsValid(), "GraphNode is invalidated - programming error");
    graph_data_->SetNextSibling(sibling);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void GraphNode::SetPrevSibling(const ResourceHandle& sibling) noexcept
{
    DCHECK_F(IsValid(), "GraphNode is invalidated - programming error");
    graph_data_->SetPrevSibling(sibling);
}

auto GraphNode::IsRoot() const noexcept -> bool
{
    DCHECK_F(IsValid(), "GraphNode is invalidated - programming error");
    return !GetParent().IsValid();
}

// Copy constructor
SceneNodeImpl::SceneNodeImpl(const SceneNodeImpl& other)
    : Composition(other)
{
    // Initialize our own GraphNode after components are copied
    cached_graph_node_ = GraphNode { this, &GetComponent<GraphData>() };
}

// Copy assignment
auto SceneNodeImpl::operator=(const SceneNodeImpl& other) -> SceneNodeImpl&
{
    if (this != &other) {
        Composition::operator=(other);
        // Re-initialize cached GraphNode with new components
        cached_graph_node_ = GraphNode { this, &GetComponent<GraphData>() };
    }
    return *this;
}

// Move constructor
SceneNodeImpl::SceneNodeImpl(SceneNodeImpl&& other) noexcept
    // Use lambda to invalidate `other`'s graph node BEFORE it is moved.
    : Composition([&]() -> Composition&& {
        if (other.cached_graph_node_.has_value()) {
            other.cached_graph_node_->Invalidate();
        }
        return std::move(other);
    }())
{
    // Initialize our own GraphNode after components are moved
    cached_graph_node_ = GraphNode { this, &GetComponent<GraphData>() };
}

// Move assignment
auto SceneNodeImpl::operator=(SceneNodeImpl&& other) noexcept -> SceneNodeImpl&
{
    if (this != &other) {
        // Invalidate `other` cached GraphNode before the move
        if (other.cached_graph_node_.has_value()) {
            other.cached_graph_node_->Invalidate();
        }

        // Perform the composition move
        Composition::operator=(std::move(
            other)); // Re-initialize our cached GraphNode with moved components
        cached_graph_node_ = GraphNode { this, &GetComponent<GraphData>() };
    }
    return *this;
}

auto SceneNodeImpl::Clone() const -> std::unique_ptr<SceneNodeImpl>
{
    LOG_SCOPE_F(2, "SceneNodeImpl cloning");
    DLOG_F(2, "original node name: {}", GetName());

    // Use CloneableMixin to get a properly cloned composition
    auto clone = CloneableMixin::Clone();

    // Re-initialize the cached GraphNode with the cloned components
    clone->cached_graph_node_
        = GraphNode { clone.get(), &clone->GetComponent<GraphData>() };

    DLOG_F(2, "successful");
    return clone;
}
