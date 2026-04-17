//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! @file EngineShaderCatalog.h
//! @brief Declares all engine shaders to be compiled and baked into
//! shaders.bin.

#pragma once

#include <Oxygen/Graphics/Direct3D12/Shaders/ShaderCatalogBuilder.h>

namespace oxygen::graphics::d3d12 {

using enum ShaderType;

//! Complete catalog of engine shaders (computed at compile time).
//!
//! Each ShaderFileSpec declares:
//! - The shader file path (relative to engine shaders directory)
//! - The entry points with their shader types
//! - The permutations (boolean defines) that apply
//!
//! The generator expands each spec into all 2^N permutation variants.
//! For example, {"ALPHA_TEST"} produces 2 variants per entry point:
//! - Base (no defines)
//! - ALPHA_TEST=1
//!
//! Benefits of consteval generation:
//! - Zero runtime cost (all computation happens at compile time)
//! - Eliminates manual duplication for permutations
//! - Compile-time count verification (array size is deduced)
//! - Makes it obvious which permutations apply to which shaders
// Debug mode defines for ForwardMesh_PS.hlsl
// These create shader variants for different visualization modes.
// Each debug define enables a specific visualization:
//   (none): Normal PBR rendering
//   DEBUG_LIGHT_HEATMAP: Light count heat map
//   DEBUG_DEPTH_SLICE: Depth slice visualization
//   DEBUG_CLUSTER_INDEX: Cluster index checkerboard
//   DEBUG_BASE_COLOR: Base color/albedo visualization
//   DEBUG_UV0: UV0 visualization
//   DEBUG_OPACITY: Opacity visualization
//   DEBUG_WORLD_NORMALS: World-space normals visualization
//   DEBUG_ROUGHNESS: Roughness visualization
//   DEBUG_METALNESS: Metalness visualization
//   DEBUG_DIRECT_LIGHTING_ONLY: Forward mesh direct-light term only
//   DEBUG_IBL_ONLY: Forward mesh IBL term only
//   DEBUG_DIRECT_PLUS_IBL: Forward mesh direct + IBL terms only
//   DEBUG_DIRECT_LIGHTING_FULL: Full forward direct-light term only
//   DEBUG_DIRECT_LIGHT_GATES: R=shadow visibility, G=sun transmittance
//   DEBUG_DIRECT_BRDF_CORE: Ungated directional BRDF core only
//   DEBUG_VIRTUAL_SHADOW_MASK: Stage 15 VSM screen-space shadow mask
//   DEBUG_SCENE_DEPTH_RAW: Published scene depth in reversed-Z space
//   DEBUG_SCENE_DEPTH_LINEAR: Published scene depth reconstructed to eye depth
//   DEBUG_SCENE_DEPTH_MISMATCH: Shading depth disagreement vs pre-pass depth
//   DEBUG_MASKED_ALPHA_COVERAGE: Masked alpha pass/fail classification

// clang-format off
inline constexpr auto kEngineShaders = GenerateCatalog(
  // Forward pass vertex shader (shared by all modes)
  ShaderFileSpec {
    .path="Forward/ForwardMesh_VS.hlsl",
    .entries=std::array { EntryPoint { .type=kVertex, .name="VS" } }
  },
  // Forward pass pixel shader: ALPHA_TEST permutation for normal rendering
  ShaderFileSpec {
    .path="Forward/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 3> { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT", "SKIP_BRDF_LUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_DIRECT_LIGHTING_ONLY" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_IBL_ONLY" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_DIRECT_PLUS_IBL" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_DIRECT_LIGHTING_FULL" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_DIRECT_LIGHT_GATES" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_DIRECT_BRDF_CORE" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Wireframe pass pixel shader: ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Forward/ForwardWireframe_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 1> { "ALPHA_TEST" }
  },
  // Forward pass pixel shader: required DEBUG_LIGHT_HEATMAP plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_LIGHT_HEATMAP" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_DEPTH_SLICE plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_DEPTH_SLICE" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_CLUSTER_INDEX plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_CLUSTER_INDEX" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_IBL_SPECULAR plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_IBL_SPECULAR" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_IBL_RAW_SKY plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_IBL_RAW_SKY" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_IBL_IRRADIANCE plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_IBL_IRRADIANCE" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_IBL_FACE_INDEX plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_IBL_FACE_INDEX" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_BASE_COLOR plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_BASE_COLOR" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_UV0 plus ALPHA_TEST / HDR output
  // permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_UV0" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_OPACITY plus ALPHA_TEST / HDR
  // output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_OPACITY" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_WORLD_NORMALS plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_WORLD_NORMALS" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_ROUGHNESS plus ALPHA_TEST / HDR
  // output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_ROUGHNESS" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_METALNESS plus ALPHA_TEST / HDR
  // output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_METALNESS" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  ShaderFileSpec {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 3>
      { "DEBUG_VIRTUAL_SHADOW_MASK", "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_SCENE_DEPTH_RAW" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_SCENE_DEPTH_LINEAR" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_SCENE_DEPTH_MISMATCH" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_MASKED_ALPHA_COVERAGE" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Depth pre-pass: VS and PS with alpha-test permutation
  ShaderFileSpec {
    .path="Depth/DepthPrePass.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" }, EntryPoint { .type=kVertex, .name="VS" } },
    .permutations=std::array<std::string_view, 1> { "ALPHA_TEST" }
  },
  // Vortex deferred-core seed shaders: initial depth/base requests only.
  ShaderFileSpec {
    .path="Vortex/Stages/DepthPrepass/DepthPrepass.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="DepthPrepassPS" }, EntryPoint { .type=kVertex, .name="DepthPrepassVS" } },
    .permutations=std::array<std::string_view, 2> { "HAS_VELOCITY", "ALPHA_TEST" }
  },
  ShaderFileSpec {
    .path="Vortex/Stages/BasePass/BasePassGBuffer.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassGBufferPS" }, EntryPoint { .type=kVertex, .name="BasePassGBufferVS" } },
    .permutations=std::array<std::string_view, 2> { "HAS_VELOCITY", "ALPHA_TEST" }
  },
  RequiredDefineShaderFileSpec<2, 1, 1> {
    .path="Vortex/Stages/BasePass/BasePassVelocityAux.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="BasePassVelocityAuxPS" },
      EntryPoint { .type=kVertex, .name="BasePassVelocityAuxVS" } },
    .required_defines=std::array<std::string_view, 1>
      { "USES_MOTION_VECTOR_WORLD_OFFSET" },
    .permutations=std::array<std::string_view, 1> { "ALPHA_TEST" }
  },
  ShaderFileSpec {
    .path="Vortex/Stages/BasePass/BasePassVelocityMerge.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="BasePassVelocityMergeCS" } }
  },
  ShaderFileSpec {
    .path="Vortex/Stages/BasePass/BasePassDebugView.hlsl",
    .entries=std::array { EntryPoint { .type=kVertex, .name="BasePassDebugViewVS" } }
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Stages/BasePass/BasePassDebugView.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassDebugViewPS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_BASE_COLOR" }
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Stages/BasePass/BasePassDebugView.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassDebugViewPS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_WORLD_NORMALS" }
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Stages/BasePass/BasePassDebugView.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassDebugViewPS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_ROUGHNESS" }
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Stages/BasePass/BasePassDebugView.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassDebugViewPS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_METALNESS" }
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Stages/BasePass/BasePassDebugView.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassDebugViewPS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_SCENE_DEPTH_RAW" }
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Stages/BasePass/BasePassDebugView.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassDebugViewPS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_SCENE_DEPTH_LINEAR" }
  },
  // VortexDeferredLightDirectionalVS / VortexDeferredLightDirectionalPS
  ShaderFileSpec {
    .path="Vortex/Services/Lighting/DeferredLightDirectional.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="DeferredLightDirectionalPS" }, EntryPoint { .type=kVertex, .name="DeferredLightDirectionalVS" } }
  },
  // VortexDeferredLightPointVS / VortexDeferredLightPointPS
  ShaderFileSpec {
    .path="Vortex/Services/Lighting/DeferredLightPoint.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="DeferredLightPointPS" },
      EntryPoint { .type=kVertex, .name="DeferredLightPointVS" } }
  },
  // VortexDeferredLightSpotVS / VortexDeferredLightSpotPS
  ShaderFileSpec {
    .path="Vortex/Services/Lighting/DeferredLightSpot.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="DeferredLightSpotPS" },
      EntryPoint { .type=kVertex, .name="DeferredLightSpotVS" } }
  },
  ShaderFileSpec {
    .path="Vortex/Services/PostProcess/Tonemap.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="VortexTonemapPS" },
      EntryPoint { .type=kVertex, .name="VortexTonemapVS" } }
  },
  ShaderFileSpec {
    .path="Vortex/Services/PostProcess/BloomDownsample.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="VortexBloomDownsamplePS" } }
  },
  ShaderFileSpec {
    .path="Vortex/Services/PostProcess/BloomUpsample.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="VortexBloomUpsamplePS" } }
  },
  ShaderFileSpec {
    .path="Vortex/Services/PostProcess/Exposure.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexExposureHistogramCS" } }
  },
  // Light culling compute shader (final clustered analytic path)
  ShaderFileSpec {
    .path="Lighting/LightCulling.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  // Renderer-level screen-space HZB build shader
  ShaderFileSpec {
    .path="Renderer/ScreenHzbBuild.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  ShaderFileSpec {
    .path="Renderer/ConventionalShadowReceiverAnalysis.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="CS_Clear" },
      EntryPoint { .type=kCompute, .name="CS_Analyze" },
      EntryPoint { .type=kCompute, .name="CS_Finalize" }
    }
  },
  ShaderFileSpec {
    .path="Renderer/ConventionalShadowReceiverMask.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="CS_ClearMasks" },
      EntryPoint { .type=kCompute, .name="CS_Analyze" },
      EntryPoint { .type=kCompute, .name="CS_DilateMasks" },
      EntryPoint { .type=kCompute, .name="CS_BuildHierarchy" },
      EntryPoint { .type=kCompute, .name="CS_Finalize" }
    }
  },
  ShaderFileSpec {
    .path="Renderer/ConventionalShadowCasterCulling.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  ShaderFileSpec {
    .path="Renderer/Vsm/VsmInstanceCulling.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  ShaderFileSpec {
    .path="Renderer/Vsm/VsmPublishRasterResults.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  // Sky atmosphere LUT compute shaders (no permutations)
  ShaderFileSpec {
    .path="Atmosphere/TransmittanceLut_CS.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  // Sky-view LUT compute uses runtime blue-noise slot toggling (no permutation).
  ShaderFileSpec {
    .path="Atmosphere/SkyViewLut_CS.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  ShaderFileSpec {
    .path="Atmosphere/MultiScatLut_CS.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  ShaderFileSpec {
    .path="Atmosphere/SkyIrradianceLut_CS.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  ShaderFileSpec {
    .path="Atmosphere/CameraVolumeLut_CS.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  // Sky sphere shaders (no permutations)
  ShaderFileSpec {
    .path="Atmosphere/SkySphere_VS.hlsl",
    .entries=std::array { EntryPoint { .type=kVertex, .name="VS" } }
  },
  ShaderFileSpec {
    .path="Atmosphere/SkySphere_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 1> { "OXYGEN_HDR_OUTPUT" }
  },
  // Sky capture shaders (no permutations)
  ShaderFileSpec {
    .path="Atmosphere/SkyCapture_VS.hlsl",
    .entries=std::array { EntryPoint { .type=kVertex, .name="VS" } }
  },
  ShaderFileSpec {
    .path="Atmosphere/SkyCapture_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } }
  },
  // IBL filtering compute shaders (no permutations)
  ShaderFileSpec {
    .path="Lighting/IblFiltering.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS_IrradianceConvolution" },
      EntryPoint { .type=kCompute, .name="CS_SpecularPrefilter" } }
  },
  // GPU Debugging shaders
  ShaderFileSpec {
    .path="Renderer/GpuDebugClear.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  ShaderFileSpec {
    .path="Renderer/GpuDebugDraw.hlsl",
    .entries=std::array { EntryPoint { .type=kVertex, .name="VS" }, EntryPoint { .type=kPixel, .name="PS" } }
  },
  // Ground grid shaders (no permutations)
  ShaderFileSpec {
    .path="Renderer/GroundGrid_VS.hlsl",
    .entries=std::array { EntryPoint { .type=kVertex, .name="VS" } }
  },
  ShaderFileSpec {
    .path="Renderer/GroundGrid_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 1> { "OXYGEN_HDR_OUTPUT" }
  },
  // VSM runtime page-request generation shader
  ShaderFileSpec {
    .path="Renderer/Vsm/VsmPageRequestGenerator.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  ShaderFileSpec {
    .path="Renderer/Vsm/VsmInvalidation.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  ShaderFileSpec {
    .path="Renderer/Vsm/VsmStaticDynamicMerge.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  ShaderFileSpec {
    .path="Renderer/Vsm/VsmHzbBuild.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="CS_SelectPages" },
      EntryPoint { .type=kCompute, .name="CS_PrepareDispatchArgs" },
      EntryPoint { .type=kCompute, .name="CS_ClearScratchRect" },
      EntryPoint { .type=kCompute, .name="CS_BuildPerPage" },
      EntryPoint { .type=kCompute, .name="CS_BuildTopLevels" }
    }
  },
  ShaderFileSpec {
    .path="Renderer/Vsm/VsmDirectionalProjection.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="CS_ClearShadowMask" },
      EntryPoint { .type=kCompute, .name="CS_ProjectDirectional" }
    }
  },
  ShaderFileSpec {
    .path="Renderer/Vsm/VsmLocalLightProjectionPerLight.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="CS_ProjectLocalLights" }
    }
  },
  // VSM runtime page-management shaders (stage 6-8)
  ShaderFileSpec {
    .path="Renderer/Vsm/VsmPageReuse.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  ShaderFileSpec {
    .path="Renderer/Vsm/VsmPackAvailablePages.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  ShaderFileSpec {
    .path="Renderer/Vsm/VsmAllocateNewPages.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  ShaderFileSpec {
    .path="Renderer/Vsm/VsmGenerateHierarchicalFlags.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  ShaderFileSpec {
    .path="Renderer/Vsm/VsmPropagateMappedMips.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  // ImGui UI shaders (no permutations)
  ShaderFileSpec {
    .path="Ui/ImGui.hlsl",
    .entries=std::array { EntryPoint { .type=kVertex, .name="VS" }, EntryPoint { .type=kPixel, .name="PS" } }
  },
  // Compositing shaders (no permutations)
  ShaderFileSpec {
    .path="Compositing/Compositing_VS.hlsl",
    .entries=std::array { EntryPoint { .type=kVertex, .name="VS" } }
  },
  ShaderFileSpec {
    .path="Compositing/Compositing_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } }
  },
  // ToneMap shaders (no permutations)
  ShaderFileSpec {
    .path="Compositing/ToneMap_VS.hlsl",
    .entries=std::array { EntryPoint { .type=kVertex, .name="VS" } }
  },
  ShaderFileSpec {
    .path="Compositing/ToneMap_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } }
  },
  // Auto Exposure Histogram shaders (no permutations)
  ShaderFileSpec {
    .path="Compositing/AutoExposure_Histogram_CS.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" },
      EntryPoint { .type=kCompute, .name="ClearHistogram" } }
  },
  ShaderFileSpec {
    .path="Compositing/AutoExposure_Average_CS.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  }
);
// clang-format on

// Compile-time verification:
// - ForwardMesh_VS: 1 entry
// - ForwardMesh_PS base: 8 (ALPHA_TEST x OXYGEN_HDR_OUTPUT x SKIP_BRDF_LUT)
// - ForwardMesh_PS DEBUG_DIRECT_* / DEBUG_IBL_*: 4 each (required debug define
//   x ALPHA_TEST x OXYGEN_HDR_OUTPUT)
// - ForwardWireframe_PS base: 2 (with/without ALPHA_TEST)
// - ForwardDebug_PS DEBUG_*: 4 each (required debug define x ALPHA_TEST x
//   OXYGEN_HDR_OUTPUT)
// - DepthPrePass: 4 (2 entries x 2 permutations)
// - VortexDepthPrepass: 8 (2 entries x HAS_VELOCITY x ALPHA_TEST)
// - VortexBasePassGBuffer: 8 (2 entries x HAS_VELOCITY x ALPHA_TEST)
// - VortexBasePassVelocityAux: 4 (2 entries x required MVWO define x ALPHA_TEST)
// - VortexBasePassVelocityMerge: 1
// - VortexBasePassDebugView: 7 (VS + 6 required debug PS variants)
// - VortexDeferredLightDirectional: 2 entries
// - VortexDeferredLightPoint: 3 entries
// - VortexDeferredLightSpot: 3 entries
// - VortexPostProcessTonemap: 2 entries
// - VortexPostProcessBloomDownsample: 1 entry
// - VortexPostProcessBloomUpsample: 1 entry
// - VortexPostProcessExposure: 1 entry
// - LightCulling: 1
// - TransmittanceLut_CS: 1 entry
// - SkyViewLut_CS: 1 entry
// - MultiScatLut_CS: 1 entry
// - SkyIrradianceLut_CS: 1 entry
// - CameraVolumeLut_CS: 1 entry
// - SkySphere_VS: 1 entry
// - SkySphere_PS: 2 entries
// - SkyCapture_VS/PS: 2 entries
// - IblFiltering: 2 entries
// - GpuDebugClear: 1 entry
// - GpuDebugDraw: 2 entries
// - ImGui: 2 entries
// - Compositing: 2 entries
// - ToneMap: 2 entries
// - AutoExposure_Histogram_CS: 2 entries
// - AutoExposure_Average_CS: 1 entry
// Total: 161

} // namespace oxygen::graphics::d3d12
