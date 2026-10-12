//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <Oxygen/Vortex/Diagnostics/GpuWorkSummary.h>
#include <Oxygen/Vortex/Diagnostics/Internal/GpuWorkStageTable.h>

namespace oxygen::vortex::gpu_work {

namespace {
  using enum GpuWorkBucket;

  constexpr auto Entry(const std::string_view label, const GpuWorkBucket bucket,
    const std::string_view item_id, const std::string_view display_name)
    -> StageEntry
  {
    return StageEntry {
      .label = label,
      .bucket = bucket,
      .item_id = item_id,
      .display_name = display_name,
    };
  }

  // Several labels may share an item; nested entries roll up into the
  // outermost one, so listing a sub-scope never double counts.
  constexpr auto kEntries = std::array {
    Entry(kOcclusionPhase1Label, kVisibility, "visibility.occlusion",
      "Occlusion culling"),
    Entry(kOcclusionPhase2Label, kVisibility, "visibility.occlusion",
      "Occlusion culling"),
    Entry(kOcclusionHzbLabel, kVisibility, "visibility.occlusion_hzb",
      "Occlusion HZB"),
    Entry(kScreenHzbLabel, kVisibility, "visibility.screen_hzb", "Screen HZB"),
    Entry(kDepthPrepassLabel, kDepthPrepass, "depth.prepass", "Depth prepass"),
    Entry(kShadowDepthsLabel, kShadows, "shadows.depths", "Shadow depths"),
    Entry(kShadowReceiverFitLabel, kShadows, "shadows.receiver_fit",
      "Receiver fit"),
    Entry(kContactShadowCasterDepthLabel, kShadows, "shadows.contact_casters",
      "Contact shadow casters"),
    Entry(kBasePassLabel, kBasePass, "base.main", "Base pass"),
    Entry(
      kSpatialLightGridLabel, kLighting, "lighting.light_grid", "Light grid"),
    Entry(kDeferredLightingLabel, kLighting, "lighting.deferred",
      "Deferred lights"),
    Entry(kIndirectLightingLabel, kLighting, "lighting.indirect",
      "Indirect lighting"),
    Entry(kLocalFogTiledCullingLabel, kEnvironment,
      "environment.local_fog_culling", "Local fog culling"),
    Entry(kVolumetricFogLabel, kEnvironment, "environment.volumetric_fog",
      "Volumetric fog"),
    Entry(
      kAtmosphereLabel, kEnvironment, "environment.atmosphere", "Atmosphere"),
    Entry(kFogLabel, kEnvironment, "environment.fog", "Height fog"),
    Entry(kLocalFogLabel, kEnvironment, "environment.local_fog", "Local fog"),
    Entry(kSkyLabel, kEnvironment, "environment.sky", "Sky"),
    Entry(kAtmosphereTransmittanceLutLabel, kEnvironment,
      "environment.atmosphere_luts", "Atmosphere LUTs"),
    Entry(kAtmosphereMultiScatteringLutLabel, kEnvironment,
      "environment.atmosphere_luts", "Atmosphere LUTs"),
    Entry(kAtmosphereSkyViewLutLabel, kEnvironment,
      "environment.atmosphere_luts", "Atmosphere LUTs"),
    Entry(kAtmosphereCameraAerialPerspectiveLabel, kEnvironment,
      "environment.atmosphere_luts", "Atmosphere LUTs"),
    Entry(kDistantSkyLightLutLabel, kEnvironment, "environment.atmosphere_luts",
      "Atmosphere LUTs"),
    Entry(kTranslucencyLabel, kTranslucency, "translucency.forward",
      "Translucency"),
    Entry(kResolveSceneColorLabel, kPostProcess, "post.resolve",
      "Scene colour resolve"),
    Entry(
      kExposureFrameResolveLabel, kPostProcess, "post.exposure", "Exposure"),
    Entry(
      kExposureFrameFallbackLabel, kPostProcess, "post.exposure", "Exposure"),
    Entry(kExposureSceneRangeLabel, kPostProcess, "post.exposure", "Exposure"),
    Entry(
      kExposureOpaqueApErrorLabel, kPostProcess, "post.exposure", "Exposure"),
    Entry(
      kExposureFilterGradientsLabel, kPostProcess, "post.exposure", "Exposure"),
    Entry(
      kExposureQualifyProductsLabel, kPostProcess, "post.exposure", "Exposure"),
    Entry(kExposureClearSuitabilityLabel, kPostProcess, "post.exposure",
      "Exposure"),
    Entry(kExposureGatherSuitabilityMaximumLabel, kPostProcess, "post.exposure",
      "Exposure"),
    Entry(kExposureSelectSuitabilityCandidateLabel, kPostProcess,
      "post.exposure", "Exposure"),
    Entry(kExposureCheckSuitabilityProductLabel, kPostProcess, "post.exposure",
      "Exposure"),
    Entry(kExposureFinalizeSuitabilityLabel, kPostProcess, "post.exposure",
      "Exposure"),
    Entry(kExposureConvertSceneColorLabel, kPostProcess, "post.exposure",
      "Exposure"),
    Entry(
      kExposureMeterAndAdaptLabel, kPostProcess, "post.exposure", "Exposure"),
    Entry(kExposureHistogramLabel, kPostProcess, "post.exposure", "Exposure"),
    Entry(kExposureSolveLabel, kPostProcess, "post.exposure", "Exposure"),
    Entry(
      kExposureStatusReadbackLabel, kPostProcess, "post.exposure", "Exposure"),
    Entry(kTonemapLabel, kPostProcess, "post.tonemap", "Tonemap"),
    Entry(kPostRenderCleanupLabel, kPostProcess, "post.cleanup",
      "Post-render cleanup"),
    Entry(kGroundGridLabel, kOverlays, "overlays.grid", "Ground grid"),
    Entry(kSelectionOutlineLabel, kOverlays, "overlays.selection",
      "Selection outline"),
    Entry(kViewOverlayLabel, kOverlays, "overlays.view", "View overlay"),
    Entry(kWireframeOverlayLabel, kOverlays, "overlays.wireframe",
      "Wireframe overlay"),
    Entry(kViewPickLabel, kOverlays, "overlays.pick", "View pick"),
    Entry(
      kDebugVisualizationLabel, kOverlays, "overlays.debug_view", "Debug view"),
    Entry(kGpuDebugPrimitivesClearLabel, kOverlays, "overlays.debug_primitives",
      "Debug primitives"),
    Entry(kGpuDebugPrimitivesDrawLabel, kOverlays, "overlays.debug_primitives",
      "Debug primitives"),
    Entry(
      kCompositingTaskLabel, kComposition, "composition.task", "Compositing"),
    Entry(kSurfaceOverlayLabel, kComposition, "composition.surface_overlay",
      "Surface overlay"),
    Entry(kSurfaceLayerLabel, kComposition, "composition.surface_layer",
      "Surface layers"),
    Entry(kAuxViewConsumeLabel, kComposition, "composition.aux_view",
      "Auxiliary view copy"),
    Entry(kIblBrdfUploadLabel, kBackground, "background.ibl", "IBL"),
    Entry(kIblProcessLabel, kBackground, "background.ibl", "IBL"),
    Entry(kIblMetadataLabel, kBackground, "background.ibl", "IBL"),
    Entry(kIblAtmosphereTransmittanceLutLabel, kBackground, "background.ibl",
      "IBL"),
    Entry(kIblAtmosphereMultiScatteringLutLabel, kBackground, "background.ibl",
      "IBL"),
    Entry(kIblAtmosphereSkyViewLutLabel, kBackground, "background.ibl", "IBL"),
    Entry(kIblDistantSkyLightLutLabel, kBackground, "background.ibl", "IBL"),
  };

  constexpr auto kHashes = [] {
    auto hashes = std::array<std::uint64_t, kEntries.size()> {};
    for (std::size_t i = 0; i < kEntries.size(); ++i) {
      hashes.at(i) = HashScopeLabel(kEntries.at(i).label);
    }
    return hashes;
  }();
} // namespace

auto EnumerateStageEntries() noexcept -> std::span<const StageEntry>
{
  return kEntries;
}

auto FindStageEntry(const std::uint64_t label_hash) noexcept
  -> const StageEntry*
{
  for (std::size_t i = 0; i < kHashes.size(); ++i) {
    if (kHashes.at(i) == label_hash) {
      return &kEntries.at(i);
    }
  }
  return nullptr;
}

} // namespace oxygen::vortex::gpu_work
