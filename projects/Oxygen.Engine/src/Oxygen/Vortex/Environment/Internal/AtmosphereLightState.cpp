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
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::vortex::environment::internal {

namespace {

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

  auto AssignLightToSlot(const scene::ResolvedDirectionalLightView& resolved,
    const std::uint32_t slot_index,
    ResolvedAtmosphereLightState& state) -> void
  {
    auto model = environment::AtmosphereLightModel {};
    model.enabled = true;
    model.use_per_pixel_transmittance
      = resolved.Light().GetUsePerPixelAtmosphereTransmittance();
    model.direction_to_light_ws = resolved.DirectionToLightWs();
    model.angular_size_radians = resolved.Light().GetAngularSizeRadians();
    model.illuminance_rgb_lux
      = resolved.Light().Common().color_rgb
      * resolved.Light().GetIntensityLux();
    model.illuminance_lux = resolved.Light().GetIntensityLux();
    model.disk_luminance_scale_rgb
      = resolved.Light().GetAtmosphereDiskLuminanceScale();
    model.slot_index = slot_index;
    state.atmosphere_lights[slot_index] = model;
    state.source_nodes[slot_index] = resolved.NodeHandle();
    state.source_cascade_counts[slot_index]
      = resolved.Light().CascadedShadows().cascade_count;
    state.explicit_slot_claims[slot_index] = true;
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
} // namespace

auto AtmosphereLightState::Update(const scene::Scene& scene_ref) -> bool
{
  const auto& resolver = scene_ref.GetDirectionalLightResolver();
  resolver.Validate();
  auto next = ResolvedAtmosphereLightState {};

  if (const auto primary = resolver.ResolvePrimarySun();
    primary.has_value()) {
    AssignLightToSlot(*primary, 0U, next);
  }
  if (const auto secondary = resolver.ResolveSecondarySun();
    secondary.has_value()) {
    AssignLightToSlot(*secondary, 1U, next);
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

  next.revision = state_.revision + 1U;
  state_ = next;
  return true;
}

auto AtmosphereLightState::Reset() -> void
{
  state_ = ResolvedAtmosphereLightState {};
}

} // namespace oxygen::vortex::environment::internal
