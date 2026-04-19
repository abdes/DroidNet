//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Internal/AtmosphereLightState.h>

#include <bit>
#include <string>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/SceneTraversal.h>

namespace oxygen::vortex::environment::internal {

namespace {

  struct AtmosphereLightCandidate {
    scene::NodeHandle node_handle {};
    std::string node_name {};
    environment::AtmosphereLightModel model {};
    scene::AtmosphereLightSlot authored_slot { scene::AtmosphereLightSlot::kNone };
    bool is_sun_light { false };
    std::uint32_t cascade_count { 0U };
  };

  struct ConflictRecord {
    std::uint32_t slot_index { environment::kInvalidAtmosphereLightSlot };
    std::string winner_name {};
    std::string ignored_name {};
  };

  auto HashCombineU64(std::uint64_t seed, const std::uint64_t value)
    -> std::uint64_t
  {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
  }

  auto FloatBits(const float value) -> std::uint32_t
  {
    return std::bit_cast<std::uint32_t>(value);
  }

  auto ResolveWorldRotation(
    const scene::Scene& scene_ref, const scene::SceneNodeImpl& node) -> glm::quat
  {
    const auto& transform
      = node.GetComponent<scene::detail::TransformComponent>();
    const auto ignore_parent = node.GetFlags().GetEffectiveValue(
      scene::SceneNodeFlags::kIgnoreParentTransform);
    auto rotation = transform.GetLocalRotation();
    if (const auto parent = node.AsGraphNode().GetParent();
      parent.IsValid() && !ignore_parent) {
      rotation
        = ResolveWorldRotation(scene_ref, scene_ref.GetNodeImplRef(parent))
        * rotation;
    }
    return rotation;
  }

  auto ComputeDirectionToLightWs(
    const scene::Scene& scene_ref, const scene::SceneNodeImpl& node) -> glm::vec3
  {
    const auto light_direction
      = ResolveWorldRotation(scene_ref, node) * space::move::Forward;
    const auto length_sq = glm::dot(light_direction, light_direction);
    if (length_sq <= math::EpsilonDirection) {
      return { 0.0F, 0.0F, 1.0F };
    }
    return -glm::normalize(light_direction);
  }

  auto BuildAtmosphereLightCandidate(const scene::Scene& scene_ref,
    const scene::ConstVisitedNode& visited) -> std::optional<AtmosphereLightCandidate>
  {
    const auto& node = *visited.node_impl;
    if (!node.HasComponent<scene::detail::TransformComponent>()
      || !node.HasComponent<scene::DirectionalLight>()) {
      return std::nullopt;
    }

    const auto& light = node.GetComponent<scene::DirectionalLight>();
    if (!light.Common().affects_world || !light.GetEnvironmentContribution()) {
      return std::nullopt;
    }

    auto model = environment::AtmosphereLightModel {};
    model.enabled = true;
    model.use_per_pixel_transmittance
      = light.GetUsePerPixelAtmosphereTransmittance();
    model.direction_to_light_ws = ComputeDirectionToLightWs(scene_ref, node);
    model.angular_size_radians = light.GetAngularSizeRadians();
    model.illuminance_rgb_lux
      = light.Common().color_rgb * light.GetIntensityLux();
    model.illuminance_lux = light.GetIntensityLux();
    model.disk_luminance_scale_rgb = light.GetAtmosphereDiskLuminanceScale();

    return AtmosphereLightCandidate {
      .node_handle = visited.handle,
      .node_name = std::string(node.GetName()),
      .model = model,
      .authored_slot = light.GetAtmosphereLightSlot(),
      .is_sun_light = light.IsSunLight(),
      .cascade_count = light.CascadedShadows().cascade_count,
    };
  }

  auto AssignLightToSlot(const AtmosphereLightCandidate& candidate,
    const std::uint32_t slot_index, const bool explicit_claim,
    ResolvedAtmosphereLightState& state) -> void
  {
    auto model = candidate.model;
    model.slot_index = slot_index;
    state.atmosphere_lights[slot_index] = model;
    state.source_nodes[slot_index] = candidate.node_handle;
    state.source_cascade_counts[slot_index] = candidate.cascade_count;
    state.explicit_slot_claims[slot_index] = explicit_claim;
  }

  [[nodiscard]] auto IsCandidateAssigned(
    const AtmosphereLightCandidate& candidate,
    const ResolvedAtmosphereLightState& state) -> bool
  {
    for (const auto& node_handle : state.source_nodes) {
      if (node_handle.IsValid()
        && node_handle.Index() == candidate.node_handle.Index()
        && node_handle.GetSceneId() == candidate.node_handle.GetSceneId()) {
        return true;
      }
    }
    return false;
  }

  auto HashResolvedState(
    const ResolvedAtmosphereLightState& state) -> std::uint64_t
  {
    auto seed = std::uint64_t { 0U };
    seed = HashCombineU64(seed, state.active_light_count);
    seed = HashCombineU64(seed, state.conflict_count);
    seed = HashCombineU64(seed, state.first_conflict_slot);
    seed = HashCombineU64(seed, state.shadow_authority_slot);
    seed = HashCombineU64(
      seed, static_cast<std::uint64_t>(state.shadow_authority_slot0_only));

    for (std::size_t index = 0; index < state.atmosphere_lights.size(); ++index) {
      const auto& light = state.atmosphere_lights[index];
      seed = HashCombineU64(seed, static_cast<std::uint64_t>(light.enabled));
      seed = HashCombineU64(seed,
        static_cast<std::uint64_t>(light.use_per_pixel_transmittance));
      seed = HashCombineU64(seed, light.slot_index);
      seed = HashCombineU64(seed, FloatBits(light.direction_to_light_ws.x));
      seed = HashCombineU64(seed, FloatBits(light.direction_to_light_ws.y));
      seed = HashCombineU64(seed, FloatBits(light.direction_to_light_ws.z));
      seed = HashCombineU64(seed, FloatBits(light.angular_size_radians));
      seed = HashCombineU64(seed, FloatBits(light.illuminance_rgb_lux.x));
      seed = HashCombineU64(seed, FloatBits(light.illuminance_rgb_lux.y));
      seed = HashCombineU64(seed, FloatBits(light.illuminance_rgb_lux.z));
      seed = HashCombineU64(seed, FloatBits(light.illuminance_lux));
      seed = HashCombineU64(seed, FloatBits(light.disk_luminance_scale_rgb.x));
      seed = HashCombineU64(seed, FloatBits(light.disk_luminance_scale_rgb.y));
      seed = HashCombineU64(seed, FloatBits(light.disk_luminance_scale_rgb.z));

      seed = HashCombineU64(
        seed, static_cast<std::uint64_t>(state.source_nodes[index].Index()));
      seed = HashCombineU64(seed,
        static_cast<std::uint64_t>(state.source_nodes[index].GetSceneId()));
      seed = HashCombineU64(seed, state.source_cascade_counts[index]);
      seed = HashCombineU64(seed,
        static_cast<std::uint64_t>(state.explicit_slot_claims[index]));
    }

    return seed;
  }

  auto ResolveWinnerName(const scene::NodeHandle handle,
    const std::vector<AtmosphereLightCandidate>& candidates) -> std::string
  {
    if (!handle.IsValid()) {
      return {};
    }

    for (const auto& candidate : candidates) {
      if (candidate.node_handle.Index() == handle.Index()
        && candidate.node_handle.GetSceneId() == handle.GetSceneId()) {
        return candidate.node_name;
      }
    }
    return {};
  }

} // namespace

auto AtmosphereLightState::Update(const scene::Scene& scene_ref) -> bool
{
  auto candidates = std::vector<AtmosphereLightCandidate> {};
  const auto visitor = [&scene_ref, &candidates](const scene::ConstVisitedNode& visited,
                         const bool dry_run) -> scene::VisitResult {
    static_cast<void>(dry_run);
    if (const auto candidate
      = BuildAtmosphereLightCandidate(scene_ref, visited);
      candidate.has_value()) {
      candidates.push_back(*candidate);
    }
    return scene::VisitResult::kContinue;
  };

  static_cast<void>(scene_ref.Traverse().Traverse(
    visitor, scene::TraversalOrder::kPreOrder));

  auto next = ResolvedAtmosphereLightState {};
  auto conflicts = std::vector<ConflictRecord> {};

  for (const auto& candidate : candidates) {
    std::uint32_t slot_index = environment::kInvalidAtmosphereLightSlot;
    switch (candidate.authored_slot) {
    case scene::AtmosphereLightSlot::kPrimary:
      slot_index = 0U;
      break;
    case scene::AtmosphereLightSlot::kSecondary:
      slot_index = 1U;
      break;
    case scene::AtmosphereLightSlot::kNone:
      break;
    }

    if (slot_index == environment::kInvalidAtmosphereLightSlot) {
      continue;
    }

    if (!next.atmosphere_lights[slot_index].enabled) {
      AssignLightToSlot(candidate, slot_index, true, next);
      continue;
    }

    next.conflict_count += 1U;
    if (next.first_conflict_slot == environment::kInvalidAtmosphereLightSlot) {
      next.first_conflict_slot = slot_index;
    }
    conflicts.push_back(ConflictRecord {
      .slot_index = slot_index,
      .winner_name = ResolveWinnerName(next.source_nodes[slot_index], candidates),
      .ignored_name = candidate.node_name,
    });
  }

  if (!next.atmosphere_lights[0].enabled) {
    for (const auto& candidate : candidates) {
      if (candidate.is_sun_light && !IsCandidateAssigned(candidate, next)) {
        AssignLightToSlot(candidate, 0U, false, next);
        break;
      }
    }
  }

  if (!next.atmosphere_lights[0].enabled) {
    for (const auto& candidate : candidates) {
      if (!IsCandidateAssigned(candidate, next)) {
        AssignLightToSlot(candidate, 0U, false, next);
        break;
      }
    }
  }

  next.active_light_count = 0U;
  for (const auto& light : next.atmosphere_lights) {
    next.active_light_count += light.enabled ? 1U : 0U;
  }
  if (next.atmosphere_lights[0].enabled) {
    next.shadow_authority_slot = 0U;
  }

  next.authored_hash = HashResolvedState(next);
  if (next.authored_hash == state_.authored_hash) {
    return false;
  }

  for (const auto& conflict : conflicts) {
    LOG_F(ERROR,
      "Atmosphere-light slot {} conflict detected; first-wins keeps '{}' and "
      "ignores '{}'",
      conflict.slot_index, conflict.winner_name, conflict.ignored_name);
  }

  next.revision = state_.revision + 1U;
  state_ = next;
  return true;
}

auto AtmosphereLightState::Reset() -> void { state_ = ResolvedAtmosphereLightState {}; }

} // namespace oxygen::vortex::environment::internal
