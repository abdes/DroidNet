//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>

namespace oxygen::vortex {

// NOLINTNEXTLINE(*-enum-size)
enum class RendererCapabilityFamily : std::uint32_t {
  kNone = 0,
  kScenePreparation = OXYGEN_FLAG(0),
  kGpuUploadAndAssetBinding = OXYGEN_FLAG(1),
  kLightingData = OXYGEN_FLAG(2),
  kShadowing = OXYGEN_FLAG(3),
  kEnvironmentLighting = OXYGEN_FLAG(4),
  kFinalOutputComposition = OXYGEN_FLAG(5),
  kDiagnosticsAndProfiling = OXYGEN_FLAG(6),
  kDeferredShading = OXYGEN_FLAG(7),
  kAll = 0xFFU,
};

OXYGEN_DEFINE_FLAGS_OPERATORS(RendererCapabilityFamily)

using CapabilitySet = RendererCapabilityFamily;

inline constexpr auto kPhase1DefaultRuntimeCapabilityFamilies
  = RendererCapabilityFamily::kScenePreparation
  | RendererCapabilityFamily::kGpuUploadAndAssetBinding
  | RendererCapabilityFamily::kFinalOutputComposition;

struct PipelineCapabilityRequirements {
  CapabilitySet required { CapabilitySet::kNone };
  CapabilitySet optional { CapabilitySet::kNone };
};

struct PipelineCapabilityValidation {
  CapabilitySet available { CapabilitySet::kNone };
  CapabilitySet missing_required { CapabilitySet::kNone };
  CapabilitySet missing_optional { CapabilitySet::kNone };

  [[nodiscard]] constexpr auto Ok() const noexcept -> bool
  {
    return missing_required == CapabilitySet::kNone;
  }
};

[[nodiscard]] constexpr auto MissingCapabilities(const CapabilitySet available,
  const CapabilitySet requested) noexcept -> CapabilitySet
{
  using Underlying = std::underlying_type_t<CapabilitySet>;
  return static_cast<CapabilitySet>(
    static_cast<Underlying>(requested) & ~static_cast<Underlying>(available));
}

[[nodiscard]] constexpr auto HasAllCapabilities(
  const CapabilitySet available, const CapabilitySet requested) noexcept -> bool
{
  return MissingCapabilities(available, requested) == CapabilitySet::kNone;
}

[[nodiscard]] constexpr auto ValidateCapabilityRequirements(
  const CapabilitySet available,
  const PipelineCapabilityRequirements requirements) noexcept
  -> PipelineCapabilityValidation
{
  return PipelineCapabilityValidation {
    .available = available,
    .missing_required = MissingCapabilities(available, requirements.required),
    .missing_optional = MissingCapabilities(available, requirements.optional),
  };
}

[[nodiscard]] inline auto to_string(const CapabilitySet capabilities)
  -> std::string
{
  using Family = RendererCapabilityFamily;

  if (capabilities == Family::kNone) {
    return "None";
  }

  std::string result;
  auto checked = Family::kNone;
  bool first = true;

  const auto append_family = [&](const Family family, const char* name) {
    if ((capabilities & family) == family) {
      if (!first) {
        result += " | ";
      }
      result += name;
      checked |= family;
      first = false;
    }
  };

  append_family(Family::kScenePreparation, "ScenePreparation");
  append_family(Family::kGpuUploadAndAssetBinding, "GpuUploadAndAssetBinding");
  append_family(Family::kLightingData, "LightingData");
  append_family(Family::kShadowing, "Shadowing");
  append_family(Family::kEnvironmentLighting, "EnvironmentLighting");
  append_family(Family::kFinalOutputComposition, "FinalOutputComposition");
  append_family(Family::kDiagnosticsAndProfiling, "DiagnosticsAndProfiling");
  append_family(Family::kDeferredShading, "DeferredShading");

  DCHECK_EQ_F(
    checked, capabilities, "Unchecked RendererCapabilityFamily value detected");

  if (result.empty()) {
    return "None";
  }
  return result;
}

} // namespace oxygen::vortex
