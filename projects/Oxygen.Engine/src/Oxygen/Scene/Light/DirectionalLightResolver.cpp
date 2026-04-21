//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Scene/Light/DirectionalLightResolver.h>

#include <algorithm>
#include <string>
#include <vector>

#include <glm/geometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/SceneTraversal.h>

namespace oxygen::scene {

namespace {

  auto ResolveWorldRotation(
    const Scene& scene_ref, const SceneNodeImpl& node) -> glm::quat
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
      = [&scene, &resolved, &traversal_index](const ConstVisitedNode& visited,
          const bool dry_run) -> VisitResult {
      static_cast<void>(dry_run);

      const auto& node = *visited.node_impl;
      if (!node.HasComponent<detail::TransformComponent>()
        || !node.HasComponent<DirectionalLight>()) {
        return VisitResult::kContinue;
      }

      const auto& light = node.GetComponent<DirectionalLight>();
      if (!light.Common().affects_world) {
        return VisitResult::kContinue;
      }

      const auto emitted_ray_direction_ws
        = ComputeEmittedRayDirectionWs(scene, node);
      resolved.emplace_back(visited.handle, node, light, emitted_ray_direction_ws,
        -emitted_ray_direction_ws, traversal_index++);
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
    static_cast<void>(scene_->RegisterObserver(
      observer_ptr<ISceneObserver> { this },
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
    throw DirectionalLightContractError(validation_error_.value_or(
      "invalid directional light contract"));
  }
}

auto DirectionalLightResolver::ResolvePrimarySun() const
  -> std::optional<ResolvedDirectionalLightView>
{
  Validate();
  if (primary_sun_light_.has_value()) {
    return primary_sun_light_;
  }
  return ResolvePrimaryEnvironmentLight();
}

auto DirectionalLightResolver::ResolveSecondarySun() const
  -> std::optional<ResolvedDirectionalLightView>
{
  Validate();
  return ResolveSecondaryEnvironmentLight();
}

auto DirectionalLightResolver::ResolveMoon() const
  -> std::optional<ResolvedDirectionalLightView>
{
  return ResolveSecondarySun();
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
  primary_sun_light_.reset();
  primary_environment_light_.reset();
  secondary_environment_light_.reset();
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

  dirty_ = false;
  primary_environment_light_ = ResolvePrimaryEnvironmentLight();
  secondary_environment_light_ = ResolveSecondaryEnvironmentLight();
  if (primary_environment_light_.has_value()
    && primary_environment_light_->Light().IsSunLight()) {
    primary_sun_light_ = primary_environment_light_;
  }
}

auto DirectionalLightResolver::MarkDirty() noexcept -> void
{
  dirty_ = true;
}

auto DirectionalLightResolver::ResolvePrimaryEnvironmentLight() const
  -> std::optional<ResolvedDirectionalLightView>
{
  if (dirty_) {
    RebuildIfDirty();
  }
  if (!valid_) {
    return std::nullopt;
  }

  const auto sun = std::ranges::find_if(directional_lights_,
    [](const ResolvedDirectionalLightView& entry) {
      const auto& light = entry.Light();
      return light.GetEnvironmentContribution() && light.IsSunLight();
    });
  if (sun != directional_lights_.end()) {
    return *sun;
  }

  const auto environment_light = std::ranges::find_if(directional_lights_,
    [](const ResolvedDirectionalLightView& entry) {
      return entry.Light().GetEnvironmentContribution();
    });
  if (environment_light != directional_lights_.end()) {
    return *environment_light;
  }
  return std::nullopt;
}

auto DirectionalLightResolver::ResolveSecondaryEnvironmentLight() const
  -> std::optional<ResolvedDirectionalLightView>
{
  if (dirty_) {
    RebuildIfDirty();
  }
  if (!valid_) {
    return std::nullopt;
  }

  const auto primary = ResolvePrimaryEnvironmentLight();
  if (!primary.has_value()) {
    return std::nullopt;
  }

  const auto secondary = std::ranges::find_if(directional_lights_,
    [&primary](const ResolvedDirectionalLightView& entry) {
      return entry.Light().GetEnvironmentContribution()
        && entry.NodeHandle() != primary->NodeHandle();
    });
  if (secondary != directional_lights_.end()) {
    return *secondary;
  }
  return std::nullopt;
}

auto DirectionalLightResolver::ValidationErrorMessage() const
  -> std::optional<std::string>
{
  auto environment_contribution_count = 0U;
  auto sun_light_count = 0U;

  for (const auto& entry : directional_lights_) {
    const auto& light = entry.Light();
    if (light.IsSunLight() && !light.GetEnvironmentContribution()) {
      return std::string("directional light '")
        + entry.Node().GetName().data()
        + "' has is_sun_light=true but environment_contribution=false";
    }
    if (light.GetEnvironmentContribution()) {
      ++environment_contribution_count;
      if (light.IsSunLight()) {
        ++sun_light_count;
      }
    }
  }

  if (directional_lights_.size() > 2U && scene_ != nullptr) {
    LOG_F(WARNING,
      "scene '{}' has {} directional lights",
      scene_->GetName(), directional_lights_.size());
  }

  if (environment_contribution_count > 2U) {
    return "scene has more than two directional "
           "lights with environment_contribution=true";
  }
  if (sun_light_count > 1U) {
    return "scene has more than one directional "
           "light with is_sun_light=true and environment_contribution=true";
  }
  return std::nullopt;
}

} // namespace oxygen::scene
