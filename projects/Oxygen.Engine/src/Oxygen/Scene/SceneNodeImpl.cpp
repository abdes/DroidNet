//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Composition/ObjectMetadata.h>
#include <Oxygen/Scene/Detail/GraphData.h>
#include <Oxygen/Scene/Detail/NodeData.h>
#include <Oxygen/Scene/Detail/RenderableComponent.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNodeImpl.h>

using oxygen::scene::SceneNodeImpl;
using oxygen::scene::detail::GraphData;
using oxygen::scene::detail::NodeData;
using oxygen::scene::detail::RenderableComponent;
using oxygen::scene::detail::TransformComponent;
using GraphNode = SceneNodeImpl::GraphNode;

SceneNodeImpl::SceneNodeImpl(const std::string& name, Flags flags)
{
  LOG_SCOPE_F(2, "SceneNodeImpl creation");
  DLOG_F(2, "name: '{}'", name);
  AddComponent<ObjectMetadata>(name);
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
  return GetComponent<ObjectMetadata>().GetName();
}

// ReSharper disable once CppMemberFunctionMayBeConst
void SceneNodeImpl::SetName(const std::string_view name) noexcept
{
  GetComponent<ObjectMetadata>().SetName(name);
}

auto SceneNodeImpl::GetFlags() const noexcept -> const Flags&
{
  return GetComponent<NodeData>().flags_;
}

auto SceneNodeImpl::GetFlags() noexcept -> Flags&
{
  return GetComponent<NodeData>().flags_;
}

/*!
 This method flags the node's TransformComponent as dirty, indicating that its
 cached world transformation matrix needs to be recomputed during the next
 transform update pass. The dirty flag is used by the scene's update system to
 efficiently batch transform calculations and maintain proper hierarchy
 dependencies.

 The transform becomes dirty when:
 - Local position, rotation, or scale is modified
 - The node is moved within the scene hierarchy
 - Parent transforms change (propagated automatically)
 - Manual marking is required for custom transform modifications

 @note This method only sets the dirty flag - it does not immediately
 recalculate the transform matrices. Call UpdateTransforms() or wait for the
 next Scene::Update() to perform the actual computation.

 @note The method is thread-safe as it only modifies a simple boolean flag, but
 transform updates should still be performed on the main thread.

 @see UpdateTransforms(), ClearTransformDirty(), IsTransformDirty()
 @see Scene::Update() for automatic transform updates
*/
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

/*!
 This method queries the current dirty state of the node's TransformComponent,
 indicating whether the cached world transformation matrix is valid or needs to
 be recomputed. The dirty state is managed automatically by the scene system to
 optimize transform updates and maintain hierarchy consistency.

 The dirty state is cleared when:
 - UpdateTransforms() successfully recalculates the world matrix
 - ClearTransformDirty() is called explicitly (primarily for testing)
 - Scene::Update() processes the node during the transform update pass

 @return `true` if the transform needs recalculation, `false` if the cached
 world transformation matrix is up-to-date.

 @note This is a lightweight query operation that simply checks a boolean flag
 in the TransformComponent - no expensive calculations are performed.

 @see MarkTransformDirty(), UpdateTransforms(), ClearTransformDirty()
 @see Scene::Update() for automatic dirty state processing
 @see DirtyTransformFilter for efficient traversal of dirty nodes
*/
auto SceneNodeImpl::IsTransformDirty() const noexcept -> bool
{
  return GetComponent<TransformComponent>().IsDirty();
}

/*!
 Recalculates the cached world transformation matrix for this node by composing
 its local transform with its parent's world transform (if any). This method is
 the core of the scene's hierarchical transform system, responsible for
 propagating transformation changes down the scene graph.

 __Transform Computation Logic__

 - **Root nodes**: World matrix = Local matrix (no parent composition)
 - **Child nodes**: World matrix = Parent's world matrix x Local matrix
 - **Ignore parent flag**: Treated as root regardless of parent relationship

 __Performance Optimization__

 The method performs an early exit if the transform is not dirty, making it safe
 to call repeatedly without performance penalty. Only dirty nodes perform the
 actual matrix multiplication and composition.

 __Parent Dependency__

 For child nodes, the parent's world transform must be up to date before calling
 this method. The Scene's update system ensures proper traversal order
 (parent-first, depth-first) to maintain hierarchy consistency.

 __State Changes__

 - Clears the dirty flag after successful computation
 - Updates the cached world matrix in the TransformComponent
 - Enables access to world-space transformation data

 @param scene Reference to the Scene containing this node, used to resolve
 parent relationships and access parent transforms.

 @note This method should typically be called by the Scene's update system
 rather than directly. Manual calls require ensuring parent nodes are updated
 first to maintain correct hierarchical relationships.

 @see MarkTransformDirty() to flag transforms for recalculation
 @see IsTransformDirty() to check if recalculation is needed
 @see Scene::Update() for automatic hierarchical transform updates
 @see TransformComponent::UpdateWorldTransform() for low-level matrix
 computation
 @see SceneTraversal::UpdateTransforms() for batch transform processing
*/
void SceneNodeImpl::UpdateTransforms(const Scene& scene)
{
  // Update the transform component. Even if this node's transform wasn't
  // explicitly marked dirty, its parent's transform may have changed. The
  // traversal system visits children when a parent is accepted, so we must
  // compute the child's world matrix here rather than early-returning.
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

  // Update the Renderable component
  if (HasComponent<RenderableComponent>()) {
    auto& renderable = GetComponent<RenderableComponent>();
    renderable.OnWorldTransformUpdated(transform.GetWorldMatrix());
  }

  ClearTransformDirty();
}

auto GraphNode::GetParent() const noexcept -> const NodeHandle&
{
  DCHECK_F(IsValid(), "GraphNode is invalidated - programming error");
  return graph_data_->GetParent();
}

auto GraphNode::GetFirstChild() const noexcept -> const NodeHandle&
{
  DCHECK_F(IsValid(), "GraphNode is invalidated - programming error");
  return graph_data_->GetFirstChild();
}

auto GraphNode::GetNextSibling() const noexcept -> const NodeHandle&
{
  DCHECK_F(IsValid(), "GraphNode is invalidated - programming error");
  return graph_data_->GetNextSibling();
}

auto GraphNode::GetPrevSibling() const noexcept -> const NodeHandle&
{
  DCHECK_F(IsValid(), "GraphNode is invalidated - programming error");
  return graph_data_->GetPrevSibling();
}

// ReSharper disable once CppMemberFunctionMayBeConst
void GraphNode::SetParent(const NodeHandle& parent) noexcept
{
  DCHECK_F(IsValid(), "GraphNode is invalidated - programming error");
  graph_data_->SetParent(parent);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void GraphNode::SetFirstChild(const NodeHandle& child) noexcept
{
  DCHECK_F(IsValid(), "GraphNode is invalidated - programming error");
  graph_data_->SetFirstChild(child);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void GraphNode::SetNextSibling(const NodeHandle& sibling) noexcept
{
  DCHECK_F(IsValid(), "GraphNode is invalidated - programming error");
  graph_data_->SetNextSibling(sibling);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void GraphNode::SetPrevSibling(const NodeHandle& sibling) noexcept
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
