//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/SceneTraversal.h>

namespace oxygen::scene {

namespace {

  auto ResolveWorldRotation(const Scene& scene_ref, const SceneNodeImpl& node)
    -> glm::quat
  {
    const auto& transform = node.GetComponent<detail::TransformComponent>();
    const auto ignore_parent = node.GetFlags().GetEffectiveValue(
      SceneNodeFlags::kIgnoreParentTransform);
    auto rotation = transform.GetLocalRotation();
    if (const auto parent = node.AsGraphNode().GetParent();
      parent.IsValid() && !ignore_parent) {
      rotation
        = ResolveWorldRotation(scene_ref, scene_ref.GetNodeImplRef(parent))
        * rotation;
    }
    return rotation;
  }

  auto ComputeEmittedRayDirectionWs(
    const Scene& scene_ref, const SceneNodeImpl& node) -> glm::vec3
  {
    const auto direction
      = ResolveWorldRotation(scene_ref, node) * space::move::Forward;
    const auto length_sq = glm::dot(direction, direction);
    if (length_sq <= math::EpsilonDirection) {
      return space::move::Forward;
    }
    return glm::normalize(direction);
  }

  auto CollectDirectionalLights(const Scene& scene)
    -> std::vector<ResolvedDirectionalLightView>
  {
    auto resolved = std::vector<ResolvedDirectionalLightView> {};
    auto traversal_index = std::uint32_t { 0U };

    const auto visitor
      = [&scene, &resolved, &traversal_index](
          const ConstVisitedNode& visited, const bool dry_run) -> VisitResult {
      static_cast<void>(dry_run);

      const auto& node = *visited.node_impl;
      if (!node.HasComponent<detail::TransformComponent>()
        || !node.HasComponent<DirectionalLight>()) {
        return VisitResult::kContinue;
      }

      const auto& light = node.GetComponent<DirectionalLight>();
      // TODO(post-v0.1, EV01-LIGHT-SKY-ONLY): Authored sky-only contribution
      // needs explicit destination semantics; affects_world remains the master
      // gate. Scope:
      // design/vortex/milestones/ED-M08/deferred-capabilities.md#ev01-light-sky-only
      if (!light.Common().affects_world) {
        return VisitResult::kContinue;
      }

      const auto emitted_ray_direction_ws
        = ComputeEmittedRayDirectionWs(scene, node);
      resolved.emplace_back(visited.handle, node, light,
        emitted_ray_direction_ws, -emitted_ray_direction_ws, traversal_index++);
      return VisitResult::kContinue;
    };

    static_cast<void>(scene.Traverse().Traverse(
      visitor, TraversalOrder::kPreOrder, VisibleFilter {}));
    return resolved;
  }

} // namespace

ResolvedDirectionalLightView::ResolvedDirectionalLightView(
  const scene::NodeHandle node_handle, const SceneNodeImpl& node,
  const DirectionalLight& light, const glm::vec3& emitted_ray_direction_ws,
  const glm::vec3& direction_to_light_ws,
  const std::uint32_t traversal_index) noexcept
  : node_handle_(node_handle)
  , node_(std::cref(node))
  , light_(std::cref(light))
  , emitted_ray_direction_ws_(emitted_ray_direction_ws)
  , direction_to_light_ws_(direction_to_light_ws)
  , traversal_index_(traversal_index)
{
}

DirectionalLightResolver::~DirectionalLightResolver() { Unbind(); }

auto DirectionalLightResolver::Bind(const observer_ptr<Scene> scene) -> void
{
  if (scene_ == scene) {
    return;
  }

  Unbind();
  scene_ = scene;
  if (scene_ != nullptr) {
    static_cast<void>(
      scene_->RegisterObserver(observer_ptr<ISceneObserver> { this },
        SceneMutationMask::kLightChanged | SceneMutationMask::kTransformChanged
          | SceneMutationMask::kNodeDestroyed));
  }
  MarkDirty();
}

auto DirectionalLightResolver::Unbind() noexcept -> void
{
  if (scene_ != nullptr) {
    static_cast<void>(
      scene_->UnregisterObserver(observer_ptr<ISceneObserver> { this }));
  }
  scene_ = nullptr;
  MarkDirty();
}

auto DirectionalLightResolver::IsValid() const -> bool
{
  if (dirty_) {
    RebuildIfDirty();
  }
  return valid_;
}

auto DirectionalLightResolver::Validate() const -> void
{
  if (dirty_) {
    RebuildIfDirty();
  }
  if (!valid_) {
    throw DirectionalLightContractError(
      validation_error_.value_or("invalid directional light contract"));
  }
}

auto DirectionalLightResolver::ResolvePrimarySun() const
  -> std::optional<ResolvedDirectionalLightView>
{
  Validate();
  return atmosphere_lights_.slots[0];
}

auto DirectionalLightResolver::ResolveSecondarySun() const
  -> std::optional<ResolvedDirectionalLightView>
{
  Validate();
  return atmosphere_lights_.slots[1];
}

auto DirectionalLightResolver::ResolveMoon() const
  -> std::optional<ResolvedDirectionalLightView>
{
  return ResolveSecondarySun();
}

auto DirectionalLightResolver::ResolveAtmosphereLights() const
  -> const ResolvedAtmosphereDirectionalLights&
{
  Validate();
  return atmosphere_lights_;
}

auto DirectionalLightResolver::ResolveDirectionalLights() const
  -> std::span<const ResolvedDirectionalLightView>
{
  Validate();
  return std::span<const ResolvedDirectionalLightView>(directional_lights_);
}

auto DirectionalLightResolver::OnLightChanged(
  const NodeHandle& /*node_handle*/) noexcept -> void
{
  MarkDirty();
}

auto DirectionalLightResolver::OnTransformChanged(
  const NodeHandle& /*node_handle*/) noexcept -> void
{
  MarkDirty();
}

auto DirectionalLightResolver::OnNodeDestroyed(
  const NodeHandle& /*node_handle*/) noexcept -> void
{
  MarkDirty();
}

auto DirectionalLightResolver::RebuildIfDirty() const -> void
{
  if (!dirty_) {
    return;
  }

  directional_lights_.clear();
  atmosphere_lights_ = ResolvedAtmosphereDirectionalLights {};
  validation_error_.reset();
  valid_ = true;

  if (scene_ == nullptr) {
    dirty_ = false;
    return;
  }

  directional_lights_ = CollectDirectionalLights(*scene_);
  validation_error_ = ValidationErrorMessage();
  valid_ = !validation_error_.has_value();
  if (!valid_) {
    LOG_F(ERROR, "{}", *validation_error_);
    dirty_ = false;
    return;
  }

  atmosphere_lights_ = ResolveCanonicalAtmosphereLights();
  dirty_ = false;
}

auto DirectionalLightResolver::MarkDirty() noexcept -> void { dirty_ = true; }

auto DirectionalLightResolver::ResolveCanonicalAtmosphereLights() const
  -> ResolvedAtmosphereDirectionalLights
{
  auto result = ResolvedAtmosphereDirectionalLights {};

  for (const auto& entry : directional_lights_) {
    const auto slot = entry.Light().GetAtmosphereLightSlot();
    if (slot == AtmosphereLightSlot::kNone)
      continue;
    const auto index = slot == AtmosphereLightSlot::kPrimary ? 0U : 1U;
    result.slots[index] = entry;
    result.explicit_slot_claims[index] = true;
  }

  return result;
}

auto DirectionalLightResolver::ValidationErrorMessage() const
  -> std::optional<std::string>
{
  // Ownership includes stored inactive/hidden lights, independently of
  // rendering.
  std::array<std::string, 2> owners;
  std::optional<std::string> error;
  const auto& scene = std::as_const(*scene_);
  static_cast<void>(scene.Traverse().Traverse(
    [&](const ConstVisitedNode& visited, bool) -> VisitResult {
      if (!visited.node_impl->HasComponent<DirectionalLight>())
        return VisitResult::kContinue;
      const auto slot = visited.node_impl->GetComponent<DirectionalLight>()
                          .GetAtmosphereLightSlot();
      if (slot == AtmosphereLightSlot::kNone)
        return VisitResult::kContinue;
      if (slot > AtmosphereLightSlot::kSecondary) {
        error = "Unknown atmosphere light slot";
        return VisitResult::kStop;
      }
      const auto index = slot == AtmosphereLightSlot::kPrimary ? 0U : 1U;
      const auto name = std::string(visited.node_impl->GetName());
      if (!owners[index].empty()) {
        error = "Atmosphere slot conflict between '" + owners[index] + "' and '"
          + name + "'";
        return VisitResult::kStop;
      }
      owners[index] = name.empty() ? "<unnamed>" : name;
      return VisitResult::kContinue;
    }));
  if (error)
    return error;

  return std::nullopt;
}

} // namespace oxygen::scene
