//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Scene/Detail/RenderableComponent.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/Types/RenderablePolicies.h>
#include <Oxygen/Scene/Types/Strong.h>

using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeImpl;

// =============================================================================
// SceneNode::Renderable Validator Implementations
// =============================================================================

auto SceneNode::Renderable::LogSafeCallError(const char* reason) const noexcept
  -> void
{
  try {
    // Suppress noisy logs for common soft-failure scenarios
    if (reason
      && std::string_view(reason)
        == std::string_view("RenderableComponent missing")) {
      return;
    }
    DLOG_F(ERROR, "Operation on SceneNode::Renderable {} failed: {}",
      nostd::to_string(node_->GetHandle()), reason);
  } catch (...) {
    (void)0;
  }
}

class SceneNode::Renderable::BaseRenderableValidator {
public:
  explicit BaseRenderableValidator(const Renderable& r) noexcept
    : r_(&r)
  {
    DCHECK_NOTNULL_F(r.node_, "expecting Renderable to have a non-null node");
  }

protected:
  [[nodiscard]] auto GetRenderable() const noexcept -> const Renderable&
  {
    return *r_;
  }

  [[nodiscard]] auto GetNode() const noexcept -> const SceneNode&
  {
    return *r_->node_;
  }

  [[nodiscard]] auto GetResult() noexcept -> std::optional<std::string>
  {
    return std::move(result_);
  }

  auto CheckNodeIsValid() -> bool
  {
    if (!GetNode().IsValid()) [[unlikely]] {
      result_ = fmt::format(
        "node({}) is invalid", nostd::to_string(GetNode().GetHandle()));
      return false;
    }
    result_.reset();
    return true;
  }

  auto PopulateStateWithRenderable(SafeCallState& state) -> bool
  {
    try {
      const auto impl_ref = GetRenderable().node_->GetObject();
      if (!impl_ref.has_value()) [[unlikely]] {
        result_ = fmt::format("node({}) is no longer in scene",
          nostd::to_string(GetNode().GetHandle()));
        return false;
      }
      state.node_impl = &impl_ref->get();
      if (!state.node_impl->HasComponent<detail::RenderableComponent>()) {
        result_.reset();
        // Treat as a soft failure without logging; caller returns false/empty
        return false;
      }
      state.renderable
        = &state.node_impl->GetComponent<detail::RenderableComponent>();
      result_.reset();
      return true;
    } catch (const std::exception&) {
      result_ = fmt::format("node({}) is no longer in scene",
        nostd::to_string(GetNode().GetHandle()));
      return false;
    }
  }

private:
  std::optional<std::string> result_ {};
  const Renderable* r_;
};

// Validator: node must be valid and present in scene (impl available), but
// renderable component may be absent; does NOT populate renderable when absent.
class SceneNode::Renderable::NodeInSceneValidator
  : public BaseRenderableValidator {
public:
  explicit NodeInSceneValidator(const Renderable& r) noexcept
    : BaseRenderableValidator(r)
  {
  }

  auto operator()(SafeCallState& state) -> std::optional<std::string>
  {
    state.node = const_cast<SceneNode*>(&GetNode());
    if (!CheckNodeIsValid()) {
      return GetResult();
    }
    try {
      const auto impl_ref = state.node->GetObject();
      if (!impl_ref.has_value()) {
        return std::optional<std::string> { fmt::format(
          "node({}) is no longer in scene",
          nostd::to_string(GetNode().GetHandle())) };
      }
      state.node_impl = &impl_ref->get();
      // Populate renderable if present; leave null otherwise without error
      if (state.node_impl->HasComponent<detail::RenderableComponent>()) {
        state.renderable
          = &state.node_impl->GetComponent<detail::RenderableComponent>();
      }
      return std::nullopt;
    } catch (const std::exception&) {
      return std::optional<std::string> { fmt::format(
        "node({}) is no longer in scene",
        nostd::to_string(GetNode().GetHandle())) };
    }
  }
};

// Validator: requires node in scene AND a RenderableComponent present
class SceneNode::Renderable::RequiresRenderableValidator
  : public BaseRenderableValidator {
public:
  explicit RequiresRenderableValidator(const Renderable& r) noexcept
    : BaseRenderableValidator(r)
  {
  }

  auto operator()(SafeCallState& state) -> std::optional<std::string>
  {
    state.node = const_cast<SceneNode*>(&GetNode());
    if (CheckNodeIsValid() && PopulateStateWithRenderable(state)) [[likely]] {
      return std::nullopt;
    }
    // Ensure we report missing component as error to force failure
    if (!state.renderable && state.node_impl) {
      return std::optional<std::string> { "RenderableComponent missing" };
    }
    return GetResult();
  }
};

auto SceneNode::Renderable::NodeInScene() const -> NodeInSceneValidator
{
  return NodeInSceneValidator(*this);
}

auto SceneNode::Renderable::RequiresRenderable() const
  -> RequiresRenderableValidator
{
  return RequiresRenderableValidator(*this);
}

// =============================================================================
// SceneNode::Renderable Implementations
// =============================================================================

auto SceneNode::Renderable::SetGeometry(GeometryAssetPtr geometry) -> void
{
  if (!geometry) {
    LOG_F(ERROR, "Cannot set a null geometry. SceneNode: {}",
      nostd::to_string(*node_));
    return;
  }
  // Needs node in scene; will attach component if missing.
  SafeCall(NodeInScene(), [&](SafeCallState& state) noexcept {
    DCHECK_EQ_F(state.node, node_);
    DCHECK_NOTNULL_F(state.node_impl);
    if (!state.renderable) {
      // No component yet: attach a new one with the geometry
      state.node_impl->AddComponent<oxygen::scene::detail::RenderableComponent>(
        std::move(geometry));
      // Refresh pointer for any subsequent operations this frame
      state.renderable
        = &state.node_impl
             ->GetComponent<oxygen::scene::detail::RenderableComponent>();
      return;
    }
    state.renderable->SetGeometry(std::move(geometry));
    return;
  });
}

auto SceneNode::Renderable::GetGeometry() const noexcept
  -> std::shared_ptr<const data::GeometryAsset>
{
  // If component is missing, return empty; do not treat as error.
  return SafeCall(NodeInScene(), [&](SafeCallState& state) noexcept {
    return state.renderable ? state.renderable->GetGeometry()
                            : GeometryAssetPtr {};
  });
}

auto SceneNode::Renderable::Detach() noexcept -> bool
{
  return SafeCall(NodeInScene(), [&](SafeCallState& state) noexcept {
    DCHECK_EQ_F(state.node, node_);
    DCHECK_NOTNULL_F(state.node_impl);
    if (!state.renderable) {
      return false;
    }
    state.node_impl
      ->RemoveComponent<oxygen::scene::detail::RenderableComponent>();
    return true;
  });
}

auto SceneNode::Renderable::HasGeometry() const noexcept -> bool
{
  return SafeCall(NodeInScene(),
    [&](SafeCallState& state) noexcept { return state.renderable != nullptr; });
}

auto SceneNode::Renderable::UsesFixedPolicy() const noexcept -> bool
{
  return SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    return state.renderable->UsesFixedPolicy();
  });
}

auto SceneNode::Renderable::UsesDistancePolicy() const noexcept -> bool
{
  return SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    return state.renderable->UsesDistancePolicy();
  });
}

auto SceneNode::Renderable::UsesScreenSpaceErrorPolicy() const noexcept -> bool
{
  return SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    return state.renderable->UsesScreenSpaceErrorPolicy();
  });
}

auto SceneNode::Renderable::SetLodPolicy(FixedPolicy p) -> void
{
  SafeCall(RequiresRenderable(),
    [&](SafeCallState& state) noexcept { state.renderable->SetLodPolicy(p); });
}

auto SceneNode::Renderable::SetLodPolicy(DistancePolicy p) -> void
{
  SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    state.renderable->SetLodPolicy(std::move(p));
  });
}

auto SceneNode::Renderable::SetLodPolicy(ScreenSpaceErrorPolicy p) -> void
{
  SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    state.renderable->SetLodPolicy(std::move(p));
  });
}

auto SceneNode::Renderable::SelectActiveMesh(
  NormalizedDistance d) const noexcept -> void
{
  SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    state.renderable->SelectActiveMesh(d);
  });
}

auto SceneNode::Renderable::SelectActiveMesh(ScreenSpaceError e) const noexcept
  -> void
{
  SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    state.renderable->SelectActiveMesh(e);
  });
}

auto SceneNode::Renderable::GetActiveMesh() const noexcept
  -> std::optional<oxygen::scene::ActiveMesh>
{
  return SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    return state.renderable->GetActiveMesh();
  });
}

auto SceneNode::Renderable::GetActiveLodIndex() const noexcept
  -> std::optional<std::size_t>
{
  return SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    return state.renderable->GetActiveLodIndex();
  });
}

auto SceneNode::Renderable::EffectiveLodCount() const noexcept -> std::size_t
{
  return SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    return state.renderable->EffectiveLodCount();
  });
}

auto SceneNode::Renderable::GetWorldBoundingSphere() const noexcept -> glm::vec4
{
  return SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    return state.renderable->GetWorldBoundingSphere();
  });
}

auto SceneNode::Renderable::OnWorldTransformUpdated(const Mat4& world) -> void
{
  SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    state.renderable->OnWorldTransformUpdated(world);
  });
}

auto SceneNode::Renderable::GetWorldSubMeshBoundingBox(
  std::size_t submesh_index) const noexcept
  -> std::optional<std::pair<Vec3, Vec3>>
{
  return SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    return state.renderable->GetWorldSubMeshBoundingBox(submesh_index);
  });
}

auto SceneNode::Renderable::IsSubmeshVisible(
  std::size_t lod, std::size_t submesh_index) const noexcept -> bool
{
  return SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    return state.renderable->IsSubmeshVisible(lod, submesh_index);
  });
}

auto SceneNode::Renderable::SetSubmeshVisible(
  std::size_t lod, std::size_t submesh_index, bool visible) noexcept -> void
{
  SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    state.renderable->SetSubmeshVisible(lod, submesh_index, visible);
  });
}

auto SceneNode::Renderable::SetAllSubmeshesVisible(bool visible) noexcept
  -> void
{
  SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    state.renderable->SetAllSubmeshesVisible(visible);
  });
}

auto SceneNode::Renderable::SetMaterialOverride(std::size_t lod,
  std::size_t submesh_index, MaterialAssetPtr material) noexcept -> void
{
  SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    state.renderable->SetMaterialOverride(
      lod, submesh_index, std::move(material));
  });
}

auto SceneNode::Renderable::ClearMaterialOverride(
  std::size_t lod, std::size_t submesh_index) noexcept -> void
{
  SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    state.renderable->ClearMaterialOverride(lod, submesh_index);
  });
}

auto SceneNode::Renderable::ResolveSubmeshMaterial(
  std::size_t lod, std::size_t submesh_index) const noexcept -> MaterialAssetPtr
{
  return SafeCall(RequiresRenderable(), [&](SafeCallState& state) noexcept {
    return state.renderable->ResolveSubmeshMaterial(lod, submesh_index);
  });
}
