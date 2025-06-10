//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <functional>
#include <ranges>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ResourceTable.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Core/SafeCall.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneTraversal.h>

using oxygen::scene::NodeHandle;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeImpl;
using oxygen::scene::VisitedNode;

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

// =============================================================================
// Scene Validator Implementations
// =============================================================================

template <typename Self, typename Validator, typename Func>
auto Scene::SafeCall(
  Self* self, Validator validator, Func&& func) const noexcept
{
  SafeCallState state;
  auto result = oxygen::SafeCall(
    *self,
    [&](auto&& /*self_ref*/) -> std::optional<std::string> {
      // Invoke the validator to populate the state
      return validator(state);
    },
    [func = std::forward<Func>(func), &state](
      auto&& /*self_ref*/) mutable noexcept {
      try {
        return func(state);
      } catch (const std::exception& ex) {
        DLOG_F(ERROR, "scene operation failed due to exception: {}", ex.what());
        return decltype(func(state)) {}; // Return default-constructed type
      } catch (...) {
        DLOG_F(ERROR, "scene operation failed due to unknown error");
        return decltype(func(state)) {}; // Return default-constructed type
      }
    });

  // Extract the actual value from the oxygen::SafeCall result. This would
  // work with operations that return std::options<T> or bool.
  if (result.has_value()) {
    return result.value();
  }
  return decltype(func(state)) {}; // Return default-constructed type
}

template <typename Validator, typename Func>
auto Scene::SafeCall(Validator&& validator, Func&& func) const noexcept
{
  return SafeCall(
    this, std::forward<Validator>(validator), std::forward<Func>(func));
}

template <typename Validator, typename Func>
auto Scene::SafeCall(Validator&& validator, Func&& func) noexcept
{
  return SafeCall(
    this, std::forward<Validator>(validator), std::forward<Func>(func));
}

void Scene::LogSafeCallError(const char* reason) noexcept
{
  try {
    DLOG_F(ERROR, "Graph operation failed: {}", reason);
  } catch (...) {
    // If logging fails, we can do nothing about it
    (void)0;
  }
}
static_assert(oxygen::HasLogSafeCallError<Scene>);

class Scene::BaseNodeValidator {
public:
  BaseNodeValidator(
    const Scene* target_scene, const SceneNode& target_node) noexcept
    : scene_(target_scene)
    , node_(&target_node)
  {
  }

protected:
  [[nodiscard]] auto GetScene() const noexcept -> const Scene*
  {
    return scene_;
  }

  [[nodiscard]] auto GetNode() const noexcept -> const SceneNode&
  {
    return *node_;
  }

  [[nodiscard]] auto GetResult() noexcept { return std::move(result_); }

  auto EnsureScene() -> bool
  {
    if (GetScene() == nullptr) [[unlikely]] {
      // This is a programming logic error, and is fatal.
      ABORT_F("scene for node({}) does not exit anymore",
        nostd::to_string(GetNode()));
    }
    result_.reset();
    return true;
  }

  auto EnsureSceneOwnsNode() -> bool
  {
    DCHECK_F(GetScene() != nullptr, "call EnsureScene() before");

    if (!GetScene()->IsOwnerOf(GetNode())) [[unlikely]] {
      // This is a programming logic error, and is fatal.
      ABORT_F("node({}) does not belong to scene `{}`",
        nostd::to_string(GetNode()), GetScene()->GetName());
    }
    result_.reset();
    return true;
  }

  auto EnsureNodeHasNoChildren() -> bool
  {
    DCHECK_NOTNULL_F(GetScene(), "call EnsureScene() before");

    if (GetNode().HasChildren()) [[unlikely]] {
      // This is a programming logic error, and is fatal.
      ABORT_F("node({}) has children; use hierarchy API methods on it",
        nostd::to_string(GetNode()), GetScene()->GetName());
    }
    result_.reset();
    return true;
  }

  auto CheckNodeIsValid() -> bool
  {
    // In debug mode, we can also explicitly check if the node is valid.
    // This is not strictly needed, as nodes_ table will check if the handle
    // is within bounds (i.e. valid), but it can help troubleshoot exactly
    // the reason why validation failed.
    if (!GetNode().IsValid()) [[unlikely]] {
      result_ = fmt::format("node({}) is invalid", nostd::to_string(GetNode()));
      return false;
    }
    result_.reset();
    return true;
  }

  auto PopulateStateWithNodeImpl(SafeCallState& state) -> bool
  {
    DCHECK_NOTNULL_F(GetScene(), "call EnsureScene() before");

    // Then check if the node is still in the scene node table, and retrieve
    // its implementation object.
    try {
      auto& impl = GetScene()->nodes_->ItemAt(GetNode().GetHandle());
      state.node_impl = &impl;
      result_.reset();
      return true;
    } catch (const std::exception&) {
      // Invalidate the node (lazy invalidation)
      state.node->Invalidate();
      result_ = fmt::format(
        "node({}) is no longer in scene `{}` -> lazily invalidated",
        nostd::to_string(GetNode().GetHandle()), GetScene()->GetName());
      return false;
    }
  }

private:
  std::optional<std::string> result_ {};
  const Scene* scene_;
  const SceneNode* node_;
};

//! A validator that checks if a SceneNode is valid and belongs to the \p scene.
class Scene::NodeIsValidAndInSceneValidator : public BaseNodeValidator {
public:
  explicit NodeIsValidAndInSceneValidator(
    const Scene* target_scene, const SceneNode& target_node) noexcept
    : BaseNodeValidator(target_scene, target_node)
  {
  }

  auto operator()(SafeCallState& state) -> std::optional<std::string>
  {
    state.node = const_cast<SceneNode*>(&GetNode());
    if (EnsureScene() && EnsureSceneOwnsNode() && CheckNodeIsValid()
      && PopulateStateWithNodeImpl(state)) [[likely]] {
      // All validations passed
      return std::nullopt;
    }
    return GetResult();
  }
};

//! A validator that checks if a SceneNode is valid and belongs to the \p scene.
class Scene::LeafNodeCanBeDestroyedValidator : public BaseNodeValidator {
public:
  explicit LeafNodeCanBeDestroyedValidator(
    const Scene* target_scene, const SceneNode& target_node) noexcept
    : BaseNodeValidator(target_scene, target_node)
  {
  }

  auto operator()(SafeCallState& state) -> std::optional<std::string>
  {
    state.node = const_cast<SceneNode*>(&GetNode());
    if (EnsureScene() && EnsureSceneOwnsNode()
      && PopulateStateWithNodeImpl(state) && EnsureNodeHasNoChildren())
      [[likely]] {
      // All validations passed
      return std::nullopt;
    }
    return GetResult();
  }
};

auto Scene::NodeIsValidAndMine(const SceneNode& node) const
  -> NodeIsValidAndInSceneValidator
{
  return NodeIsValidAndInSceneValidator(this, node);
}

auto Scene::NodeIsValidAndInScene(const SceneNode& node) const
  -> NodeIsValidAndInSceneValidator
{
  const Scene* scene { nullptr };
  if (!node.scene_weak_.expired()) [[likely]] {
    scene = node.scene_weak_.lock().get();
  }
  return NodeIsValidAndInSceneValidator(scene, node);
}

auto Scene::LeafNodeCanBeDestroyed(const SceneNode& node) const
  -> LeafNodeCanBeDestroyedValidator
{
  return LeafNodeCanBeDestroyedValidator(this, node);
}

//------------------------------------------------------------------------------
// Basic Scene Operations
//------------------------------------------------------------------------------

Scene::Scene(const std::string& name, size_t initial_capacity)
  : nodes_(std::make_shared<NodeTable>(resources::kSceneNode, initial_capacity))
  , traversal_(new SceneTraversal(*this))
{
  LOG_SCOPE_F(INFO, "Scene creation");
  LOG_F(2, "name: '{}'", name);
  LOG_F(2, "initial capacity: '{}'", initial_capacity);

  AddComponent<ObjectMetaData>(name);

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
}

Scene::~Scene()
{
  LOG_SCOPE_F(INFO, "Scene destruction");
  delete traversal_;

  // Release the scene ID for reuse
  SceneIdManager::Instance().ReleaseId(scene_id_);
}

auto Scene::GetName() const noexcept -> std::string_view
{
  return GetComponent<ObjectMetaData>().GetName();
}

// ReSharper disable once CppMemberFunctionMayBeConst
void Scene::SetName(const std::string_view name) noexcept
{
  GetComponent<ObjectMetaData>().SetName(name);
}

//------------------------------------------------------------------------------
// Scene graph operations
//------------------------------------------------------------------------------

auto Scene::IsOwnerOf(const SceneNode& node) const -> bool
{
  return !node.scene_weak_.expired() && node.scene_weak_.lock().get() == this;
}

/*!
 This method creates a new scene node and adds it to this scene as a root node.
 The created node will have no parent and will be automatically added to the
 scene's root nodes collection.

 This call will never fail, unless the resource table is full. In such a case,
 the application will terminate.

 @tparam Args Variadic template arguments forwarded to SceneNodeImpl
 constructor. Must match SceneNodeImpl constructor parameters (e.g., name,
 flags).

 @param args Arguments forwarded to SceneNodeImpl constructor. Typically
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
  return SceneNode(shared_from_this(), handle);
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

 __Failure Scenarios:__
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
 @param args Arguments forwarded to SceneNodeImpl constructor. Typically
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
auto Scene::CreateChildNodeImpl(
  const SceneNode& parent, Args&&... args) noexcept -> std::optional<SceneNode>
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
      return SceneNode(shared_from_this(), child_handle);
    });
}

/*!
 This method creates a new scene node and links it as a child to the specified
 \p parent node. The created node will be properly inserted into the scene
 hierarchy with all parent-child relationships established.

 __Failure Scenarios:__
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
auto Scene::CreateChildNode(const SceneNode& parent,
  const std::string& name) noexcept -> std::optional<SceneNode>
{
  return CreateChildNodeImpl(parent, name);
}

/*!
 \copydetails CreateChildNode(const SceneNode&, const std::string&)
 @param flags Flags to assign to the new node.
*/
auto Scene::CreateChildNode(const SceneNode& parent, const std::string& name,
  const SceneNode::Flags flags) noexcept -> std::optional<SceneNode>
{
  return CreateChildNodeImpl(parent, name, flags);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
auto Scene::DestroyNode(SceneNode& node) noexcept -> bool
{
  DLOG_SCOPE_F(3, "Destroy Node");
  return SafeCall(
    LeafNodeCanBeDestroyed(node), [&](const SafeCallState& state) -> bool {
      DCHECK_EQ_F(state.node, &node);
      DCHECK_NOTNULL_F(state.node_impl);

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

auto Scene::DestroyNodeHierarchy(SceneNode& starting_node) noexcept -> bool
{
  DLOG_SCOPE_F(3, "Destroy Node Hierarchy");
  return SafeCall(
    NodeIsValidAndMine(starting_node), [&](const SafeCallState& state) -> bool {
      DCHECK_EQ_F(state.node, &starting_node);
      DCHECK_NOTNULL_F(state.node_impl);

      // Non-recursive implementation using a stack for performance
      // Collect all nodes in the hierarchy first, then destroy them
      // bottom-up
      std::vector<SceneNode> nodes_to_destroy;
      std::vector<SceneNode> stack;

      // Start with the starting node
      stack.push_back(starting_node);

      // Traverse the hierarchy depth-first to collect all nodes
      while (!stack.empty()) {
        SceneNode current = stack.back();
        stack.pop_back();

        // Add current node to destruction list
        nodes_to_destroy.push_back(current);

        // Check if node still exists (may have been destroyed in a
        // previous iteration)
        if (!Contains(current)) {
          continue; // Skip if node was already destroyed or
                    // invalid
        }

        // Add all children to the stack using SceneNode's public
        // API
        auto child_opt = current.GetFirstChild();
        while (child_opt.has_value()) {
          SceneNode child = child_opt.value();
          stack.push_back(child);

          // Move to next sibling using SceneNode's public API
          child_opt = child.GetNextSibling();
        }
      } // First, handle the starting node's relationship with its
        // parent/scene
      // If it's a scene root node, remove it from the root nodes
      // collection If it has a parent, unlink it from its parent
      if (starting_node.IsRoot()) {
        // This is an actual scene root node - remove from root
        // nodes collection
        RemoveRootNode(starting_node.GetHandle());
      } else {
        // This node has a parent - unlink it from its parent
        // This is the only un-linking we need since we're destroying
        // the entire subtree
        UnlinkNode(starting_node.GetHandle(), state.node_impl);
      }
      // Destroy all nodes in the hierarchy
      // We can destroy in any order since we've already unlinked the
      // starting node
      size_t destroyed_count = 0;
      for (const SceneNode& node : nodes_to_destroy) {
        // Skip if node was already destroyed or is no longer valid
        if (!Contains(node)) {
          continue;
        }

        // Destroy the node data directly - no need to unlink since
        // entire subtree is being destroyed
        const auto removed = nodes_->Erase(node.GetHandle());
        if (removed > 0) {
          ++destroyed_count;
        }
      }

      // Invalidate the starting node handle since it's been destroyed
      starting_node.Invalidate();

      DLOG_F(2, "Destroyed {} nodes in hierarchy", destroyed_count);
      return destroyed_count > 0;
    });
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
  nodes_->Clear();
  root_nodes_.clear();
}

auto Scene::GetParent(const SceneNode& node) const noexcept
  -> std::optional<SceneNode>
{
  return SafeCall(NodeIsValidAndMine(node),
    [&](const SafeCallState& state) -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, &node);
      DCHECK_NOTNULL_F(state.node_impl);

      return GetParentUnsafe(node, state.node_impl);
    });
}

auto Scene::GetParentUnsafe(const SceneNode& node,
  const SceneNodeImpl* node_impl) const -> std::optional<SceneNode>
{
  DCHECK_NOTNULL_F(node_impl);

  const auto parent_handle = node_impl->AsGraphNode().GetParent();

  // Bail out quickly if the handle is not valid.
  if (!parent_handle.IsValid()) {
    return std::nullopt;
  }

  // Check the the parent node is still alive
  if (nodes_->Contains(parent_handle)) {
    return SceneNode(
      std::const_pointer_cast<Scene>(this->shared_from_this()), parent_handle);
  }

  // The parent node is no longer alive, likely due to recent hierarchy
  // destruction. Lazily invalidate this node. Properly destroyed hierarchies
  // destroy all descendants under the starting node, so a node with a valid
  // parent handle cannot exist if it's parent does not.
  DLOG_F(4, "Parent node is no longer there: {} -> child node {} invalidated",
    nostd::to_string(parent_handle), nostd::to_string(node));
  node.Invalidate();
  return std::nullopt;
}

auto Scene::HasParent(const SceneNode& node) const noexcept -> bool
{
  return SafeCall(
    NodeIsValidAndMine(node), [&](const SafeCallState& state) -> bool {
      DCHECK_EQ_F(state.node, &node);
      DCHECK_NOTNULL_F(state.node_impl);

      return HasParentUnsafe(node, state.node_impl);
    });
}

auto Scene::HasParentUnsafe(
  const SceneNode& node, const SceneNodeImpl* node_impl) const -> bool
{
  DCHECK_NOTNULL_F(node_impl);

  const auto parent_handle = node_impl->AsGraphNode().GetParent();

  // Bail out quickly if the handle is not valid.
  if (!parent_handle.IsValid()) {
    return false;
  }

  if (nodes_->Contains(parent_handle)) {
    return true;
  }

  // The parent node is no longer alive, likely due to recent hierarchy
  // destruction. Lazily invalidate this node. Properly destroyed hierarchies
  // destroy all descendants under the starting node, so a node with a valid
  // parent handle cannot exist if it's parent does not.
  DLOG_F(4, "Parent node is no longer there: {} -> child node {} invalidated",
    nostd::to_string(parent_handle), nostd::to_string(node));
  node.Invalidate();

  return false;
}

auto Scene::HasChildren(const SceneNode& node) const noexcept -> bool
{
  return SafeCall(
    NodeIsValidAndMine(node), [&](const SafeCallState& state) -> bool {
      DCHECK_EQ_F(state.node, &node);
      DCHECK_NOTNULL_F(state.node_impl);

      return HasChildrenUnsafe(node, state.node_impl);
    });
}

auto Scene::HasChildrenUnsafe(
  const SceneNode& node, const SceneNodeImpl* node_impl) const -> bool
{
  DCHECK_NOTNULL_F(node_impl);

  const auto child_handle = node_impl->AsGraphNode().GetFirstChild();

  // Bail out quickly if the handle is not valid.
  if (!child_handle.IsValid()) {
    return false;
  }

  if (nodes_->Contains(child_handle)) {
    return true;
  }

  // The child node is no longer alive, likely due to recent hierarchy
  // destruction. Lazily invalidate this node. Properly destroyed children
  // unlink from their parent, so a valid node should never reference an invalid
  // first child.
  DLOG_F(4, "first child node is no longer there: {} -> node {} invalidated",
    nostd::to_string(child_handle), nostd::to_string(node));
  node.Invalidate();

  return false;
}

auto Scene::GetFirstChild(const SceneNode& node) const noexcept
  -> std::optional<SceneNode>
{
  return SafeCall(NodeIsValidAndMine(node),
    [&](const SafeCallState& state) -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, &node);
      DCHECK_NOTNULL_F(state.node_impl);

      return GetFirstChildUnsafe(node, state.node_impl);
    });
}

auto Scene::GetFirstChildUnsafe(const SceneNode& node,
  const SceneNodeImpl* node_impl) const -> std::optional<SceneNode>
{
  DCHECK_NOTNULL_F(node_impl);

  const auto child_handle = node_impl->AsGraphNode().GetFirstChild();

  // Bail out quickly if the handle is not valid.
  if (!child_handle.IsValid()) {
    return std::nullopt;
  }

  if (nodes_->Contains(child_handle)) {
    return SceneNode(
      std::const_pointer_cast<Scene>(this->shared_from_this()), child_handle);
  }

  // The child node is no longer alive, likely due to recent hierarchy
  // destruction. Lazily invalidate this node. Properly destroyed children
  // unlink from their parent, so a valid node can't have an invalid first
  // child.
  DLOG_F(4, "first child node is no longer there: {} -> node {} invalidated",
    nostd::to_string(child_handle), nostd::to_string(node));
  node.Invalidate();
  return std::nullopt;
}

auto Scene::GetNextSibling(const SceneNode& node) const noexcept
  -> std::optional<SceneNode>
{
  return SafeCall(NodeIsValidAndMine(node),
    [&](const SafeCallState& state) -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, &node);
      DCHECK_NOTNULL_F(state.node_impl);

      return GetNextSiblingUnsafe(node, state.node_impl);
    });
}

auto Scene::GetNextSiblingUnsafe(const SceneNode& node,
  const SceneNodeImpl* node_impl) const -> std::optional<SceneNode>
{
  DCHECK_NOTNULL_F(node_impl);

  const auto sibling_handle = node_impl->AsGraphNode().GetNextSibling();

  // Bail out quickly if the handle is not valid.
  if (!sibling_handle.IsValid()) {
    return std::nullopt;
  }

  if (nodes_->Contains(sibling_handle)) {
    return SceneNode(
      std::const_pointer_cast<Scene>(this->shared_from_this()), sibling_handle);
  }

  // The sibling is no longer alive, likely due to recent hierarchy
  // destruction. Lazily invalidate this node. Properly destroyed siblings
  // unlink themselves, so a valid node can't have an invalid sibling.
  DLOG_F(4, "sibling node is no longer there: {} -> node {} invalidated",
    nostd::to_string(sibling_handle), nostd::to_string(node));
  node.Invalidate();

  return std::nullopt;
}

auto Scene::GetPrevSibling(const SceneNode& node) const noexcept
  -> std::optional<SceneNode>
{
  return SafeCall(NodeIsValidAndMine(node),
    [&](const SafeCallState& state) -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, &node);
      DCHECK_NOTNULL_F(state.node_impl);

      return GetPrevSiblingUnsafe(node, state.node_impl);
    });
}

auto Scene::GetPrevSiblingUnsafe(const SceneNode& node,
  const SceneNodeImpl* node_impl) const -> std::optional<SceneNode>
{
  DCHECK_NOTNULL_F(node_impl);

  const auto sibling_handle = node_impl->AsGraphNode().GetPrevSibling();

  // Bail out quickly if the handle is not valid.
  if (!sibling_handle.IsValid()) {
    return std::nullopt;
  }

  if (nodes_->Contains(sibling_handle)) {
    return SceneNode(
      std::const_pointer_cast<Scene>(this->shared_from_this()), sibling_handle);
  }

  // The sibling node is no longer valid, likely due to recent hierarchy
  // destruction. Invalidate this node lazily. Properly destroyed siblings
  // unlink themselves, so a valid node cannot have an invalid sibling.
  DLOG_F(4, "sibling node is no longer there: {} -> node {} invalidated",
    nostd::to_string(sibling_handle), nostd::to_string(node));
  node.Invalidate();

  return std::nullopt;
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

auto Scene::GetNodeImplRef(const NodeHandle& handle) noexcept -> SceneNodeImpl&
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

auto Scene::GetNodeImplRef(const NodeHandle& handle) const noexcept
  -> const SceneNodeImpl&
{
  return const_cast<Scene*>(this)->GetNodeImplRef(handle);
}

auto Scene::GetNodeImplRefUnsafe(const NodeHandle& handle) -> SceneNodeImpl&
{
  return nodes_->ItemAt(handle);
}

auto Scene::GetNodeImplRefUnsafe(const NodeHandle& handle) const
  -> const SceneNodeImpl&
{
  return const_cast<Scene*>(this)->GetNodeImplRef(handle);
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

  // If the parent already has a first child, link the new child to it
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

  // Mark both nodes' transforms as dirty since hierarchy changed
  parent_impl->MarkTransformDirty();
  MarkSubtreeTransformDirty(child_handle);
}

/*!
 This method does not destroy the node, it only removes it from the hierarchy.
 If the node must be destroyed, DestroyNode() or DestroyNodeHierarchy() should
 be used after un-linking. If it is simply being detached, it needs to be added
 to the roots set using AddRootNode().
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

    // Mark parent's transform as dirty since hierarchy changed
    parent_impl.MarkTransformDirty();
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

  // Mark node_handle's transform as dirty since its hierarchy relationship
  // changed
  node_impl->MarkTransformDirty();

  LOG_F(3, "node unlinked from hierarchy");
}

//------------------------------------------------------------------------------
// Scene Update and Dirty Flags Processing
//------------------------------------------------------------------------------

namespace {

//! Processes dirty flags for all nodes in the scene.
/*!
 Processes all dirty flags for each node in the resource table. This pass
 maximizes cache locality and ensures all dirty flags are handled.
*/
void ProcessDirtyFlags(const Scene& scene) noexcept
{
  LOG_SCOPE_F(2, "PASS 1 - Dirty flags");
  auto& node_table = scene.GetNodes();
  size_t processed_count = 0;
  for (size_t i = 0; i < node_table.Size(); ++i) {
    auto& node_impl = const_cast<SceneNodeImpl&>(node_table.Items()[i]);
    LOG_SCOPE_F(2, "For Node");
    LOG_F(2, "name = {}", node_impl.GetName());
    LOG_F(2, "is root: {}", node_impl.AsGraphNode().IsRoot());
    auto& flags = node_impl.GetFlags();
    bool has_dirty_flags { false };
    for (auto flag : flags.dirty_flags()) {
      LOG_F(2, "flag: ", nostd::to_string(flag));
      flags.ProcessDirtyFlag(flag);
      if (!has_dirty_flags) {
        LOG_F(2, "Flags");
      }
      has_dirty_flags = true;
    }
    if (has_dirty_flags) {
      ++processed_count;
    }
    // Do not update transforms here.
  }
  DLOG_F(2, "{}/{} nodes had dirty flags", processed_count, node_table.Size());
}

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

// ReSharper disable once CppMemberFunctionMayBeConst
void Scene::Update(const bool skip_dirty_flags) noexcept
{
  LOG_SCOPE_F(2, "Scene update");
  if (!skip_dirty_flags) {
    // Pass 1: Process dirty flags for all nodes (linear scan,
    // cache-friendly)
    ProcessDirtyFlags(*this);
  }
  // Pass 2: Update transforms
  LOG_SCOPE_F(2, "PASS 2 - Update transforms");
  [[maybe_unused]] const auto updated_count = traversal_->UpdateTransforms();
  DLOG_F(2, "Updated transforms for {} nodes", updated_count);
}

//------------------------------------------------------------------------------
// Node cloning support
//------------------------------------------------------------------------------

/*!
 This method clones the \p original node (preserving its component data) and
 creates an __orphan__ node. The cloned node will have no hierarchy
 relationships, will not be a root node, and will have \p new_name as a name.

 __Failure Scenarios:__
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
auto Scene::CloneNode(const SceneNode& original, const std::string& new_name)
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

 __Failure Scenarios:__
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
auto Scene::CreateNodeFrom(const SceneNode& original,
  const std::string& new_name) -> std::optional<SceneNode>
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

 __Failure Scenarios:__
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
auto Scene::CreateChildNodeFrom(const SceneNode& parent,
  const SceneNode& original, const std::string& new_name)
  -> std::optional<SceneNode>
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

      // Remove from root nodes and link as child
      LinkChild(
        parent.GetHandle(), state.node_impl, cloned_handle, cloned_node_impl);

      return SceneNode(shared_from_this(), cloned_handle);
    });
}

/*!
 Traverses the hierarchy to be cloned starting from \p starting_node, in a non
 recursive way, cloning each node and properly linking it to the hierarchy under
 construction.

 This method assumes the hierarchy to be cloned is a valid hierarchy, with all
 nodes having valid handles, valid impl objects, and properly linked to their
 parent and siblings. If any of these assumptions are violated, the method will
 skip invalid nodes and continue with the traversal.

 The cloned hierarchy will have the same structure as the original, with names
 preserved exactly. The root of the cloned hierarchy will be added as a root
 node to this scene.

 __Failure Scenarios:__
 - If the \p starting_node handle is not valid (expired or invalidated).
 - If the \p starting_node is valid but its corresponding node was removed from
   its scene.
 - If any individual node cloning fails due to component issues or memory
   constraints.
 - If the resource table is full and cannot accommodate new nodes.

 @note This method will terminate the program if the resource table is full,
 which can only be remedied by increasing the initial capacity of the table.

 @param starting_node The node from which to start cloning the hierarchy, can be
 from any scene

 @return When successful, a std::pair, where the first item is the handle to the
 root node of the cloned hierarchy, and the second item is the corresponding
 SceneNodeImpl object; std::nullopt otherwise.
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
  SceneTraversal traversal(*starting_node.scene_weak_.lock());
  const auto traversal_result = traversal.TraverseHierarchy(
    const_cast<SceneNode&>(starting_node),
    [&](const VisitedNode& node, const Scene&) -> VisitResult {
      auto orig_parent_handle = node.node_impl->AsGraphNode().GetParent();
      std::string name = std::string(node.node_impl->GetName());

      try {
        // Clone the node directly from impl
        auto cloned_impl = node.node_impl->Clone();
        cloned_impl->SetName(name);
        NodeHandle cloned_handle { nodes_->Insert(std::move(*cloned_impl)),
          GetId() };
        DCHECK_F(
          cloned_handle.IsValid(), "expecting a valid handle for cloned node");

        cloned_nodes.push_back(cloned_handle);
        handle_map[node.handle] = cloned_handle;

        if (!orig_parent_handle.IsValid()) {
          // Root node of the hierarchy being cloned
          root_cloned_handle = cloned_handle;
          root_cloned_impl = &nodes_->ItemAt(cloned_handle);
          AddRootNode(cloned_handle);
          root_cloned = true;
        } else {
          // Link to already-cloned parent
          auto it = handle_map.find(orig_parent_handle);
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
          NodeHandle cloned_parent_handle = it->second;
          SceneNodeImpl* cloned_parent_impl
            = &GetNodeImplRefUnsafe(cloned_parent_handle);
          LinkChild(cloned_parent_handle, cloned_parent_impl, cloned_handle,
            &nodes_->ItemAt(cloned_handle));
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
          RemoveRootNode(root_cloned_handle);
        }
        return VisitResult::kStop;
      }
    },
    TraversalOrder::kDepthFirst); // Depth-first guarantees parent visited
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
 This method clones the entire subtree rooted at the original node, preserving
 all parent-child relationships within the cloned hierarchy. The cloned root
 will become a new root node in this scene with the specified name.

 All nodes in the original hierarchy will be cloned with their component data
 preserved, and new names will be generated based on the original names. The
 hierarchy structure is maintained exactly as in the original.

 This method will only fail if the resource table holding scene data is full,
 which can only be remedied by increasing the initial capacity of the table.
 Therefore, a failure is a fatal error that will result in the application
 terminating.

 @param original_root The root node of the hierarchy to clone (can be from any
 scene)
 @param new_root_name The name to assign to the cloned root node

 @return A new SceneNode handle representing the cloned root node in this scene
*/
auto Scene::CreateHierarchyFrom(
  const SceneNode& original_root, const std::string& new_root_name) -> SceneNode
{
  DLOG_SCOPE_F(3, "Create Hierarchy From");

  // Use the private CloneHierarchy method to do the heavy lifting
  auto clone_result = CloneHierarchy(original_root);
  if (!clone_result.has_value()) {
    // CloneHierarchy failed - this should not happen with a valid hierarchy
    // and sufficient capacity, so terminate the program as documented
    ABORT_F("Failed to clone hierarchy from node '{}' - this indicates either "
            "an invalid source hierarchy or insufficient scene capacity",
      original_root.IsValid() ? "unknown" : "invalid");
  }

  auto [cloned_root_handle, cloned_root_impl] = clone_result.value();

  // Update the root node's name as requested
  cloned_root_impl->SetName(new_root_name);

  // Return the cloned root as a SceneNode
  return SceneNode(shared_from_this(), cloned_root_handle);
}

/*!
 This method  clones the entire subtree rooted at the original node, preserving
 all parent-child relationships within the cloned hierarchy. The cloned root
 will become a child of the specified parent node.

 All nodes in the original hierarchy will be cloned with their component data
 preserved, and new names will be generated based on the original names. The
 hierarchy structure is maintained exactly as in the original.

 This method will terminate the program if the \p parent is not valid, as this
 is a programming error. It may fail if the \p parent is valid but its
 corresponding node was removed from the scene. In such case, it will return
 std::nullopt and invalidate the \p parent node.

 @param parent The parent node under which to create the cloned hierarchy
 @param original_root The root node of the hierarchy to clone (can be from any
 scene)
 @param new_root_name The name to assign to the cloned root node

 @return A new SceneNode handle representing the cloned root node, or
 std::nullopt if parent is invalid
*/
// auto Scene::CreateChildHierarchyFrom(const SceneNode& parent,
//   const SceneNode& original_root, const std::string& new_root_name)
//   -> std::optional<SceneNode>
// {
