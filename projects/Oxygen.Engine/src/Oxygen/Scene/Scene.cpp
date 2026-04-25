//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bitset>
#include <ranges>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ResourceTable.h>
#include <Oxygen/Composition/ObjectMetadata.h>
#include <Oxygen/Scene/Detail/Scene_safecall_impl.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Internal/IMutationCollector.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Internal/MutationCollector.h>
#include <Oxygen/Scene/Internal/MutationDispatcher.h>
#include <Oxygen/Scene/Internal/ScriptSlotMutationProcessor.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneQuery.h>
#include <Oxygen/Scene/SceneTraversal.h>

using oxygen::scene::NodeHandle;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeImpl;

// =============================================================================
// SceneIdManager Implementations
// =============================================================================

namespace {

//! Thread-safe scene ID management
struct SceneIdManager {
  static constexpr Scene::SceneId kMaxScenes = NodeHandle::kMaxSceneId;

  std::mutex mutex;
  std::bitset<kMaxScenes> used_ids {};

  //! Allocates the next available scene ID
  auto AllocateId() -> std::optional<Scene::SceneId>;

  //! Releases a scene ID for reuse
  void ReleaseId(Scene::SceneId id) noexcept;

  //! Gets the singleton instance
  static auto Instance() -> SceneIdManager&;
};

auto SceneIdManager::AllocateId() -> std::optional<Scene::SceneId>
{
  std::lock_guard<std::mutex> lock(mutex);

  // Find first available ID, that is not the invalid ID
  for (size_t i = 0; i < kMaxScenes; ++i) {
    if (!used_ids[i] && i != NodeHandle::kInvalidSceneId) {
      used_ids[i] = true;
      return static_cast<Scene::SceneId>(i);
    }
  }

  // All IDs are in use
  return std::nullopt;
}

void SceneIdManager::ReleaseId(Scene::SceneId id) noexcept
{
  std::lock_guard<std::mutex> lock(mutex);

  if (id < kMaxScenes) {
    used_ids[id] = false;
  }
}

auto SceneIdManager::Instance() -> SceneIdManager&
{
  static SceneIdManager instance;
  return instance;
}

} // namespace

//------------------------------------------------------------------------------
// Basic Scene Operations
//------------------------------------------------------------------------------

Scene::Scene(const std::string& name, size_t initial_capacity)
  : nodes_(std::make_shared<NodeTable>(
      SceneNode::GetResourceType(), initial_capacity))
  , mutation_collector_(internal::CreateMutationCollector())
  , script_slot_processor_(internal::CreateScriptSlotMutationProcessor())
  , mutation_dispatcher_(internal::CreateMutationDispatcher(
      observer_ptr<internal::IScriptSlotMutationProcessor>(
        script_slot_processor_.get())))
  , directional_light_resolver_(std::make_unique<DirectionalLightResolver>())
{
  LOG_SCOPE_F(INFO, "Scene creation");
  LOG_F(2, "name: '{}'", name);
  LOG_F(2, "initial capacity: '{}'", initial_capacity);

  AddComponent<ObjectMetadata>(name);

  // Allocate unique scene ID
  auto id = SceneIdManager::Instance().AllocateId();
  if (!id.has_value()) {
    // Handle the case where all 256 IDs are exhausted
    // This could throw an exception or use a fallback strategy
    throw std::runtime_error(
      "Cannot create Scene: All 256 scene IDs are in use");
  }
  scene_id_ = *id;

  SetName(name);
  directional_light_resolver_->Bind(observer_ptr<Scene> { this });
  UpdateMutationCollectionState(false);
  LOG_F(INFO, "mutation collection default state: enabled={}",
    mutation_collector_ != nullptr && mutation_collector_->IsEnabled());
}

Scene::~Scene()
{
  LOG_SCOPE_F(INFO, "Scene destruction");
  if (directional_light_resolver_ != nullptr) {
    directional_light_resolver_->Unbind();
  }
  nodes_->Clear();
  // Release the scene ID for reuse
  SceneIdManager::Instance().ReleaseId(scene_id_);
}

auto Scene::GetName() const noexcept -> std::string_view
{
  return GetComponent<ObjectMetadata>().GetName();
}

auto Scene::GetDirectionalLightResolver() noexcept
  -> DirectionalLightResolver&
{
  DCHECK_NOTNULL_F(directional_light_resolver_.get());
  return *directional_light_resolver_;
}

auto Scene::GetDirectionalLightResolver() const noexcept
  -> const DirectionalLightResolver&
{
  DCHECK_NOTNULL_F(directional_light_resolver_.get());
  return *directional_light_resolver_;
}

// ReSharper disable once CppMemberFunctionMayBeConst
void Scene::SetName(const std::string_view name) noexcept
{
  GetComponent<ObjectMetadata>().SetName(name);
}

void Scene::LogPartialFailure(
  [[maybe_unused]] const std::vector<uint8_t>& results,
  [[maybe_unused]] const std::string& operation_name) const
{
#if defined(NDEBUG)
  return;
#else // NDEBUG
  const auto successful_count = std::ranges::count(results, 1);
  LOG_F(3, "{} / {} nodes processed", successful_count, results.size());
  if (static_cast<size_t>(successful_count) != results.size()) {
    LOG_F(WARNING, "{} partially failed: {} nodes out of {} were not processed",
      operation_name, results.size() - successful_count, results.size());
  }
#endif // NDEBUG
}

//------------------------------------------------------------------------------
// Scene graph operations
//------------------------------------------------------------------------------

auto Scene::RelativeIsAlive(const NodeHandle& relative,
  [[maybe_unused]] const SceneNode& target) const -> std::optional<SceneNode>
{
  if (!relative.IsValid()) {
    return std::nullopt;
  }
  if (!nodes_->Contains(relative)) {
    DLOG_F(4, "Relative node {} of target node {} is not alive",
      to_string_compact(relative), nostd::to_string(target));
    return std::nullopt;
  }
  return SceneNode(
    std::const_pointer_cast<Scene>(this->shared_from_this()), relative);
}

auto Scene::RelativeIsAliveOrInvalidateTarget(const NodeHandle& relative,
  SceneNode& target) const -> std::optional<SceneNode>
{
  if (!relative.IsValid()) {
    return std::nullopt;
  }
  // If the relative node is not alive, invalidate the target and return nullopt
  if (!nodes_->Contains(relative)) {
    DLOG_F(4, "Relative node is not alive: {} -> target node {} invalidated",
      to_string_compact(relative), nostd::to_string(target));
    target.Invalidate();
    return std::nullopt;
  }
  return SceneNode(
    std::const_pointer_cast<Scene>(this->shared_from_this()), relative);
}

auto Scene::IsOwnerOf(const SceneNode& node) const -> bool
{
  return !node.scene_weak_.expired() && node.scene_weak_.lock().get() == this;
}

// ReSharper disable once CppMemberFunctionMayBeConst
void Scene::DefragmentStorage()
{
  // Defragment the underlying storage using a simple comparator
  // This will reorganize nodes for better cache locality
  nodes_->Defragment([](const SceneNodeImpl& a, const SceneNodeImpl& b) {
    // Sort by name for predictable ordering
    return a.GetName() < b.GetName();
  });
}

void Scene::Clear() noexcept
{
  if (mutation_collector_ && script_slot_processor_) {
    script_slot_processor_->QueueTrackedSlotDeactivations(*mutation_collector_);
  }
  nodes_->Clear();
  root_nodes_.clear();
  environment_.reset();
}

auto Scene::SetEnvironment(
  std::unique_ptr<SceneEnvironment> environment) noexcept -> void
{
  environment_ = std::move(environment);
}

auto Scene::ClearEnvironment() noexcept -> void { environment_.reset(); }

auto Scene::GetParent(SceneNode& node) const noexcept
  -> std::optional<SceneNode>
{
  return SafeCall(NodeIsValidAndMine(node),
    [&](const SafeCallState& state) -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, &node);
      DCHECK_NOTNULL_F(state.node_impl);

      return GetParentUnsafe(node, state.node_impl);
    });
}

auto Scene::GetParentUnsafe(SceneNode& node,
  const SceneNodeImpl* node_impl) const -> std::optional<SceneNode>
{
  DCHECK_NOTNULL_F(node_impl);

  return RelativeIsAliveOrInvalidateTarget(
    node_impl->AsGraphNode().GetParent(), node);
}

auto Scene::HasParent(SceneNode& node) const noexcept -> bool
{
  return SafeCall(
    NodeIsValidAndMine(node), [&](const SafeCallState& state) -> bool {
      DCHECK_EQ_F(state.node, &node);
      DCHECK_NOTNULL_F(state.node_impl);

      return HasParentUnsafe(node, state.node_impl);
    });
}

auto Scene::HasParentUnsafe(
  SceneNode& node, const SceneNodeImpl* node_impl) const -> bool
{
  DCHECK_NOTNULL_F(node_impl);

  return RelativeIsAlive(node_impl->AsGraphNode().GetParent(), node)
    .has_value();
}

auto Scene::HasChildren(SceneNode& node) const noexcept -> bool
{
  return SafeCall(
    NodeIsValidAndMine(node), [&](const SafeCallState& state) -> bool {
      DCHECK_EQ_F(state.node, &node);
      DCHECK_NOTNULL_F(state.node_impl);

      return HasChildrenUnsafe(node, state.node_impl);
    });
}

auto Scene::HasChildrenUnsafe(
  SceneNode& node, const SceneNodeImpl* node_impl) const -> bool
{
  DCHECK_NOTNULL_F(node_impl);

  return RelativeIsAlive(node_impl->AsGraphNode().GetFirstChild(), node)
    .has_value();
}

auto Scene::GetFirstChild(SceneNode& node) const noexcept
  -> std::optional<SceneNode>
{
  return SafeCall(NodeIsValidAndMine(node),
    [&](const SafeCallState& state) -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, &node);
      DCHECK_NOTNULL_F(state.node_impl);

      return GetFirstChildUnsafe(node, state.node_impl);
    });
}

auto Scene::GetFirstChildUnsafe(SceneNode& node,
  const SceneNodeImpl* node_impl) const -> std::optional<SceneNode>
{
  DCHECK_NOTNULL_F(node_impl);

  return RelativeIsAliveOrInvalidateTarget(
    node_impl->AsGraphNode().GetFirstChild(), node);
}

auto Scene::GetNextSibling(SceneNode& node) const noexcept
  -> std::optional<SceneNode>
{
  return SafeCall(NodeIsValidAndMine(node),
    [&](const SafeCallState& state) -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, &node);
      DCHECK_NOTNULL_F(state.node_impl);

      return GetNextSiblingUnsafe(node, state.node_impl);
    });
}

auto Scene::GetNextSiblingUnsafe(SceneNode& node,
  const SceneNodeImpl* node_impl) const -> std::optional<SceneNode>
{
  DCHECK_NOTNULL_F(node_impl);

  return RelativeIsAliveOrInvalidateTarget(
    node_impl->AsGraphNode().GetNextSibling(), node);
}

auto Scene::GetPrevSibling(SceneNode& node) const noexcept
  -> std::optional<SceneNode>
{
  return SafeCall(NodeIsValidAndMine(node),
    [&](const SafeCallState& state) -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, &node);
      DCHECK_NOTNULL_F(state.node_impl);

      return GetPrevSiblingUnsafe(node, state.node_impl);
    });
}

auto Scene::GetPrevSiblingUnsafe(SceneNode& node,
  const SceneNodeImpl* node_impl) const -> std::optional<SceneNode>
{
  DCHECK_NOTNULL_F(node_impl);

  return RelativeIsAliveOrInvalidateTarget(
    node_impl->AsGraphNode().GetPrevSibling(), node);
}

auto Scene::GetNodeImpl(const SceneNode& node) noexcept -> OptionalRefToImpl
{
  // This is a logic error, should be fixed in the code. An invalid handle
  // should not be used anymore.
  CHECK_F(node.IsValid(), "expecting a valid node handle");

  try {
    auto& impl = nodes_->ItemAt(node.GetHandle());
    return { impl };
  } catch ([[maybe_unused]] const std::exception& ex) {
    // If the handle is valid but the node is no longer in the scene, this
    // is a case for lazy invalidation.
    DLOG_F(4, "Node {} is no longer there -> invalidate : {}",
      to_string_compact(node.GetHandle()), ex.what());
    node.Invalidate();
    return std::nullopt;
  }
}

auto Scene::GetNodeImpl(const SceneNode& node) const noexcept
  -> OptionalConstRefToImpl
{
  return const_cast<Scene*>(this)->GetNodeImpl(node);
}

auto Scene::GetNodeImplRef(const ResourceHandle& handle) noexcept
  -> SceneNodeImpl&
{
  // This is a logic error, should be fixed in the code. An invalid handle
  // should not be used anymore.
  CHECK_F(handle.IsValid(), "expecting a valid node handle");

  try {
    return GetNodeImplRefUnsafe(handle);
  } catch (const std::exception&) {
    ABORT_F("expecting the node to exist");
  }
}

auto Scene::GetNodeImplRef(const ResourceHandle& handle) const noexcept
  -> const SceneNodeImpl&
{
  return const_cast<Scene*>(this)->GetNodeImplRef(handle);
}

auto Scene::GetNodeImplRefUnsafe(const ResourceHandle& handle) const
  -> const SceneNodeImpl&
{
  return nodes_->ItemAt(handle);
}

auto Scene::GetNodeImplRefUnsafe(const ResourceHandle& handle) -> SceneNodeImpl&
{
  return nodes_->ItemAt(handle);
}

auto Scene::TryGetNodeImpl(const ResourceHandle& handle) noexcept
  -> SceneNodeImpl*
{
  return nodes_->TryGet(handle);
}

auto Scene::TryGetNodeImpl(const ResourceHandle& handle) const noexcept
  -> const SceneNodeImpl*
{
  return nodes_->TryGet(handle);
}

auto Scene::GetNode(const NodeHandle& handle) const noexcept
  -> std::optional<SceneNode>
{
  if (!nodes_->Contains(handle)) {
    return std::nullopt;
  }
  return SceneNode(std::const_pointer_cast<Scene>(shared_from_this()), handle);
}

auto Scene::Contains(const SceneNode& node) const noexcept -> bool
{
  // First ensure the node is associated with this scene.
  if (node.scene_weak_.expired() || node.scene_weak_.lock().get() != this) {
    return false;
  }

  // Then check if the handle is valid and exists in our node table
  if (!nodes_->Contains(node.GetHandle())) {
    return false;
  }

  return true;
}

auto Scene::GetNodeCount() const noexcept -> size_t { return nodes_->Size(); }

auto Scene::GetNodes() const noexcept -> const NodeTable& { return *nodes_; }

auto Scene::IsEmpty() const noexcept -> bool { return nodes_->IsEmpty(); }

auto Scene::GetChildrenCount(const SceneNode& parent) const noexcept -> size_t
{
  // This is a logic error, should be fixed in the code. An invalid handle
  // should not be used anymore.
  CHECK_F(
    parent.IsValid(), "Parent node handle is not valid for GetChildrenCount");

  const auto parent_impl_opt = GetNodeImpl(parent);
  if (!parent_impl_opt) {
    return 0;
  }

  // We do a count of children by iterating through the linked list, we
  // terminate the program if any of the children is not valid. This is
  // clearly an indication of a logic error, and should be fixed in the code.
  size_t count = 0;
  auto current_child_handle
    = parent_impl_opt->get().AsGraphNode().GetFirstChild();
  while (current_child_handle.IsValid()) {
    auto& child_node_impl = GetNodeImplRef(current_child_handle);
    ++count;
    current_child_handle = child_node_impl.AsGraphNode().GetNextSibling();
  }
  return count;
}

auto Scene::GetChildren(const SceneNode& parent) const
  -> std::vector<NodeHandle>
{
  // This is a logic error, should be fixed in the code. An invalid handle
  // should not be used anymore.
  CHECK_F(parent.IsValid(), "Parent node handle is not valid for GetChildren");

  const auto parent_impl_opt = GetNodeImpl(parent);
  if (!parent_impl_opt) {
    return {};
  }

  // We terminate the program if any of the children is not valid. This is
  // clearly an indication of a logic error, and should be fixed in the code.
  std::vector<NodeHandle> children;
  auto current_child_handle
    = parent_impl_opt->get().AsGraphNode().GetFirstChild();
  while (current_child_handle.IsValid()) {
    const auto& child_impl = GetNodeImplRef(current_child_handle);
    children.push_back(current_child_handle);
    current_child_handle = child_impl.AsGraphNode().GetNextSibling();
  }
  return children;
}

void Scene::AddRootNode(const NodeHandle& node)
{
  // Ensure no duplicate root nodes
  DCHECK_F(std::ranges::find(root_nodes_, node) == root_nodes_.end(),
    "duplicate root node detected");
  root_nodes_.push_back(node);
}

void Scene::RemoveRootNode(const NodeHandle& node)
{
  std::erase(root_nodes_, node);
}

void Scene::EnsureRootNodesValid() const noexcept
{
#if !defined(NDEBUG)
  for (const auto& handle : root_nodes_) {
    // A bug that needs fixing.
    DCHECK_F(handle.IsValid(), "expecting a valid root node handle");
    // This is also a bug that needs fixing.
    DCHECK_F(nodes_->Contains(handle),
      "expecting root nodes to be in the scene or not in the root nodes "
      "set");
  }
#endif // NDEBUG
}

auto Scene::GetRootNodes() const -> std::vector<SceneNode>
{
  EnsureRootNodesValid();

  std::vector<SceneNode> nodes {};
  nodes.reserve(root_nodes_.size());
  for (const auto& handle : root_nodes_) {
    nodes.emplace_back(
      std::const_pointer_cast<Scene>(shared_from_this()), handle);
  }
  return nodes;
}

auto Scene::GetRootHandles() const -> std::span<const NodeHandle>
{
  EnsureRootNodesValid();
  return { root_nodes_.data(), root_nodes_.size() };
}

/*!
 Expects the child node to be an orphan, with no exiting hierarchy links to a
 parent or to siblings. Such a node is usually a newly created one, or one
 obtained through a call to UnlinkNode().

 The child node may have children though. In such case, its children will remain
 attached, and as result, the entire subtree will be preserved.

 @note Transform management is the responsibility of the caller. This method
 does NOT mark transforms as dirty - the caller should handle transform
 preservation or dirty marking as appropriate for their use case.
*/
void Scene::LinkChild(const NodeHandle& parent_handle,
  SceneNodeImpl* parent_impl, const NodeHandle& child_handle,
  SceneNodeImpl* child_impl) noexcept
{
  DCHECK_F(parent_handle.IsValid());
  DCHECK_NOTNULL_F(parent_impl);
  DCHECK_F(child_handle.IsValid());
  DCHECK_NOTNULL_F(child_impl);
  DCHECK_F(!child_impl->AsGraphNode().GetParent().IsValid());
  DCHECK_F(!child_impl->AsGraphNode().GetPrevSibling().IsValid());
  DCHECK_F(!child_impl->AsGraphNode().GetNextSibling().IsValid());

  DCHECK_NE_F(parent_impl, child_impl, "cannot link a node to itself");

  DLOG_SCOPE_F(3, "Link Child Node");
  DLOG_F(3, "child node `{}`: {}", child_impl->GetName(),
    to_string_compact(child_handle));
  DLOG_F(3, "parent node: `{}`: {}", parent_impl->GetName(),
    to_string_compact(parent_handle));

  // TODO: Ensure not creating a cyclic dependency

  // For a fast and optimized hierarchy linking, we link the new child to always
  // become the new first child. The implication of this is that there will be
  // no order guarantee in the collection of children. This is acceptable,
  // especially knowing that the scene graph will usually undergo several
  // transformations that will not preserve the creation order of nodes.
  if (const auto first_child_handle
    = parent_impl->AsGraphNode().GetFirstChild();
    first_child_handle.IsValid()) {
    // Set the new child's next sibling to the current first child
    child_impl->AsGraphNode().SetNextSibling(first_child_handle);
    // Set the current first child's previous sibling to the new child
    auto& first_child_impl = GetNodeImplRef(first_child_handle);
    first_child_impl.AsGraphNode().SetPrevSibling(child_handle);
  }
  // Set the new child's parent to the parent node
  child_impl->AsGraphNode().SetParent(parent_handle);
  // Set the parent's first child to the new child
  parent_impl->AsGraphNode().SetFirstChild(child_handle);
}

/*!
 This method does not destroy the node, it only removes it from the hierarchy.
 If the node must be destroyed, DestroyNode() or DestroyNodeHierarchy() should
 be used after un-linking. If it is simply being detached, it needs to be added
 to the roots set using AddRootNode().

 @note Transform management is the responsibility of the caller. This method
 does NOT mark transforms as dirty - the caller should handle transform
 preservation or dirty marking as appropriate for their use case.
*/
void Scene::UnlinkNode(
  const NodeHandle& node_handle, SceneNodeImpl* node_impl) noexcept
{
  DCHECK_F(node_handle.IsValid());
  DCHECK_NOTNULL_F(node_impl);

  DLOG_SCOPE_F(3, "Unlink Node");
  DLOG_F(
    3, "node `{}`: {}", node_impl->GetName(), to_string_compact(node_handle));

  // Get parent, next sibling, and previous sibling handles
  const NodeHandle parent_handle = node_impl->AsGraphNode().GetParent();
  const NodeHandle next_sibling_handle
    = node_impl->AsGraphNode().GetNextSibling();
  const NodeHandle prev_sibling_handle
    = node_impl->AsGraphNode().GetPrevSibling();

  // Update the parent's first_child pointer if this node_handle is the first
  // child
  if (parent_handle.IsValid()) {
    auto& parent_impl = GetNodeImplRef(parent_handle);
    DLOG_F(3, "parent `{}`: {}", parent_impl.GetName(),
      to_string_compact(parent_handle));
    if (parent_impl.AsGraphNode().GetFirstChild() == node_handle) {
      // This node_handle is the first child of its parent
      // Update parent to point to the next sibling as its first child
      parent_impl.AsGraphNode().SetFirstChild(next_sibling_handle);
    }
  }

  // Update previous sibling's next_sibling pointer if it exists
  if (prev_sibling_handle.IsValid()) {
    auto& prev_sibling_impl = GetNodeImplRef(prev_sibling_handle);
    DLOG_F(3, "prev sibling `{}`: {}", prev_sibling_impl.GetName(),
      to_string_compact(prev_sibling_handle));
    prev_sibling_impl.AsGraphNode().SetNextSibling(next_sibling_handle);
  }

  // Update next sibling's prev_sibling pointer if it exists
  if (next_sibling_handle.IsValid()) {
    auto& next_sibling_impl = GetNodeImplRef(next_sibling_handle);
    DLOG_F(3, "next sibling `{}`: {}", next_sibling_impl.GetName(),
      to_string_compact(next_sibling_handle));
    next_sibling_impl.AsGraphNode().SetPrevSibling(prev_sibling_handle);
  }
  // Reset the node_handle's parent, next sibling, and previous sibling
  node_impl->AsGraphNode().SetParent({});
  node_impl->AsGraphNode().SetNextSibling({});
  node_impl->AsGraphNode().SetPrevSibling({});

  LOG_F(3, "node unlinked from hierarchy");
}

//------------------------------------------------------------------------------
// Scene Update and Dirty Flags Processing
//------------------------------------------------------------------------------

namespace {

//! Marks the transform as dirty for a node and all its descendants
//! (non-recursive).
void MarkSubtreeTransformDirty(
  Scene& scene, const NodeHandle& root_handle) noexcept
{
  std::vector<SceneNodeImpl*> stack;
  size_t count = 0;
  stack.push_back(&scene.GetNodeImplRef(root_handle));
  while (!stack.empty()) {
    SceneNodeImpl* node = stack.back();
    stack.pop_back();
    node->MarkTransformDirty();
    ++count;
    auto child_handle = node->AsGraphNode().GetFirstChild();
    while (child_handle.IsValid()) {
      stack.push_back(&scene.GetNodeImplRef(child_handle));
      child_handle
        = scene.GetNodeImplRef(child_handle).AsGraphNode().GetNextSibling();
    }
  }
  DLOG_F(2, "Marked {} nodes as transform dirty (subtree rooted at: {})", count,
    scene.GetNodeImplRef(root_handle).GetName());
}

} // namespace

void Scene::MarkSubtreeTransformDirty(const NodeHandle& root_handle) noexcept
{
  ::MarkSubtreeTransformDirty(*this, root_handle);
}

auto Scene::Traverse() const -> NonMutatingTraversal
{
  return NonMutatingTraversal(shared_from_this());
}

auto Scene::Traverse() -> MutatingTraversal
{
  return MutatingTraversal(shared_from_this());
}

/*!
 Provides access to the SceneQuery API for efficient scene graph queries
 including batch operations, predicate-based searches, and optimized traversals.

 @return SceneQuery instance configured for this scene

 <b>Usage</b>:
 ```cpp
   auto query = scene->Query();
   auto node = query.FindFirst([](const auto& visited) {
     return visited.node.GetName() == "TargetNode";
   });
 ```
*/
auto Scene::Query() const -> SceneQuery
{
  return SceneQuery(shared_from_this());
}

auto Scene::RegisterObserver(const observer_ptr<ISceneObserver> observer,
  const SceneMutationMask mutation_mask) noexcept -> bool
{
  if (observer == nullptr) {
    return false;
  }
  for (auto& subscription : observers_) {
    if (subscription.observer != observer) {
      continue;
    }
    return false;
  }
  observers_.push_back(ObserverSubscription {
    .observer = observer,
    .mutation_mask = mutation_mask,
  });
  UpdateMutationCollectionState();
  return true;
}

auto Scene::RegisterObserver(
  const observer_ptr<ISceneObserver> observer) noexcept -> bool
{
  return RegisterObserver(observer, SceneMutationMask::kAllMutations);
}

auto Scene::UnregisterObserver(
  const observer_ptr<ISceneObserver> observer) noexcept -> bool
{
  const auto previous_size = observers_.size();
  std::erase_if(
    observers_, [observer](const ObserverSubscription& subscription) {
      return subscription.observer == observer;
    });
  const auto changed = observers_.size() != previous_size;
  if (changed && observers_.empty() && mutation_collector_
    && !hydration_mutation_collection_enabled_) {
    // Contract: new observers receive only future mutations unless hydration
    // collection was explicitly enabled.
    mutation_collector_->ClearMutations();
  }
  UpdateMutationCollectionState();
  return changed;
}

auto Scene::AsMutationCollector() const noexcept
  -> observer_ptr<internal::IMutationCollector>
{
  return observer_ptr<internal::IMutationCollector>(mutation_collector_.get());
}

auto Scene::ResolveScriptSlot(const NodeHandle& node_handle,
  const ScriptSlotIndex slot_index) const noexcept
  -> const ScriptingComponent::Slot*
{
  if (!node_handle.IsValid() || !nodes_->Contains(node_handle)) {
    return nullptr;
  }
  const auto& node_impl = GetNodeImplRef(node_handle);
  if (!node_impl.HasComponent<ScriptingComponent>()) {
    return nullptr;
  }
  const auto slots = node_impl.GetComponent<ScriptingComponent>().Slots();
  if (slot_index.get() >= slots.size()) {
    return nullptr;
  }
  return &slots[slot_index.get()];
}

auto Scene::NotifyObservers(const SceneMutationMask mutation_type,
  const NodeHandle& node_handle, const ScriptSlotIndex slot_index,
  const ScriptingComponent::Slot* slot) const -> void
{
  for (const auto& subscription : observers_) {
    if (subscription.observer == nullptr) {
      continue;
    }
    const auto mask
      = static_cast<uint32_t>(subscription.mutation_mask & mutation_type);
    if (mask == 0) {
      continue;
    }

    switch (mutation_type) {
    case SceneMutationMask::kScriptSlotActivated:
      DCHECK_NOTNULL_F(slot);
      subscription.observer->OnScriptSlotActivated(
        node_handle, slot_index, *slot);
      break;
    case SceneMutationMask::kScriptSlotChanged:
      DCHECK_NOTNULL_F(slot);
      subscription.observer->OnScriptSlotChanged(
        node_handle, slot_index, *slot);
      break;
    case SceneMutationMask::kScriptSlotDeactivated:
      subscription.observer->OnScriptSlotDeactivated(node_handle, slot_index);
      break;
    case SceneMutationMask::kLightChanged:
      subscription.observer->OnLightChanged(node_handle);
      break;
    case SceneMutationMask::kCameraChanged:
      subscription.observer->OnCameraChanged(node_handle);
      break;
    case SceneMutationMask::kTransformChanged:
      subscription.observer->OnTransformChanged(node_handle);
      break;
    case SceneMutationMask::kNodeDestroyed:
      subscription.observer->OnNodeDestroyed(node_handle);
      break;
    case SceneMutationMask::kNone:
    case SceneMutationMask::kAllScriptSlotMutations:
    case SceneMutationMask::kAllMutations:
      break;
    }
  }
}

auto Scene::SyncObservers() -> void
{
  if (observers_.empty() || !mutation_collector_ || !mutation_dispatcher_
    || !script_slot_processor_) {
    return;
  }

  mutation_dispatcher_->Dispatch(*mutation_collector_,
    internal::IMutationDispatcher::DispatchContext {
      .resolve_script_slot =
        [this](
          const NodeHandle& node_handle, const ScriptSlotIndex slot_index) {
          return ResolveScriptSlot(node_handle, slot_index);
        },
      .notify_script_observers =
        [this](const SceneMutationMask mutation_type,
          const NodeHandle& node_handle, const ScriptSlotIndex slot_index,
          const ScriptingComponent::Slot* slot) {
          NotifyObservers(mutation_type, node_handle, slot_index, slot);
        },
      .notify_light_mutation =
        [this](const internal::LightMutation& mutation) {
          NotifyObservers(SceneMutationMask::kLightChanged,
            mutation.node_handle, ScriptSlotIndex {}, nullptr);
        },
      .notify_camera_mutation =
        [this](const internal::CameraMutation& mutation) {
          NotifyObservers(SceneMutationMask::kCameraChanged,
            mutation.node_handle, ScriptSlotIndex {}, nullptr);
        },
      .notify_transform_mutation =
        [this](const internal::TransformMutation& mutation) {
          NotifyObservers(SceneMutationMask::kTransformChanged,
            mutation.node_handle, ScriptSlotIndex {}, nullptr);
        },
      .notify_node_destroyed_mutation =
        [this](const internal::NodeDestroyedMutation& mutation) {
          NotifyObservers(SceneMutationMask::kNodeDestroyed,
            mutation.node_handle, ScriptSlotIndex {}, nullptr);
        },
    });
}

auto Scene::GetMutationDispatchCounters() const noexcept
  -> MutationDispatchCounters
{
  if (!mutation_dispatcher_) {
    return {};
  }
  const auto counters = mutation_dispatcher_->GetCounters();
  return MutationDispatchCounters {
    .sync_calls = counters.sync_calls,
    .frames_with_mutations = counters.frames_with_mutations,
    .drained_records = counters.drained_records,
    .script_records_dispatched = counters.script_records_dispatched,
    .light_records_coalesced_in = counters.light_records_coalesced_in,
    .light_records_dispatched = counters.light_records_dispatched,
    .camera_records_coalesced_in = counters.camera_records_coalesced_in,
    .camera_records_dispatched = counters.camera_records_dispatched,
    .transform_records_coalesced_in = counters.transform_records_coalesced_in,
    .transform_records_dispatched = counters.transform_records_dispatched,
    .node_destroyed_records_dispatched
    = counters.node_destroyed_records_dispatched,
  };
}

auto Scene::CollectMutationsStart() noexcept -> void
{
  hydration_mutation_collection_enabled_ = true;
  UpdateMutationCollectionState();
}

auto Scene::CollectMutationsEnd() noexcept -> void
{
  hydration_mutation_collection_enabled_ = false;
  UpdateMutationCollectionState();
}

auto Scene::UpdateMutationCollectionState(const bool log_transition) -> void
{
  if (!mutation_collector_) {
    return;
  }
  const bool enabled
    = hydration_mutation_collection_enabled_ || !observers_.empty();
  const bool was_enabled = mutation_collector_->IsEnabled();
  mutation_collector_->SetEnabled(enabled);
  if (log_transition && was_enabled != enabled) {
    LOG_F(INFO, "mutation collection {}", enabled ? "enabled" : "disabled");
  }
}

// ReSharper disable once CppMemberFunctionMayBeConst
void Scene::Update(const bool skip_dirty_flags) noexcept
{
  LOG_SCOPE_F(2, "Scene update");
  LOG_SCOPE_F(2, "PASS 1 - Flags + dirty subtree counts");
  [[maybe_unused]] const auto processed_nodes_with_dirty_flags
    = Traverse().PrepareDirtyFlagsAndSubtreeCounts(!skip_dirty_flags);
  if (!skip_dirty_flags) {
    DLOG_F(2, "{}/{} nodes had dirty flags", processed_nodes_with_dirty_flags,
      GetNodes().Size());
  }

  // Pass 2: Update transforms with subtree rejection.
  LOG_SCOPE_F(2, "PASS 2 - Update transforms");
  [[maybe_unused]] const auto updated_count = Traverse().UpdateTransforms();
  DLOG_F(2, "Updated transforms for {} nodes", updated_count);
}
