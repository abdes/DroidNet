//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNodeImpl.h>

using oxygen::scene::SceneNodeData;
using oxygen::scene::SceneNodeImpl;

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

void SceneNodeImpl::SetName(std::string_view name) noexcept
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

void SceneNodeImpl::MarkTransformDirty() noexcept
{
    transform_dirty_ = true;
}

void SceneNodeImpl::ClearTransformDirty() noexcept
{
    transform_dirty_ = false;
}

auto SceneNodeImpl::IsTransformDirty() const noexcept -> bool
{
    return transform_dirty_;
}

void SceneNodeImpl::UpdateTransforms(const Scene& scene)
{
    if (!transform_dirty_) {
        return;
    }
    auto& transform = GetComponent<TransformComponent>();
    if (parent_.IsValid() && !ShouldIgnoreParentTransform()) {
        const auto& parent_impl = scene.GetNodeImplRef(parent_);
        const auto& parent_transform = parent_impl.GetComponent<TransformComponent>();
        transform.UpdateWorldTransform(parent_transform.GetWorldMatrix());
    } else {
        transform.UpdateWorldTransformAsRoot();
    }
    ClearTransformDirty();
}
