//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>

#include <glm/ext/vector_double3.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Profiling/CpuProfileScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Lighting/Internal/LightEvaluationRecords.h>
#include <Oxygen/Core/Lighting/LightPhotometry.h>
#include <Oxygen/Vortex/Lighting/Types/DirectionalLightForwardData.h>
#include <Oxygen/Vortex/Lighting/Types/ForwardLocalLightRecord.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>

namespace oxygen::vortex::lighting::internal {
using oxygen::lighting::LightPhotometryError;
using oxygen::lighting::LightPhotometryModifiers;
using oxygen::lighting::SpotConeProfile;
using oxygen::lighting::ResolveDirectionalIlluminanceRgb;
using oxygen::lighting::ResolvePointIntensityRgb;
using oxygen::lighting::ResolveSpotIntensityRgb;
using oxygen::lighting::ResolveSpotConeProfile;
namespace {

  auto IsFinite(const glm::vec3 value) -> bool
  {
    return std::isfinite(value.x) && std::isfinite(value.y)
      && std::isfinite(value.z);
  }

  auto NormalizeDirection(const glm::vec3 value) -> std::optional<glm::vec3>
  {
    if (!IsFinite(value)) {
      return std::nullopt;
    }
    const auto direction = glm::dvec3 { value };
    const auto length = glm::length(direction);
    if (length == 0.0) {
      return std::nullopt;
    }
    return glm::vec3 { direction / length };
  }

  auto ConvertError(const LightPhotometryError error)
    -> LightingPreparationError
  {
    return error == LightPhotometryError::kUnrepresentable
      ? LightingPreparationError::kUnrepresentable
      : LightingPreparationError::kInvalidInput;
  }

} // namespace

auto ResolveLightEvaluationRecords(const FrameLightSelection& input)
  -> std::expected<LightEvaluationRecords, LightingPreparationFailure>
{
  // Cache the owning label; steady-state scope entry needs no label allocation.
  static const auto kProfile = profiling::CpuProfileScopeDesc {
    .label = "Vortex.Lighting.ResolveEvaluation",
    .category = profiling::ProfileCategory::kPass,
  };
  const auto profile = profiling::CpuProfileScope(kProfile);
  auto records = LightEvaluationRecords {};
  if (input.directional_lights.size() >= kInvalidLightingArrayIndexValue) {
    return std::unexpected(LightingPreparationFailure {
      .error = LightingPreparationError::kUnrepresentable,
      .family = LightingSelectionFamily::kDirectional,
    });
  }
  records.directional.reserve(input.directional_lights.size());
  auto atmosphere_claimed = std::array<bool, 2> {};
  for (std::size_t index = 0U; index < input.directional_lights.size();
    ++index) {
    const auto& source = input.directional_lights.at(index);
    auto failure = LightingPreparationFailure {
      .family = LightingSelectionFamily::kDirectional,
      .selection_index
      = LightSelectionIndex { static_cast<std::uint32_t>(index) },
    };
    const auto direction = NormalizeDirection(source.direction);
    const auto transmittance = source.transmittance_toward_sun_rgb;
    constexpr auto kKnownAtmosphereFlags
      = kDirectionalLightAtmosphereModeFlagAuthority
      | kDirectionalLightAtmosphereModeFlagPerPixelTransmittance
      | kDirectionalLightAtmosphereModeFlagHasBakedGroundTransmittance;
    if (!direction || !IsFinite(transmittance) || transmittance.r < 0.0F
      || transmittance.r > 1.0F || transmittance.g < 0.0F
      || transmittance.g > 1.0F || transmittance.b < 0.0F
      || transmittance.b > 1.0F
      || (source.atmosphere_light_slot != kInvalidAtmosphereLightIndex.get()
        && source.atmosphere_light_slot > 1U)
      || (source.atmosphere_mode_flags & ~kKnownAtmosphereFlags) != 0U
      || (source.shadow_flags & ~kLightRequestFlags) != 0U
      || ((source.shadow_flags & kDirectionalLightShadowFlagCastsShadows) != 0U
        && (source.cascade_count == 0U
          || source.cascade_count > kFrameDirectionalLightMaxCascades))) {
      return std::unexpected(failure);
    }
    if (source.atmosphere_light_slot != kInvalidAtmosphereLightIndex.get()) {
      auto& claimed = atmosphere_claimed.at(source.atmosphere_light_slot);
      if (claimed) {
        return std::unexpected(failure);
      }
      claimed = true;
    }
    const auto illuminance
      = ResolveDirectionalIlluminanceRgb(source.illuminance_lux,
        {
          .color_rgb = source.color,
          .exposure_compensation_ev = source.exposure_compensation_ev,
        });
    if (!illuminance) {
      failure.error = ConvertError(illuminance.error());
      return std::unexpected(failure);
    }
    records.directional.push_back(DirectionalLightForwardData {
      .direction_to_source_ws = *direction,
      .atmosphere_light_slot
      = AtmosphereLightIndex { source.atmosphere_light_slot },
      .illuminance_rgb_lux = *illuminance,
      .flags = source.shadow_flags,
      .ground_transmittance_rgb = transmittance,
      .atmosphere_mode_flags = source.atmosphere_mode_flags,
      .selection_index = failure.selection_index,
    });
  }

  if (input.local_lights.size() >= kInvalidLightingArrayIndexValue) {
    return std::unexpected(LightingPreparationFailure {
      .error = LightingPreparationError::kUnrepresentable,
      .family = LightingSelectionFamily::kLocal,
    });
  }
  records.local.reserve(input.local_lights.size());
  for (std::size_t index = 0; index < input.local_lights.size(); ++index) {
    const auto& source = input.local_lights.at(index);
    auto failure = LightingPreparationFailure {
      .family = LightingSelectionFamily::kLocal,
      .selection_index
      = LightSelectionIndex { static_cast<std::uint32_t>(index) },
    };
    const auto point = source.kind == LocalLightKind::kPoint;
    if ((!point && source.kind != LocalLightKind::kSpot)
      || !IsFinite(source.position) || !std::isfinite(source.range)
      || source.range < 0.0F || !std::isfinite(source.source_radius)
      || source.source_radius < 0.0F
      || (source.flags & ~kLightRequestFlags) != 0U) {
      return std::unexpected(failure);
    }
    const auto inverse_range
      = source.range == 0.0F ? 0.0 : 1.0 / static_cast<double>(source.range);
    if (inverse_range > std::numeric_limits<float>::max()
      || (inverse_range > 0.0
        && inverse_range < std::numeric_limits<float>::min())
      || static_cast<double>(source.range) + source.source_radius
        > std::numeric_limits<float>::max()) {
      failure.error = LightingPreparationError::kUnrepresentable;
      return std::unexpected(failure);
    }
    auto direction = glm::vec3 { 0.0F, -1.0F, 0.0F };
    auto cone = SpotConeProfile {};
    if (!point) {
      const auto normalized = NormalizeDirection(source.direction);
      const auto cone_profile
        = ResolveSpotConeProfile(source.inner_cone_half_angle_radians,
          source.outer_cone_half_angle_radians);
      if (!normalized || !cone_profile) {
        if (!cone_profile) {
          failure.error = ConvertError(cone_profile.error());
        }
        return std::unexpected(failure);
      }
      direction = *normalized;
      cone = *cone_profile;
    }
    const auto modifiers = LightPhotometryModifiers {
      .color_rgb = source.color,
      .exposure_compensation_ev = source.exposure_compensation_ev,
    };
    const auto intensity = point
      ? ResolvePointIntensityRgb(source.luminous_flux_lm, modifiers)
      : ResolveSpotIntensityRgb(source.luminous_flux_lm, cone, modifiers);
    if (!intensity) {
      failure.error = ConvertError(intensity.error());
      return std::unexpected(failure);
    }
    records.local.push_back(ForwardLocalLightRecord {
      .position_ws = source.position,
      .range_m = source.range,
      .intensity_rgb_cd = *intensity,
      .source_radius_m = source.source_radius,
      .emitted_direction_ws = direction,
      .inverse_range_m = static_cast<float>(inverse_range),
      .outer_cone_cosine = cone.outer_cosine,
      .inverse_cone_cosine_width = cone.inverse_cosine_width,
      .kind = static_cast<std::uint32_t>(source.kind),
      .flags = source.flags,
      .selection_index = failure.selection_index,
    });
  }
  return records;
}

} // namespace oxygen::vortex::lighting::internal
