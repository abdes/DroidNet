//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include <Oxygen/Vortex/Diagnostics/GpuWorkSummary.h>
#include <Oxygen/Vortex/api_export.h>

//! GPU scope labels that carry GPU work summary time.
/*!
 Scope sites use these constants, so a label is spelled once and a renamed
 pass cannot silently fall out of its bucket. Every `kTelemetry` scope must
 be one of these labels or nest inside one; any other lands in `kOther`.
*/
namespace oxygen::vortex::gpu_work {

inline constexpr std::string_view kFrameLabel = "Vortex.Frame";
//! One per view; carries the `view` and `name` variables.
inline constexpr std::string_view kViewLabel = "Vortex.View";

inline constexpr std::string_view kOcclusionPhase1Label
  = "Vortex.Stage3.Occlusion.Phase1";
inline constexpr std::string_view kOcclusionPhase2Label
  = "Vortex.Stage3.Occlusion.Phase2";
inline constexpr std::string_view kOcclusionHzbLabel
  = "Vortex.Occlusion.PyramidBuild";
inline constexpr std::string_view kScreenHzbLabel
  = "Vortex.Stage5.ScreenHzbBuild";
inline constexpr std::string_view kDepthPrepassLabel
  = "Vortex.Stage3.DepthPrepass";
inline constexpr std::string_view kShadowDepthsLabel
  = "Vortex.Stage8.ShadowDepths";
inline constexpr std::string_view kShadowReceiverFitLabel
  = "Vortex.Stage8.ShadowReceiverFit";
inline constexpr std::string_view kContactShadowCasterDepthLabel
  = "Vortex.Stage8.ContactShadowCasterDepth";
inline constexpr std::string_view kBasePassLabel = "Vortex.Stage9.BasePass";
inline constexpr std::string_view kSpatialLightGridLabel
  = "Vortex.Stage6.SpatialLightGrid";
inline constexpr std::string_view kDeferredLightingLabel
  = "Vortex.Stage12.DeferredLighting";
inline constexpr std::string_view kIndirectLightingLabel
  = "Vortex.Stage13.IndirectLighting";
inline constexpr std::string_view kLocalFogTiledCullingLabel
  = "Vortex.Stage14.LocalFogTiledCulling";
inline constexpr std::string_view kVolumetricFogLabel
  = "Vortex.Stage14.VolumetricFog";
inline constexpr std::string_view kAtmosphereLabel
  = "Vortex.Stage15.Atmosphere";
inline constexpr std::string_view kFogLabel = "Vortex.Stage15.Fog";
inline constexpr std::string_view kLocalFogLabel = "Vortex.Stage15.LocalFog";
inline constexpr std::string_view kSkyLabel = "Vortex.Stage15.Sky";
inline constexpr std::string_view kTranslucencyLabel
  = "Vortex.Stage18.Translucency";
inline constexpr std::string_view kResolveSceneColorLabel
  = "Vortex.ResolveSceneColor";
inline constexpr std::string_view kTonemapLabel = "Vortex.PostProcess.Tonemap";
inline constexpr std::string_view kPostRenderCleanupLabel
  = "Vortex.PostRenderCleanup";
inline constexpr std::string_view kGroundGridLabel
  = "Vortex.Stage20.GroundGrid";
inline constexpr std::string_view kSelectionOutlineLabel
  = "Vortex.Stage20.SelectionOutline";
inline constexpr std::string_view kViewOverlayLabel
  = "Vortex.Stage20.ViewOverlay";
inline constexpr std::string_view kWireframeOverlayLabel
  = "Vortex.Stage20.WireframeOverlay";
inline constexpr std::string_view kViewPickLabel = "Vortex.ViewPick";
//! The view's mode is the `mode` variable.
inline constexpr std::string_view kDebugVisualizationLabel
  = "Vortex.Stage12.DebugVisualization";
inline constexpr std::string_view kGpuDebugPrimitivesClearLabel
  = "Vortex.Diagnostics.GpuDebugPrimitives.Clear";
inline constexpr std::string_view kGpuDebugPrimitivesDrawLabel
  = "Vortex.Diagnostics.GpuDebugPrimitives.Draw";
inline constexpr std::string_view kCompositingTaskLabel
  = "Vortex.CompositingTask";
inline constexpr std::string_view kSurfaceOverlayLabel
  = "Vortex.SurfaceOverlay";
inline constexpr std::string_view kSurfaceLayerLabel = "Vortex.SurfaceLayer";
inline constexpr std::string_view kAuxViewConsumeLabel
  = "Vortex.AuxView.Consume";

inline constexpr std::string_view kAtmosphereTransmittanceLutLabel
  = "Vortex.Environment.AtmosphereTransmittanceLut";
inline constexpr std::string_view kAtmosphereMultiScatteringLutLabel
  = "Vortex.Environment.AtmosphereMultiScatteringLut";
inline constexpr std::string_view kAtmosphereSkyViewLutLabel
  = "Vortex.Environment.AtmosphereSkyViewLut";
inline constexpr std::string_view kAtmosphereCameraAerialPerspectiveLabel
  = "Vortex.Environment.AtmosphereCameraAerialPerspective";
inline constexpr std::string_view kDistantSkyLightLutLabel
  = "Vortex.Environment.DistantSkyLightLut";

inline constexpr std::string_view kIblBrdfUploadLabel
  = "Vortex.Environment.IBL.BrdfUpload";
inline constexpr std::string_view kIblProcessLabel
  = "Vortex.Environment.IBL.Process";
inline constexpr std::string_view kIblMetadataLabel
  = "Vortex.Diagnostics.IBL.Metadata";
inline constexpr std::string_view kIblAtmosphereTransmittanceLutLabel
  = "Vortex.Environment.IBL.AtmosphereTransmittanceLut";
inline constexpr std::string_view kIblAtmosphereMultiScatteringLutLabel
  = "Vortex.Environment.IBL.AtmosphereMultiScatteringLut";
inline constexpr std::string_view kIblAtmosphereSkyViewLutLabel
  = "Vortex.Environment.IBL.AtmosphereSkyViewLut";
inline constexpr std::string_view kIblDistantSkyLightLutLabel
  = "Vortex.Environment.IBL.DistantSkyLightLut";

inline constexpr std::string_view kExposureFrameResolveLabel
  = "Vortex.PostProcess.Exposure.FrameResolve";
inline constexpr std::string_view kExposureFrameFallbackLabel
  = "Vortex.PostProcess.Exposure.FrameFallback";
inline constexpr std::string_view kExposureSceneRangeLabel
  = "Vortex.PostProcess.Exposure.SceneRange";
inline constexpr std::string_view kExposureOpaqueApErrorLabel
  = "Vortex.PostProcess.Exposure.OpaqueApError";
inline constexpr std::string_view kExposureFilterGradientsLabel
  = "Vortex.PostProcess.Exposure.FilterGradients";
inline constexpr std::string_view kExposureQualifyProductsLabel
  = "Vortex.PostProcess.Exposure.QualifyProducts";
inline constexpr std::string_view kExposureClearSuitabilityLabel
  = "Vortex.PostProcess.Exposure.ClearSuitability";
inline constexpr std::string_view kExposureGatherSuitabilityMaximumLabel
  = "Vortex.PostProcess.Exposure.GatherSuitabilityMaximum";
inline constexpr std::string_view kExposureSelectSuitabilityCandidateLabel
  = "Vortex.PostProcess.Exposure.SelectSuitabilityCandidate";
inline constexpr std::string_view kExposureCheckSuitabilityProductLabel
  = "Vortex.PostProcess.Exposure.CheckSuitabilityProduct";
inline constexpr std::string_view kExposureFinalizeSuitabilityLabel
  = "Vortex.PostProcess.Exposure.FinalizeSuitability";
inline constexpr std::string_view kExposureConvertSceneColorLabel
  = "Vortex.PostProcess.Exposure.ConvertSceneColor";
inline constexpr std::string_view kExposureMeterAndAdaptLabel
  = "Vortex.PostProcess.Exposure.MeterAndAdapt";
inline constexpr std::string_view kExposureHistogramLabel
  = "Vortex.PostProcess.Exposure.Histogram";
inline constexpr std::string_view kExposureSolveLabel
  = "Vortex.PostProcess.Exposure.Solve";
inline constexpr std::string_view kExposureStatusReadbackLabel
  = "Vortex.PostProcess.Exposure.StatusReadback";

//! A label that carries summary time, and the item it is shown as.
struct StageEntry {
  std::string_view label;
  GpuWorkBucket bucket { GpuWorkBucket::kOther };
  std::string_view item_id;
  std::string_view display_name;
};

//! FNV-1a, as the GPU timeline hashes scope labels.
[[nodiscard]] constexpr auto HashScopeLabel(const std::string_view label)
  -> std::uint64_t
{
  constexpr std::uint64_t kOffsetBasis = 1469598103934665603ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  auto hash = kOffsetBasis;
  for (const char c : label) {
    hash ^= static_cast<std::uint8_t>(c);
    hash *= kPrime;
  }
  return hash;
}

OXGN_VRTX_NDAPI auto EnumerateStageEntries() noexcept
  -> std::span<const StageEntry>;
//! Null for a label the table does not list.
OXGN_VRTX_NDAPI auto FindStageEntry(std::uint64_t label_hash) noexcept
  -> const StageEntry*;

} // namespace oxygen::vortex::gpu_work
