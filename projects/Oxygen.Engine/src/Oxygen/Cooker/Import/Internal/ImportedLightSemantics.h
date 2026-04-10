//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string_view>

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

[[nodiscard]] constexpr auto ResolveImportedLightCommon(
  const ImportedLightSemantics& node_overrides,
  const ImportedLightSemantics& light_overrides,
  const bool default_affects_world, const bool default_casts_shadows) noexcept
  -> data::pak::world::LightCommonRecord
{
  auto common = data::pak::world::LightCommonRecord {};
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

} // namespace oxygen::content::import::detail
