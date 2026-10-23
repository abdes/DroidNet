//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Data/SurfaceId.h>
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
    if ((reason != nullptr)
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
      const auto impl_ref = GetRenderable().node_->GetImpl();
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
  std::optional<std::string> result_;
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
    // NOLINTNEXTLINE(*-pro-type-const-cast)
    state.node = const_cast<SceneNode*>(&GetNode());
    if (!CheckNodeIsValid()) {
      return GetResult();
    }
    try {
      const auto impl_ref = state.node->GetImpl();
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
    // NOLINTNEXTLINE(*-pro-type-const-cast)
    state.node = const_cast<SceneNode*>(&GetNode());
    if (CheckNodeIsValid() && PopulateStateWithRenderable(state)) [[likely]] {
      return std::nullopt;
    }
    // Ensure we report missing component as error to force failure
    if ((state.renderable == nullptr) && (state.node_impl != nullptr)) {
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
  SafeCall(NodeInScene(), [&](SafeCallState& state) noexcept -> void {
    DCHECK_EQ_F(state.node, node_);
    DCHECK_NOTNULL_F(state.node_impl);
    if (!state.renderable) {
      // No component yet: attach a new one with the geometry
      state.node_impl->AddComponent<detail::RenderableComponent>(
        std::move(geometry));
      // Refresh pointer for any subsequent operations this frame
      state.renderable
        = &state.node_impl->GetComponent<detail::RenderableComponent>();
      return;
    }
    state.renderable->SetGeometry(std::move(geometry));
  });
}

auto SceneNode::Renderable::GetGeometry() const noexcept
  -> std::shared_ptr<const data::GeometryAsset>
{
  // If component is missing, return empty; do not treat as error.
  return SafeCall(NodeInScene(),
    [&](const SafeCallState& state) noexcept
      -> std::shared_ptr<const data::GeometryAsset> {
      return state.renderable ? state.renderable->GetGeometry()
                              : GeometryAssetPtr {};
    });
}

auto SceneNode::Renderable::Detach() noexcept -> bool
{
  return SafeCall(
    NodeInScene(), [&](const SafeCallState& state) noexcept -> bool {
      DCHECK_EQ_F(state.node, node_);
      DCHECK_NOTNULL_F(state.node_impl);
      if (!state.renderable) {
        return false;
      }
      state.node_impl->RemoveComponent<detail::RenderableComponent>();
      return true;
    });
}

auto SceneNode::Renderable::HasGeometry() const noexcept -> bool
{
  return SafeCall(
    NodeInScene(), [&](const SafeCallState& state) noexcept -> bool {
      return state.renderable != nullptr;
    });
}

auto SceneNode::Renderable::UsesFixedPolicy() const noexcept -> bool
{
  return SafeCall(
    RequiresRenderable(), [&](const SafeCallState& state) noexcept -> bool {
      return state.renderable->UsesFixedPolicy();
    });
}

auto SceneNode::Renderable::UsesDistancePolicy() const noexcept -> bool
{
  return SafeCall(
    RequiresRenderable(), [&](const SafeCallState& state) noexcept -> bool {
      return state.renderable->UsesDistancePolicy();
    });
}

auto SceneNode::Renderable::UsesScreenSpaceErrorPolicy() const noexcept -> bool
{
  return SafeCall(
    RequiresRenderable(), [&](const SafeCallState& state) noexcept -> bool {
      return state.renderable->UsesScreenSpaceErrorPolicy();
    });
}

auto SceneNode::Renderable::GetLodPolicy() const noexcept
  -> std::optional<LodPolicy>
{
  return SafeCall(RequiresRenderable(),
    [&](const SafeCallState& state) noexcept -> std::optional<LodPolicy> {
      try {
        return state.renderable->GetLodPolicy();
      } catch (const std::exception&) {
        return std::nullopt;
      }
    });
}

auto SceneNode::Renderable::SetLodPolicyOverride(LodPolicy policy) -> void
{
  SafeCall(
    RequiresRenderable(), [&](const SafeCallState& state) noexcept -> void {
      state.renderable->SetLodPolicyOverride(std::move(policy));
    });
}

auto SceneNode::Renderable::ClearLodPolicyOverride() -> void
{
  SafeCall(
    RequiresRenderable(), [&](const SafeCallState& state) noexcept -> void {
      state.renderable->ClearLodPolicyOverride();
    });
}

auto SceneNode::Renderable::HasLodPolicyOverride() const noexcept -> bool
{
  return SafeCall(
    RequiresRenderable(), [&](const SafeCallState& state) noexcept -> bool {
      return state.renderable->HasLodPolicyOverride();
    });
}

auto SceneNode::Renderable::SelectActiveMesh(
  const NormalizedDistance d) const noexcept -> void
{
  SafeCall(
    RequiresRenderable(), [&](const SafeCallState& state) noexcept -> void {
      state.renderable->SelectActiveMesh(d);
    });
}

auto SceneNode::Renderable::SelectActiveMesh(
  const ScreenSpaceError e) const noexcept -> void
{
  SafeCall(
    RequiresRenderable(), [&](const SafeCallState& state) noexcept -> void {
      state.renderable->SelectActiveMesh(e);
    });
}

auto SceneNode::Renderable::GetActiveMesh() const noexcept
  -> std::optional<ActiveMesh>
{
  return SafeCall(RequiresRenderable(),
    [&](const SafeCallState& state) noexcept -> std::optional<ActiveMesh> {
      return state.renderable->GetActiveMesh();
    });
}

auto SceneNode::Renderable::GetActiveLodIndex() const noexcept
  -> std::optional<data::LodIndex>
{
  return SafeCall(RequiresRenderable(),
    [&](const SafeCallState& state) noexcept -> std::optional<data::LodIndex> {
      return state.renderable->GetActiveLodIndex();
    });
}

auto SceneNode::Renderable::EffectiveLodCount() const noexcept -> std::size_t
{
  return SafeCall(RequiresRenderable(),
    [&](const SafeCallState& state) noexcept -> std::size_t {
      return state.renderable->EffectiveLodCount();
    });
}

auto SceneNode::Renderable::GetWorldBoundingSphere() const noexcept -> Vec4
{
  return SafeCall(
    RequiresRenderable(), [&](const SafeCallState& state) noexcept -> Vec4 {
      return state.renderable->GetWorldBoundingSphere();
    });
}

auto SceneNode::Renderable::OnWorldTransformUpdated(const Mat4& world) -> void
{
  SafeCall(
    RequiresRenderable(), [&](const SafeCallState& state) noexcept -> void {
      state.renderable->OnWorldTransformUpdated(world);
    });
}

auto SceneNode::Renderable::GetWorldSubMeshBoundingBox(
  const data::SubmeshIndex submesh) const noexcept
  -> std::optional<std::pair<Vec3, Vec3>>
{
  return SafeCall(RequiresRenderable(),
    [&](const SafeCallState& state) noexcept
      -> std::optional<std::pair<Vec3, Vec3>> {
      return state.renderable->GetWorldSubMeshBoundingBox(submesh);
    });
}

auto SceneNode::Renderable::SetMaterialOverride(
  const data::SurfaceId surface, MaterialAssetPtr material) noexcept -> bool
{
  return SafeCall(
    RequiresRenderable(), [&](const SafeCallState& state) noexcept -> bool {
      return state.renderable->SetMaterialOverride(
        surface, std::move(material));
    });
}

auto SceneNode::Renderable::ClearMaterialOverride(
  const data::SurfaceId surface) noexcept -> bool
{
  return SetMaterialOverride(surface, nullptr);
}

auto SceneNode::Renderable::SetVisibilityOverride(
  const data::SurfaceId surface, const bool visible) noexcept -> bool
{
  return SafeCall(
    RequiresRenderable(), [&](const SafeCallState& state) noexcept -> bool {
      return state.renderable->SetVisibilityOverride(surface, visible);
    });
}

auto SceneNode::Renderable::ClearVisibilityOverride(
  const data::SurfaceId surface) noexcept -> bool
{
  return SafeCall(
    RequiresRenderable(), [&](const SafeCallState& state) noexcept -> bool {
      return state.renderable->ClearVisibilityOverride(surface);
    });
}

auto SceneNode::Renderable::SetCastsShadowsOverride(
  const data::SurfaceId surface, const bool casts_shadows) noexcept -> bool
{
  return SafeCall(
    RequiresRenderable(), [&](const SafeCallState& state) noexcept -> bool {
      return state.renderable->SetCastsShadowsOverride(surface, casts_shadows);
    });
}

auto SceneNode::Renderable::ClearCastsShadowsOverride(
  const data::SurfaceId surface) noexcept -> bool
{
  return SafeCall(
    RequiresRenderable(), [&](const SafeCallState& state) noexcept -> bool {
      return state.renderable->ClearCastsShadowsOverride(surface);
    });
}

auto SceneNode::Renderable::IsSubmeshVisible(const data::LodIndex lod,
  const data::SubmeshIndex submesh) const noexcept -> bool
{
  return SafeCall(
    RequiresRenderable(), [&](const SafeCallState& state) noexcept -> bool {
      return state.renderable->IsSubmeshVisible(lod, submesh);
    });
}

auto SceneNode::Renderable::SubmeshCastsShadows(const data::LodIndex lod,
  const data::SubmeshIndex submesh) const noexcept -> bool
{
  return SafeCall(
    RequiresRenderable(), [&](const SafeCallState& state) noexcept -> bool {
      return state.renderable->SubmeshCastsShadows(lod, submesh);
    });
}

auto SceneNode::Renderable::ResolveSubmeshMaterial(const data::LodIndex lod,
  const data::SubmeshIndex submesh) const noexcept -> MaterialAssetPtr
{
  return SafeCall(RequiresRenderable(),
    [&](const SafeCallState& state) noexcept
      -> std::shared_ptr<const data::MaterialAsset> {
      return state.renderable->ResolveSubmeshMaterial(lod, submesh);
    });
}
