//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/RendererCapability.h>

namespace oxygen::vortex {

struct ViewFeatureProfileSpec {
  CompositionView::ViewFeatureProfile profile {
    CompositionView::ViewFeatureProfile::kDefault
  };
  CompositionView::ViewFeatureMask feature_mask {};
  PipelineCapabilityRequirements capability_requirements {};
  bool depth_only { false };
  bool shadow_only { false };
  bool diagnostics_only { false };
  bool requires_color_output { true };
};

[[nodiscard]] constexpr auto MakeFeatureMask(const std::uint64_t bits) noexcept
  -> CompositionView::ViewFeatureMask
{
  return CompositionView::ViewFeatureMask {
    .bits = CompositionView::ViewFeatureBits { bits },
  };
}

[[nodiscard]] constexpr auto ToString(
  const CompositionView::ViewFeatureProfile profile) noexcept -> std::string_view
{
  using Profile = CompositionView::ViewFeatureProfile;
  switch (profile) {
  case Profile::kDefault:
    return "default";
  case Profile::kDepthOnly:
    return "depth-only";
  case Profile::kShadowOnly:
    return "shadow-only";
  case Profile::kNoEnvironment:
    return "no-environment";
  case Profile::kNoShadowing:
    return "no-shadowing";
  case Profile::kNoVolumetrics:
    return "no-volumetrics";
  case Profile::kDiagnosticsOnly:
    return "diagnostics-only";
  default:
    return "__unknown__";
  }
}

[[nodiscard]] constexpr auto ResolveViewFeatureProfileSpec(
  const CompositionView::ViewFeatureProfile profile) noexcept
  -> ViewFeatureProfileSpec
{
  using Mask = CompositionView::ViewFeatureMask;
  using Family = RendererCapabilityFamily;
  using Profile = CompositionView::ViewFeatureProfile;

  switch (profile) {
  case Profile::kDepthOnly:
    return ViewFeatureProfileSpec {
      .profile = profile,
      .feature_mask = MakeFeatureMask(Mask::kNone),
      .capability_requirements = {
        .required = Family::kScenePreparation | Family::kDeferredShading,
        .optional = Family::kDiagnosticsAndProfiling,
      },
      .depth_only = true,
      .requires_color_output = false,
    };
  case Profile::kShadowOnly:
    return ViewFeatureProfileSpec {
      .profile = profile,
      .feature_mask = MakeFeatureMask(Mask::kShadows),
      .capability_requirements = {
        .required = Family::kScenePreparation | Family::kDeferredShading
          | Family::kLightingData | Family::kShadowing,
        .optional = Family::kDiagnosticsAndProfiling,
      },
      .shadow_only = true,
      .requires_color_output = false,
    };
  case Profile::kNoEnvironment:
    return ViewFeatureProfileSpec {
      .profile = profile,
      .feature_mask = MakeFeatureMask(Mask::kSceneLighting | Mask::kShadows
        | Mask::kTranslucency | Mask::kDiagnostics),
      .capability_requirements = {
        .required = Family::kScenePreparation | Family::kDeferredShading
          | Family::kLightingData | Family::kFinalOutputComposition,
        .optional = Family::kShadowing | Family::kDiagnosticsAndProfiling,
      },
    };
  case Profile::kNoShadowing:
    return ViewFeatureProfileSpec {
      .profile = profile,
      .feature_mask = MakeFeatureMask(Mask::kSceneLighting
        | Mask::kEnvironment | Mask::kVolumetrics | Mask::kTranslucency
        | Mask::kDiagnostics),
      .capability_requirements = {
        .required = Family::kScenePreparation | Family::kDeferredShading
          | Family::kLightingData | Family::kFinalOutputComposition,
        .optional = Family::kEnvironmentLighting
          | Family::kDiagnosticsAndProfiling,
      },
    };
  case Profile::kNoVolumetrics:
    return ViewFeatureProfileSpec {
      .profile = profile,
      .feature_mask = MakeFeatureMask(Mask::kSceneLighting | Mask::kShadows
        | Mask::kEnvironment | Mask::kTranslucency | Mask::kDiagnostics),
      .capability_requirements = {
        .required = Family::kScenePreparation | Family::kDeferredShading
          | Family::kLightingData | Family::kEnvironmentLighting
          | Family::kFinalOutputComposition,
        .optional = Family::kShadowing | Family::kDiagnosticsAndProfiling,
      },
    };
  case Profile::kDiagnosticsOnly:
    return ViewFeatureProfileSpec {
      .profile = profile,
      .feature_mask = MakeFeatureMask(Mask::kDiagnostics),
      .capability_requirements = {
        .required = Family::kDiagnosticsAndProfiling
          | Family::kFinalOutputComposition,
        .optional = Family::kScenePreparation,
      },
      .diagnostics_only = true,
    };
  case Profile::kDefault:
  default:
    return ViewFeatureProfileSpec {
      .profile = Profile::kDefault,
      .feature_mask = CompositionView::ViewFeatureMask {},
      .capability_requirements = {
        .required = Family::kScenePreparation | Family::kDeferredShading
          | Family::kLightingData | Family::kFinalOutputComposition,
        .optional = Family::kShadowing | Family::kEnvironmentLighting
          | Family::kDiagnosticsAndProfiling,
      },
    };
  }
}

} // namespace oxygen::vortex
