//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ImportedPropertyCommit.h>
#include <Oxygen/Core/Lighting/LightPhotometry.h>
#include <Oxygen/Data/LightValidation.h>
#include <Oxygen/Data/PakFormat_world.h>

namespace oxygen::content::import::detail {

struct ImportedLightSemantics final {
  std::optional<bool> affects_world;
  std::optional<bool> casts_shadows;
};

inline constexpr std::string_view kImportedLightExtrasNamespace = "oxygen";
inline constexpr std::string_view kImportedLightAffectsWorldKey
  = "affects_world";
inline constexpr std::string_view kImportedLightCastsShadowsKey
  = "casts_shadows";
inline constexpr std::string_view kImportedLightAffectsWorldFbxProp
  = "Oxygen.AffectsWorld";
inline constexpr std::string_view kImportedLightCastsShadowsFbxProp
  = "Oxygen.CastsShadows";

[[nodiscard]] constexpr auto ResolveImportedLightFlag(
  const std::optional<bool> node_override,
  const std::optional<bool> light_override, const bool fallback) noexcept
  -> bool
{
  return node_override.value_or(light_override.value_or(fallback));
}

//! `common` with the flags the importer's semantics resolve.
/*!
 `common` holds the light kind's schema defaults; every other field stays.
*/
[[nodiscard]] constexpr auto ResolveImportedLightCommon(
  data::pak::world::LightCommonRecord common,
  const ImportedLightSemantics& node_overrides,
  const ImportedLightSemantics& light_overrides,
  const bool default_affects_world, const bool default_casts_shadows) noexcept
  -> data::pak::world::LightCommonRecord
{
  common.affects_world = ResolveImportedLightFlag(node_overrides.affects_world,
                           light_overrides.affects_world, default_affects_world)
    ? 1U
    : 0U;
  common.casts_shadows = ResolveImportedLightFlag(node_overrides.casts_shadows,
                           light_overrides.casts_shadows, default_casts_shadows)
    ? 1U
    : 0U;
  return common;
}

//! Commits an imported linear RGB light color into `common`, through the
//! light kind's color ranges.
inline auto CommitImportedLightColor(ImportedPropertyCommit& commit,
  const data::LightCommonRanges& ranges, const std::array<float, 3>& color,
  data::pak::world::LightCommonRecord& common) -> void
{
  constexpr auto kChannels = std::array<std::string_view, 3> {
    "color_rgb[0]",
    "color_rgb[1]",
    "color_rgb[2]",
  };
  for (std::size_t channel = 0; channel < kChannels.size(); ++channel) {
    common.color_rgb[channel] = commit(kChannels[channel],
      ranges.color[channel], color[channel], common.color_rgb[channel]);
  }
}

//! Commits an imported spot cone into `light`; its profile, or nothing with a
//! warning when the angles form no cone and the light must be dropped.
/*!
 The angles are half angles in radians. Each is a property of the schema, but
 the pair is a rule across both, which the scene pipeline refuses to cook: the
 importer drops such a light rather than fail the model.
*/
inline auto CommitImportedSpotCone(ImportedPropertyCommit& commit,
  const float inner_half_angle_radians, const float outer_half_angle_radians,
  data::pak::world::SpotLightRecord& light, const std::string_view source_id,
  const std::string_view object_path,
  std::vector<ImportDiagnostic>& diagnostics)
  -> std::optional<lighting::SpotConeProfile>
{
  namespace light_schema = data::schema::spot_light;
  light.inner_cone_angle_radians = commit("inner_cone_angle_radians",
    light_schema::kInnerConeAngleRadiansRange, inner_half_angle_radians,
    light.inner_cone_angle_radians);
  light.outer_cone_angle_radians = commit("outer_cone_angle_radians",
    light_schema::kOuterConeAngleRadiansRange, outer_half_angle_radians,
    light.outer_cone_angle_radians);
  const auto cone = lighting::ResolveSpotConeProfile(
    light.inner_cone_angle_radians, light.outer_cone_angle_radians);
  if (!cone) {
    diagnostics.push_back(ImportDiagnostic {
      .severity = ImportSeverity::kWarning,
      .code = std::string(import_codes::kSceneLightPhotometryInvalid),
      .message = "Spot cone angles do not form a representable cone pair; the "
                 "light is dropped",
      .source_path = std::string(source_id),
      .object_path = std::string(object_path),
    });
    return std::nullopt;
  }
  return *cone;
}

} // namespace oxygen::content::import::detail
