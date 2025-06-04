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

// --- SceneNode SafeCall wrappers and validator ---

namespace oxygen::scene {

auto SceneNode::ValidateForSafeCall() const noexcept -> std::optional<std::string>
{
    if (scene_weak_.expired()) {
        return "Scene is expired";
    }
    if (!IsValid()) {
        return "SceneNode is invalid";
    }
    return std::nullopt;
}

void SceneNode::LogSafeCallError(const char* reason) const noexcept
{
    try {
        DLOG_F(ERROR, "Operation on SceneNode {} failed: {}",
            nostd::to_string(GetHandle()), reason);
    } catch (...) {
        // If logging fails, we can do nothing about it
        (void)0;
    }
}

template <typename Func>
auto SceneNode::SafeCall(Func&& func) const noexcept
{
    return oxygen::SafeCall(
        *this,
        [this](const SceneNode&) { return this->ValidateForSafeCall(); },
        std::forward<Func>(func));
}

template <typename Func>
auto SceneNode::SafeCall(Func&& func) noexcept
{
    return oxygen::SafeCall(
        *this,
        [this](SceneNode&) { return this->ValidateForSafeCall(); },
        std::forward<Func>(func));
}

} // namespace oxygen::scene

auto SceneNode::GetObject() const noexcept -> OptionalConstRefToImpl
{
    auto result = SafeCall([&](const SceneNode& n) -> OptionalConstRefToImpl {
        const auto scene = n.scene_weak_.lock();
        DCHECK_NOTNULL_F(scene, "Attempting to access SceneNode whose Scene is expired or invalid.");
        return scene->GetNodeImpl(n);
    });
    if (result) {
        return *result;
    }
    return std::nullopt;
}

auto SceneNode::GetObject() noexcept -> OptionalRefToImpl
{
    auto result = SafeCall([&](const SceneNode& n) -> OptionalRefToImpl {
        const auto cref = n.GetObject();
        if (cref.has_value()) {
            return std::optional {
                std::ref(const_cast<SceneNodeImpl&>(cref->get())) // NOLINT(*-pro-type-const-cast)
            };
        }
        return std::nullopt;
    });
    if (result) {
        return *result;
    }
    return std::nullopt;
}

auto SceneNode::GetFlags() const noexcept -> OptionalConstRefToFlags
{
    auto result = SafeCall([&](const SceneNode& n) -> OptionalConstRefToFlags {
        if (const auto impl_opt = n.GetObject()) {
            return impl_opt->get().GetFlags();
        }
        return std::nullopt;
    });
    if (result) {
        return *result;
    }
    return std::nullopt;
}

auto SceneNode::GetFlags() noexcept -> OptionalRefToFlags
{
    auto result = SafeCall([&](SceneNode& n) -> OptionalRefToFlags {
        if (const auto impl_opt = n.GetObject()) {
            return impl_opt->get().GetFlags();
        }
        return std::nullopt;
    });
    if (result) {
        return *result;
    }
    return std::nullopt;
}

// Hierarchy navigation methods
auto SceneNode::GetParent() const noexcept -> std::optional<SceneNode>
{
    auto result = SafeCall([&](const SceneNode& n) -> std::optional<SceneNode> {
        const auto scene = n.scene_weak_.lock();
        DCHECK_NOTNULL_F(scene, "Attempting to access SceneNode whose Scene is expired or invalid.");
        return scene->GetParent(n);
    });
    if (result) {
        return *result;
    }
    return std::nullopt;
}

auto SceneNode::GetFirstChild() const noexcept -> std::optional<SceneNode>
{
    auto result = SafeCall([&](const SceneNode& n) -> std::optional<SceneNode> {
        const auto scene = n.scene_weak_.lock();
        DCHECK_NOTNULL_F(scene, "Attempting to access SceneNode whose Scene is expired or invalid.");
        return scene->GetFirstChild(n);
    });
    if (result) {
        return *result;
    }
    return std::nullopt;
}

auto SceneNode::GetNextSibling() const noexcept -> std::optional<SceneNode>
{
    auto result = SafeCall([&](const SceneNode& n) -> std::optional<SceneNode> {
        const auto scene = n.scene_weak_.lock();
        if (!scene) {
            return std::nullopt;
        }
        return scene->GetNextSibling(n);
    });
    if (result) {
        return *result;
    }
    return std::nullopt;
}

auto SceneNode::GetPrevSibling() const noexcept -> std::optional<SceneNode>
{
    auto result = SafeCall([&](const SceneNode& n) -> std::optional<SceneNode> {
        const auto scene = n.scene_weak_.lock();
        if (!scene) {
            return std::nullopt;
        }
        return scene->GetPrevSibling(n);
    });
    if (result) {
        return *result;
    }
    return std::nullopt;
}

auto SceneNode::HasParent() const noexcept -> bool
{
    auto result = SafeCall([&](const SceneNode& n) -> bool {
        const auto scene = n.scene_weak_.lock();
        if (!scene) {
            return false;
        }
        const auto impl = scene->GetNodeImpl(n);
        if (!impl) {
            return false;
        }
        return impl->get().GetParent().IsValid();
    });
    return result.value_or(false);
}

auto SceneNode::HasChildren() const noexcept -> bool
{
    auto result = SafeCall([&](const SceneNode& n) -> bool {
        const auto scene = n.scene_weak_.lock();
        if (!scene) {
            return false;
        }
        const auto impl = scene->GetNodeImpl(n);
        if (!impl) {
            return false;
        }
        return impl->get().GetFirstChild().IsValid();
    });
    return result.value_or(false);
}

auto SceneNode::IsRoot() const noexcept -> bool
{
    auto result = SafeCall([&](const SceneNode& n) -> bool {
        const auto scene = n.scene_weak_.lock();
        if (!scene) {
            return false;
        }
        const auto impl = scene->GetNodeImpl(n);
        if (!impl) {
            return false;
        }
        return !impl->get().GetParent().IsValid();
    });
    return result.value_or(false);
}

auto SceneNode::GetTransform() noexcept -> Transform
{
    return Transform(*this);
}

auto SceneNode::GetTransform() const noexcept -> Transform
{
    return Transform(const_cast<SceneNode&>(*this)); // NOLINT(*-pro-type-const-cast)
}

auto SceneNode::Transform::LookAt(const Vec3& target_position, const Vec3& up_direction) noexcept -> bool
{
    return SafeCall([&](const State& state) {
        // Get current world position from cached data
        const auto world_pos = GetWorldPosition();
        if (!world_pos) {
            return false;
        }

        // Compute look-at rotation in world space
        const auto forward = glm::normalize(target_position - *world_pos);
        const auto right = glm::normalize(glm::cross(forward, up_direction));
        const auto up = glm::cross(right, forward);

        // Create rotation matrix (note: -forward because we use -Z as forward)
        Mat4 look_matrix(1.0F);
        look_matrix[0] = glm::vec4(right, 0);
        look_matrix[1] = glm::vec4(up, 0);
        look_matrix[2] = glm::vec4(-forward, 0);

        // Convert to quaternion and set as local rotation
        const auto look_rotation = glm::quat_cast(look_matrix);

        state.transform_component->SetLocalRotation(look_rotation);
        state.node_impl->MarkTransformDirty();
        return true;
    }).has_value();
}

namespace {
// Template implementation that works with both State types
template <typename StateT>
auto ValidateAndPopulateState(SceneNode* node, StateT& state) noexcept -> std::optional<std::string>
{
    using oxygen::scene::TransformComponent;

    state.node = node;
    if (!state.node->IsValid()) {
        return "node is invalid";
    }
    auto node_impl_opt = state.node->GetObject();
    if (!node_impl_opt) {
        return "node is expired";
    }
    state.node_impl = &(node_impl_opt->get());
#if !defined(NDEBUG)
    if (!state.node_impl->template HasComponent<TransformComponent>()) {
        return "missing TransformComponent";
    }
#endif // NDEBUG
    state.transform_component = &(state.node_impl->template GetComponent<TransformComponent>());
    return std::nullopt;
}
} // namespace

// Thin wrapper overloads that delegate to the common implementation
auto SceneNode::Transform::ValidateForSafeCall(State& state) const noexcept
    -> std::optional<std::string>
{
    return ValidateAndPopulateState(node_, state);
}

auto SceneNode::Transform::ValidateForSafeCall(ConstState& state) const noexcept
    -> std::optional<std::string>
{
    return ValidateAndPopulateState(node_, state);
}

void SceneNode::Transform::LogSafeCallError(const char* reason) const noexcept
{
    try {
        DLOG_F(ERROR, "Operation on SceneNode::Transform {} failed: {}",
            nostd::to_string(node_->GetHandle()), reason);
    } catch (...) {
        // If logging fails, we can do nothing about it
        (void)0;
    }
}
