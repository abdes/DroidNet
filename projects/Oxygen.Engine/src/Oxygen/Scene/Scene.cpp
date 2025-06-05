//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <functional>
#include <ranges>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ResourceTable.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Scene/Scene.h>

using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeImpl;

Scene::Scene(const std::string& name, size_t initial_capacity)
    : nodes_(std::make_shared<NodeTable>(resources::kSceneNode, initial_capacity))
{
    LOG_SCOPE_F(INFO, "Scene creation");
    LOG_F(2, "name: '{}'", name);
    LOG_F(2, "initial capacity: '{}'", initial_capacity);

    AddComponent<ObjectMetaData>(name);
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

// ReSharper disable once CppMemberFunctionMayBeConst
auto Scene::CreateNodeImpl(const std::string& name) noexcept -> NodeHandle
{
    const auto handle = nodes_->Emplace(name);
    DCHECK_F(handle.IsValid(), "expecting a valid handle for a new node");
    return handle;
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto Scene::CreateNodeImpl(const std::string& name, SceneNode::Flags flags) noexcept -> NodeHandle
{
    const auto handle = nodes_->Emplace(name, flags);
    DCHECK_F(handle.IsValid(), "expecting a valid handle for a new node");
    return handle;
}

auto Scene::CreateNode(const std::string& name) -> SceneNode
{
    // This call will abort if the table is full, and that is the only possible
    // failure.
    const auto handle = CreateNodeImpl(name);
    DCHECK_F(handle.IsValid(), "expecting a valid handle for a new node");

    AddRootNode(handle);
    return SceneNode(handle, shared_from_this());
}

auto Scene::CreateNode(const std::string& name, const SceneNode::Flags flags)
    -> SceneNode
{
    // This call will abort if the table is full, and that is the only possible
    // failure.
    const auto handle = CreateNodeImpl(name, flags);
    DCHECK_F(handle.IsValid(), "expecting a valid handle for a new node");

    AddRootNode(handle);
    return SceneNode(handle, shared_from_this());
}

auto Scene::CreateChildNode(const SceneNode& parent, const std::string& name)
    -> std::optional<SceneNode>
{
    // This is a logic error, should be fixed in the code. An invalid handle
    // should not be used anymore.
    CHECK_F(parent.IsValid(), "expecting a valid parent handle");

    if (!GetNodeImpl(parent)) {
        return std::nullopt;
    }

    const auto handle = CreateNodeImpl(name);
    LinkChild(parent.GetHandle(), handle);
    return SceneNode(handle, shared_from_this());
}

auto Scene::CreateChildNode(
    const SceneNode& parent,
    const std::string& name, const SceneNode::Flags flags)
    -> std::optional<SceneNode>
{
    // This is a logic error, should be fixed in the code. An invalid handle
    // should not be used anymore.
    CHECK_F(parent.IsValid(), "expecting a valid parent handle");

    if (!GetNodeImpl(parent)) {
        return std::nullopt;
    }

    const auto handle = CreateNodeImpl(name, flags);
    LinkChild(parent.GetHandle(), handle);
    return SceneNode(handle, shared_from_this());
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
auto Scene::DestroyNode(SceneNode& node) -> bool
{
    // This is a logic error, should be fixed in the code. An invalid handle
    // should not be used anymore.
    CHECK_F(node.IsValid(), "expecting a valid node handle");

    // This is also a logic error
    CHECK_F(!node.HasChildren(), "node has children, use DestroyNodeHierarchy() instead");

    // Properly unlink the node from its parent and siblings
    UnlinkNode(node.GetHandle());

    const auto handle = node.GetHandle();
    // Remove from root nodes set only if it's actually a root node (optimization)
    if (node.IsRoot()) {
        RemoveRootNode(handle);
    }

    const auto removed = nodes_->Erase(handle);
    node.Invalidate();

    if (removed == 1) {
        return true;
    }
    DLOG_F(WARNING, "Node not found in scene: {} -> node invalidated",
        handle.ToString());
    return false;
}

auto Scene::DestroyNodeHierarchy(SceneNode& root) -> bool
{
    // This is a logic error, should be fixed in the code. An invalid handle
    // should not be used anymore.
    CHECK_F(root.IsValid(), "expecting a valid root node handle");

    if (!GetNodeImpl(root)) {
        return false;
    }

    // ReSharper disable once CppLocalVariableMayBeConst
    auto child = root.GetFirstChild();
    while (child) {
        const auto next_child = child->GetNextSibling(); // Save next sibling before destroying current child
        DestroyNodeHierarchy(*child);
        child = next_child; // Move to next sibling
    }

    // After all children are destroyed, forcibly clear the first child pointer
    if (auto root_impl_opt = GetNodeImpl(root)) {
        root_impl_opt->get().AsGraphNode().SetFirstChild({});
    }

    UnlinkNode(root.GetHandle()); // always succeeds
    const auto destroyed = DestroyNode(root); // always succeeds
    DCHECK_F(destroyed);
    return destroyed;
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

void Scene::Clear()
{
    nodes_->Clear();
    root_nodes_.clear();
}

auto Scene::GetParent(const SceneNode& node) const -> std::optional<SceneNode>
{
    // This is a logic error, should be fixed in the code. An invalid handle
    // should not be used anymore.
    CHECK_F(node.IsValid(), "expecting a valid node handle");

    // Check the node is still alive
    const auto node_impl_opt = GetNodeImpl(node);
    if (!node_impl_opt) {
        return std::nullopt;
    }

    const auto parent_handle = node_impl_opt->get().AsGraphNode().GetParent();
    if (!parent_handle.IsValid()) {
        return std::nullopt; // No parent
    }

    // Check the parent is still alive
    if (!Contains(parent_handle)) {
        DLOG_F(4, "Parent node is no longer there: {} -> child node {} invalidated",
            parent_handle.ToString(), node.GetHandle().ToString());
        node.Invalidate();
        return std::nullopt;
    }

    return SceneNode(parent_handle, std::const_pointer_cast<Scene>(this->shared_from_this()));
}

auto Scene::GetFirstChild(const SceneNode& node) const -> std::optional<SceneNode>
{
    // This is a logic error, should be fixed in the code. An invalid handle
    // should not be used anymore.
    CHECK_F(node.IsValid(), "expecting a valid node handle");

    // Check the node is still alive
    const auto node_impl_opt = GetNodeImpl(node);
    if (!node_impl_opt) {
        return std::nullopt;
    }

    const auto first_child_handle = node_impl_opt->get().AsGraphNode().GetFirstChild();
    if (!first_child_handle.IsValid()) {
        return std::nullopt; // No first child
    }

    // Check the first child is still alive. If not, this is a bug and is fatal.
    // We can't really recover from this situation, because the children list
    // linking is embedded in the node implementation, and if the first child
    // disappeared, the entire hierarchy is broken.
    CHECK_F(Contains(first_child_handle), "Child node is no longer there");

    return SceneNode(first_child_handle, std::const_pointer_cast<Scene>(this->shared_from_this()));
}

auto Scene::GetNextSibling(const SceneNode& node) const -> std::optional<SceneNode>
{
    // This is a logic error, should be fixed in the code. An invalid handle
    // should not be used anymore.
    CHECK_F(node.IsValid(), "expecting a valid node handle");

    // Check the node is still alive
    const auto node_impl_opt = GetNodeImpl(node);
    if (!node_impl_opt) {
        return std::nullopt;
    }

    const auto next_sibling_handle = node_impl_opt->get().AsGraphNode().GetNextSibling();
    if (!next_sibling_handle.IsValid()) {
        return std::nullopt; // No sibling
    }

    // Check the sibling is still alive. If not, this is a bug and is fatal. We
    // can't really recover from this situation, because the children list
    // linking is embedded in the node implementation, and if one link is
    // broken, the entire hierarchy is broken.
    CHECK_F(Contains(next_sibling_handle), "Sibling node is no longer there");

    return SceneNode(next_sibling_handle, std::const_pointer_cast<Scene>(this->shared_from_this()));
}

auto Scene::GetPrevSibling(const SceneNode& node) const -> std::optional<SceneNode>
{
    // This is a logic error, should be fixed in the code. An invalid handle
    // should not be used anymore.
    CHECK_F(node.IsValid(), "expecting a valid node handle");

    // Check the node is still alive
    const auto node_impl_opt = GetNodeImpl(node);
    if (!node_impl_opt) {
        return std::nullopt;
    }

    const auto prev_sibling_handle = node_impl_opt->get().AsGraphNode().GetPrevSibling();
    if (!prev_sibling_handle.IsValid()) {
        return std::nullopt; // No sibling
    }

    // Check the sibling is still alive. If not, this is a bug and is fatal. We
    // can't really recover from this situation, because the children list
    // linking is embedded in the node implementation, and if one link is
    // broken, the entire hierarchy is broken.
    CHECK_F(Contains(prev_sibling_handle), "Sibling node is no longer there");

    return SceneNode(prev_sibling_handle, std::const_pointer_cast<Scene>(this->shared_from_this()));
}

auto Scene::GetNodeImpl(const SceneNode& node) noexcept -> optional_ref<SceneNodeImpl>
{
    // This is a logic error, should be fixed in the code. An invalid handle
    // should not be used anymore.
    CHECK_F(node.IsValid(), "expecting a valid node handle");

    try {
        auto& impl = nodes_->ItemAt(node.GetHandle());
        return { impl };
    } catch (const std::exception& ex) {
        // If the handle is valid but the node is no longer in the scene, this
        // is a case for lazy invalidation.
        DLOG_F(4, "Node {} is no longer there -> invalidate : {}",
            node.GetHandle().ToString(), ex.what());
        node.Invalidate();
        return std::nullopt;
    }
}

auto Scene::GetNodeImpl(const SceneNode& node) const noexcept -> optional_cref<SceneNodeImpl>
{
    return const_cast<Scene*>(this)->GetNodeImpl(node);
}

auto Scene::GetNodeImplRef(const NodeHandle& handle) noexcept -> SceneNodeImpl&
{
    // This is a logic error, should be fixed in the code. An invalid handle
    // should not be used anymore.
    CHECK_F(handle.IsValid(), "expecting a valid node handle");

    try {
        return nodes_->ItemAt(handle);
    } catch (const std::exception&) {
        ABORT_F("expecting the node to exist");
    }
}

auto Scene::GetNodeImplRef(const NodeHandle& handle) const noexcept -> const SceneNodeImpl&
{
    return const_cast<Scene*>(this)->GetNodeImplRef(handle);
}

auto Scene::GetNode(const NodeHandle& handle) const noexcept -> std::optional<SceneNode>
{
    if (!Contains(handle)) {
        return std::nullopt;
    }
    return SceneNode(handle, std::const_pointer_cast<Scene>(shared_from_this()));
}

auto Scene::Contains(const SceneNode& node) const noexcept -> bool
{
    // First check if the handle exists in our node table
    if (!nodes_->Contains(node.GetHandle())) {
        return false;
    }

    // Then verify that the SceneNode's scene_weak_ actually points to this scene
    // Since Scene is a friend of SceneNode, we can access the private scene_weak_ member
    if (auto scene_shared = node.scene_weak_.lock()) {
        return scene_shared.get() == this;
    }

    // If scene_weak_ is expired, the node doesn't belong to any scene
    return false;
}

auto Scene::Contains(const NodeHandle& handle) const noexcept -> bool
{
    // Do not invalidate here, just check presence.
    return nodes_->Contains(handle);
}

auto Scene::GetNodeCount() const -> size_t
{
    return nodes_->Size();
}

auto Scene::GetNodes() const -> const NodeTable&
{
    return *nodes_;
}

auto Scene::IsEmpty() const -> bool
{
    return nodes_->IsEmpty();
}

auto Scene::GetChildrenCount(const SceneNode& parent) const -> size_t
{
    // This is a logic error, should be fixed in the code. An invalid handle
    // should not be used anymore.
    CHECK_F(parent.IsValid(), "Parent node handle is not valid for GetChildrenCount");

    const auto parent_impl_opt = GetNodeImpl(parent);
    if (!parent_impl_opt) {
        return 0;
    }

    // We do a count of children by iterating through the linked list, we
    // terminate the program if any of the children is not valid. This is
    // clearly an indication of a logic error, and should be fixed in the code.
    size_t count = 0;
    auto current_child_handle = parent_impl_opt->get().AsGraphNode().GetFirstChild();
    while (current_child_handle.IsValid()) {
        auto& child_node_impl = GetNodeImplRef(current_child_handle);
        ++count;
        current_child_handle = child_node_impl.AsGraphNode().GetNextSibling();
    }
    return count;
}

auto Scene::GetChildren(const SceneNode& parent) const -> std::vector<NodeHandle>
{
    // This is a logic error, should be fixed in the code. An invalid handle
    // should not be used anymore.
    CHECK_F(parent.IsValid(), "Parent node handle is not valid for GetChildren");

    auto parent_impl_opt = GetNodeImpl(parent);
    if (!parent_impl_opt) {
        return {};
    }

    // We terminate the program if any of the children is not valid. This is
    // clearly an indication of a logic error, and should be fixed in the code.
    std::vector<NodeHandle> children;
    auto current_child_handle = parent_impl_opt->get().AsGraphNode().GetFirstChild();
    while (current_child_handle.IsValid()) {
        const auto& child_impl = GetNodeImplRef(current_child_handle);
        children.push_back(current_child_handle);
        current_child_handle = child_impl.AsGraphNode().GetNextSibling();
    }
    return children;
}

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
        LOG_F(2, "is root: {}", !node_impl.AsGraphNode().IsRoot());
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

//! Updates transforms for all nodes in the scene in parent-before-child order.
/*!
 Performs an explicit stack-based DFS traversal from each root node, updating
 transforms only once per node.

 \note Only nodes marked dirty will have their transforms updated.
*/
void UpdateTransformsIterative(Scene& scene) noexcept
{
    LOG_SCOPE_F(2, "PASS 2 - Transforms (iterative DFS)");
    const auto& root_handles = scene.GetRootNodes();
    size_t processed_count = 0;
    for (const auto& root_handle : root_handles) {
        if (!scene.Contains(root_handle)) {
            continue;
        }
        std::vector<SceneNodeImpl*> stack;
        stack.push_back(&scene.GetNodeImplRef(root_handle));
        while (!stack.empty()) {
            SceneNodeImpl* node = stack.back();
            stack.pop_back();
            if (node->IsTransformDirty()) {
                LOG_SCOPE_F(2, "For Node");
                LOG_F(2, "name = {}", node->GetName());
                LOG_F(2, "is root: {}", !node->AsGraphNode().IsRoot());
                LOG_F(2, "transform: {}", node->IsTransformDirty() ? "dirty" : "clean");
                node->UpdateTransforms(scene);
                ++processed_count;
            }
            // Push children in reverse order for left-to-right traversal
            std::vector<SceneNodeImpl*> children;
            auto child_handle = node->AsGraphNode().GetFirstChild();
            while (child_handle.IsValid()) {
                children.push_back(&scene.GetNodeImplRef(child_handle));
                child_handle = scene.GetNodeImplRef(child_handle).AsGraphNode().GetNextSibling();
            }
            for (auto& it : std::ranges::reverse_view(children)) {
                stack.push_back(it);
            }
        }
    }
    DLOG_F(2, "{}/{} nodes updated", processed_count, scene.GetNodeCount());
}

//! Marks the transform as dirty for a node and all its descendants
//! (non-recursive).
void MarkSubtreeTransformDirty(Scene& scene, const Scene::NodeHandle& root_handle) noexcept
{
    DCHECK_F(root_handle.IsValid() && scene.Contains(root_handle),
        "expecting a valid and existing root_handle");

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
            child_handle = scene.GetNodeImplRef(child_handle).AsGraphNode().GetNextSibling();
        }
    }
    DLOG_F(2, "Marked {} nodes as transform dirty (subtree rooted at: {})",
        count, scene.GetNodeImplRef(root_handle).GetName());
}

} // namespace

void Scene::Update()
{
    LOG_SCOPE_F(2, "Scene update");
    // Pass 1: Process dirty flags for all nodes (linear scan, cache-friendly)
    ProcessDirtyFlags(*this);
    // Pass 2: Update transforms in parent-before-child order (iterative DFS)
    UpdateTransformsIterative(*this);
}

// --- Root node management API implementations ---

void Scene::AddRootNode(const NodeHandle& node)
{
    root_nodes_.insert(node);
}

void Scene::RemoveRootNode(const NodeHandle& node)
{
    root_nodes_.erase(node);
}

auto Scene::GetRootNodes() const -> std::vector<NodeHandle>
{
    std::vector<NodeHandle> valid_roots;
    valid_roots.reserve(root_nodes_.size());
    for (const auto& handle : root_nodes_) {
        // A bug that needs fixing.
        DCHECK_F(handle.IsValid(), "expecting a valid root node handle");
        // This is also a bug that needs fixing.
        DCHECK_F(nodes_->Contains(handle),
            "expecting root nodes to be in the scene or not in the root nodes set");
        valid_roots.push_back(handle);
    }
    return valid_roots;
}

void Scene::LinkChild(const NodeHandle& parent_handle, const NodeHandle& child_handle)
{
    CHECK_F(parent_handle.IsValid(), "Parent node handle is not valid for LinkChild");
    CHECK_F(child_handle.IsValid(), "Child node handle is not valid for LinkChild");

    auto& parent_impl = GetNodeImplRef(parent_handle);
    auto& child_impl = GetNodeImplRef(child_handle);

    CHECK_F(parent_handle != child_handle, "cannot link a node to itself");

    // TODO: Ensure not creating a cyclic dependency

    // If the parent already has a first child, link the new child to it
    if (const auto first_child_handle = parent_impl.AsGraphNode().GetFirstChild(); first_child_handle.IsValid()) {
        // Set the new child's next sibling to the current first child
        child_impl.AsGraphNode().SetNextSibling(first_child_handle);
        // Set the current first child's previous sibling to the new child
        auto& first_child_impl = GetNodeImplRef(first_child_handle);
        first_child_impl.AsGraphNode().SetPrevSibling(child_handle);
    }

    // Set the new child's parent to the parent node
    child_impl.AsGraphNode().SetParent(parent_handle);
    // Set the parent's first child to the new child
    parent_impl.AsGraphNode().SetFirstChild(child_handle);

    // Mark both nodes' transforms as dirty since hierarchy changed
    parent_impl.MarkTransformDirty();
    MarkSubtreeTransformDirty(*this, child_handle);

    LOG_F(3, "Linked node {} as a child of {}", child_handle.ToString(), parent_handle.ToString());
}

void Scene::UnlinkNode(const NodeHandle& node_handle) noexcept
{
    // This is a logic error, should be fixed in the code. An invalid handle
    // should not be used anymore.
    DCHECK_F(node_handle.IsValid(), "expecting a valid node_handle handle");

    auto& node_impl = GetNodeImplRef(node_handle);

    // Get parent, next sibling, and previous sibling handles
    const ResourceHandle parent_handle = node_impl.AsGraphNode().GetParent();
    const ResourceHandle next_sibling_handle = node_impl.AsGraphNode().GetNextSibling();
    const ResourceHandle prev_sibling_handle = node_impl.AsGraphNode().GetPrevSibling();

    // Update the parent's first_child pointer if this node_handle is the first child
    if (parent_handle.IsValid()) {
        auto& parent_impl = GetNodeImplRef(parent_handle);
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
        prev_sibling_impl.AsGraphNode().SetNextSibling(next_sibling_handle);
    }

    // Update next sibling's prev_sibling pointer if it exists
    if (next_sibling_handle.IsValid()) {
        auto& next_sibling_impl = GetNodeImplRef(next_sibling_handle);
        next_sibling_impl.AsGraphNode().SetPrevSibling(prev_sibling_handle);
    }

    // Reset the node_handle's parent, next sibling, and previous sibling
    node_impl.AsGraphNode().SetParent({});
    node_impl.AsGraphNode().SetNextSibling({});
    node_impl.AsGraphNode().SetPrevSibling({});

    // Mark node_handle's transform as dirty since its hierarchy relationship changed
    node_impl.MarkTransformDirty();

    LOG_F(3, "Unlinked node_handle {} from hierarchy", node_handle.ToString());
}

auto Scene::CreateNodeFrom(const SceneNode& original, const std::string& new_name)
    -> SceneNode
{
    // This is a logic error, should be fixed in the code. An invalid handle
    // should not be used anymore.
    CHECK_F(original.IsValid(), "expecting a valid original node handle");

    // Get the original node implementation
    auto original_impl_opt = original.GetObject();
    if (!original_impl_opt) {
        // Original node no longer exists, this is a fatal error for this API
        ABORT_F("Original node no longer exists in its scene");
    }

    const auto& original_impl = original_impl_opt->get();

    // Clone the original node implementation
    auto cloned_impl = original_impl.Clone();

    // Set the new name on the cloned node
    cloned_impl->SetName(new_name);

    // Add the cloned implementation to this scene's node table
    const auto handle = nodes_->Insert(std::move(*cloned_impl));
    DCHECK_F(handle.IsValid(), "expecting a valid handle for cloned node");

    // Add as root node since clones are orphaned
    AddRootNode(handle);

    return SceneNode(handle, shared_from_this());
}
