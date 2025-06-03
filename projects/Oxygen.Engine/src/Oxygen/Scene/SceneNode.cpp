//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/TransformComponent.h>

using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeData;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::SceneNodeImpl;

constexpr auto oxygen::scene::to_string(const SceneNodeFlags& value) noexcept -> const char*
{
    switch (value) {
    case SceneNodeFlags::kVisible:
        return "Visible";
    case SceneNodeFlags::kStatic:
        return "Static";
    case SceneNodeFlags::kCastsShadows:
        return "CastsShadows";
    case SceneNodeFlags::kReceivesShadows:
        return "ReceivesShadows";
    case SceneNodeFlags::kRayCastingSelectable:
        return "RayCastingSelectable";
    case SceneNodeFlags::kIgnoreParentTransform:
        return "IgnoreParentTransform";

    case SceneNodeFlags::kCount:
        break;
    }

    return "__NotSupported__";
}

SceneNodeData::SceneNodeData(const Flags flags)
    : flags_(flags)
{
    // Iterate over the flags and log each one
    for (const auto [flag, flag_values] : flags) {
        DLOG_F(2, "flag `{}`: {}", nostd::to_string(flag), nostd::to_string(flag_values));
    }
}

SceneNodeImpl::SceneNodeImpl(const std::string& name, Flags flags)
{
    LOG_SCOPE_F(2, "SceneNodeImpl creation");
    DLOG_F(2, "name: '{}'", name);

    AddComponent<ObjectMetaData>(name);
    AddComponent<SceneNodeData>(flags);
    AddComponent<TransformComponent>();
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
    return GetComponent<SceneNodeData>().GetFlags();
}

auto SceneNodeImpl::GetFlags() noexcept -> Flags&
{
    return GetComponent<SceneNodeData>().GetFlags();
}

// Hierarchy access methods
auto SceneNodeImpl::GetParent() const noexcept -> ResourceHandle
{
    return parent_;
}

auto SceneNodeImpl::GetFirstChild() const noexcept -> ResourceHandle
{
    return first_child_;
}

auto SceneNodeImpl::GetNextSibling() const noexcept -> ResourceHandle
{
    return next_sibling_;
}

auto SceneNodeImpl::GetPrevSibling() const noexcept -> ResourceHandle
{
    return prev_sibling_;
}

// Internal hierarchy management methods
void SceneNodeImpl::SetParent(ResourceHandle parent) noexcept
{
    parent_ = parent;
}

void SceneNodeImpl::SetFirstChild(ResourceHandle child) noexcept
{
    first_child_ = child;
}

void SceneNodeImpl::SetNextSibling(ResourceHandle sibling) noexcept
{
    next_sibling_ = sibling;
}

void SceneNodeImpl::SetPrevSibling(ResourceHandle sibling) noexcept
{
    prev_sibling_ = sibling;
}

// Transform update system
void SceneNodeImpl::MarkTransformDirty() noexcept
{
    transform_dirty_ = true;
}

auto SceneNodeImpl::IsTransformDirty() const noexcept -> bool
{
    return transform_dirty_;
}

void SceneNodeImpl::ClearTransformDirty() noexcept
{
    transform_dirty_ = false;
}

void SceneNodeImpl::UpdateTransforms(const Scene& scene)
{
    if (!transform_dirty_) {
        return;
    }

    auto& transform = GetComponent<TransformComponent>();

    // If we have a parent, compose with parent's world transform, unless we ignore parent transform
    if (parent_.IsValid() && !ShouldIgnoreParentTransform()) {
        // Compose with parent's world transform
        const auto& parent_impl = scene.GetNodeImplRef(parent_);
        const auto& parent_transform = parent_impl.GetComponent<TransformComponent>();
        transform.UpdateWorldTransform(parent_transform.GetWorldMatrix());
    } else {
        // Root node or ignoring parent transform - use only local
        transform.UpdateWorldTransformAsRoot();
    }

    ClearTransformDirty();
}

SceneNode::SceneNode(const ResourceHandle& handle, std::weak_ptr<Scene> scene_weak)
    : Resource(handle)
    , scene_weak_(std::move(scene_weak))
{
}

auto SceneNode::GetObject() const noexcept -> SceneNodeImpl::optional_cref
{
    const auto scene = scene_weak_.lock();
    DCHECK_NOTNULL_F(scene, "Attempting to access SceneNode whose Scene is expired or invalid.");

    if (!IsValid()) {
        DLOG_F(WARNING, "Invalid SceneNode being used.");
        return std::nullopt;
    }

    return scene->GetNodeImpl(*this);
}

auto SceneNode::GetObject() noexcept -> SceneNodeImpl::optional_ref
{
    const auto cref = static_cast<const SceneNode*>(this)->GetObject();
    if (cref.has_value()) {
        return std::optional {
            std::ref(const_cast<SceneNodeImpl&>(cref->get()))
        };
    }
    return std::nullopt;
}

// Hierarchy navigation methods
auto SceneNode::GetParent() const noexcept -> std::optional<SceneNode>
{
    const auto scene = scene_weak_.lock();
    DCHECK_NOTNULL_F(scene, "Attempting to access SceneNode whose Scene is expired or invalid.");

    return scene->GetParent(*this);
}

auto SceneNode::GetFirstChild() const noexcept -> std::optional<SceneNode>
{
    const auto scene = scene_weak_.lock();
    DCHECK_NOTNULL_F(scene, "Attempting to access SceneNode whose Scene is expired or invalid.");

    return scene->GetFirstChild(*this);
}

auto SceneNode::GetNextSibling() const noexcept -> std::optional<SceneNode>
{
    const auto scene = scene_weak_.lock();
    DCHECK_NOTNULL_F(scene, "Attempting to access SceneNode whose Scene is expired or invalid.");

    return scene->GetNextSibling(*this);
}

auto SceneNode::GetPrevSibling() const noexcept -> std::optional<SceneNode>
{
    const auto scene = scene_weak_.lock();
    DCHECK_NOTNULL_F(scene, "Attempting to access SceneNode whose Scene is expired or invalid.");

    return scene->GetPrevSibling(*this);
}

// Hierarchy queries
auto SceneNode::HasParent() const noexcept -> bool
{
    const auto scene = scene_weak_.lock();
    DCHECK_NOTNULL_F(scene, "Attempting to access SceneNode whose Scene is expired or invalid.");

    const auto impl = scene->GetNodeImpl(*this);
    if (!impl) {
        return false; // node is invalidated now, so we consider it has no parent
    }
    return impl->get().GetParent().IsValid();
}

auto SceneNode::HasChildren() const noexcept -> bool
{
    const auto scene = scene_weak_.lock();
    DCHECK_NOTNULL_F(scene, "Attempting to access SceneNode whose Scene is expired or invalid.");

    const auto impl = scene->GetNodeImpl(*this);
    if (!impl) {
        return false; // node is invalidated now, so we consider it has no children
    }

    return impl->get().GetFirstChild().IsValid();
}

auto SceneNode::IsRoot() const noexcept -> bool
{
    const auto scene = scene_weak_.lock();
    DCHECK_NOTNULL_F(scene, "Attempting to access SceneNode whose Scene is expired or invalid.");

    const auto impl = scene->GetNodeImpl(*this);
    if (!impl) {
        return false;
    }
    return impl->get().GetParent().IsValid() == false;
}
