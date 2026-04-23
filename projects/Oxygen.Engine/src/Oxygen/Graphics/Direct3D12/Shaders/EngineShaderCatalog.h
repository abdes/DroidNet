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
    .path="Vortex/Stages/Translucency/ForwardMesh_VS.hlsl",
    .entries=std::array { EntryPoint { .type=kVertex, .name="VS" } }
  },
  // Forward pass pixel shader: ALPHA_TEST permutation for normal rendering
  ShaderFileSpec {
    .path="Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 3> { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT", "SKIP_BRDF_LUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_DIRECT_LIGHTING_ONLY" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_IBL_ONLY" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_DIRECT_PLUS_IBL" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_DIRECT_LIGHTING_FULL" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_DIRECT_LIGHT_GATES" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_DIRECT_BRDF_CORE" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Wireframe pass pixel shader: ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Vortex/Stages/Translucency/ForwardWireframe_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 1> { "ALPHA_TEST" }
  },
  // Forward pass pixel shader: required DEBUG_LIGHT_HEATMAP plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_LIGHT_HEATMAP" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_DEPTH_SLICE plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_DEPTH_SLICE" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_CLUSTER_INDEX plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_CLUSTER_INDEX" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_IBL_SPECULAR plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_IBL_SPECULAR" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_IBL_RAW_SKY plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_IBL_RAW_SKY" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_IBL_IRRADIANCE plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_IBL_IRRADIANCE" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_IBL_FACE_INDEX plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_IBL_FACE_INDEX" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_BASE_COLOR plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_BASE_COLOR" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_UV0 plus ALPHA_TEST / HDR output
  // permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_UV0" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_OPACITY plus ALPHA_TEST / HDR
  // output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_OPACITY" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_WORLD_NORMALS plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_WORLD_NORMALS" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_ROUGHNESS plus ALPHA_TEST / HDR
  // output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_ROUGHNESS" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: required DEBUG_METALNESS plus ALPHA_TEST / HDR
  // output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_METALNESS" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  ShaderFileSpec {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 3>
      { "DEBUG_VIRTUAL_SHADOW_MASK", "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_SCENE_DEPTH_RAW" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_SCENE_DEPTH_LINEAR" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_SCENE_DEPTH_MISMATCH" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_MASKED_ALPHA_COVERAGE" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Vortex deferred-core seed shaders: initial depth/base requests only.
  ShaderFileSpec {
    .path="Vortex/Stages/DepthPrepass/DepthPrepass.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="DepthPrepassPS" }, EntryPoint { .type=kVertex, .name="DepthPrepassVS" } },
    .permutations=std::array<std::string_view, 2> { "HAS_VELOCITY", "ALPHA_TEST" }
  },
  ShaderFileSpec {
    .path="Vortex/Stages/Occlusion/ScreenHzbBuild.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexScreenHzbBuildCS" } }
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
  // VortexShadowDepthVS / VortexShadowDepthMaskedPS
  ShaderFileSpec {
    .path="Vortex/Services/Shadows/DirectionalShadowDepth.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="VortexShadowDepthMaskedPS" },
      EntryPoint { .type=kVertex, .name="VortexShadowDepthVS" } },
    .permutations=std::array<std::string_view, 1> { "ALPHA_TEST" }
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
    .path="Vortex/Services/Environment/Sky.hlsl",
    .entries=std::array {
      EntryPoint { .type=kVertex, .name="VortexSkyPassVS" },
      EntryPoint { .type=kPixel, .name="VortexSkyPassPS" },
      EntryPoint { .type=kCompute, .name="VortexIblIrradianceCS" },
      EntryPoint { .type=kCompute, .name="VortexIblPrefilterCS" } }
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/AtmosphereCompose.hlsl",
    .entries=std::array {
      EntryPoint { .type=kVertex, .name="VortexAtmosphereComposeVS" },
      EntryPoint { .type=kPixel, .name="VortexAtmosphereComposePS" } }
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/AtmosphereTransmittanceLut.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexAtmosphereTransmittanceLutCS" } }
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/AtmosphereMultiScatteringLut.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexAtmosphereMultiScatteringLutCS" } }
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/DistantSkyLightLut.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexDistantSkyLightLutCS" } }
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/AtmosphereSkyViewLut.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexAtmosphereSkyViewLutCS" } }
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/AtmosphereCameraAerialPerspective.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexAtmosphereCameraAerialPerspectiveCS" } }
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/Fog.hlsl",
    .entries=std::array {
      EntryPoint { .type=kVertex, .name="VortexFogPassVS" },
      EntryPoint { .type=kPixel, .name="VortexFogPassPS" } }
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/LocalFogVolumeTiledCulling.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexLocalFogVolumeTiledCullingCS" } }
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/LocalFogVolumeCompose.hlsl",
    .entries=std::array {
      EntryPoint { .type=kVertex, .name="VortexLocalFogVolumeComposeVS" },
      EntryPoint { .type=kPixel, .name="VortexLocalFogVolumeComposePS" } }
  },
  ShaderFileSpec {
    .path="Vortex/Services/PostProcess/Tonemap.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="VortexTonemapPS" },
      EntryPoint { .type=kVertex, .name="VortexTonemapVS" } }
  },
  ShaderFileSpec {
    .path="Vortex/Services/PostProcess/GroundGrid.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="VortexGroundGridPS" },
      EntryPoint { .type=kVertex, .name="VortexGroundGridVS" } },
    .permutations=std::array<std::string_view, 1> { "OXYGEN_HDR_OUTPUT" }
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
      EntryPoint { .type=kCompute, .name="ClearHistogram" },
      EntryPoint { .type=kCompute, .name="VortexExposureHistogramCS" },
      EntryPoint { .type=kCompute, .name="VortexExposureAverageCS" } }
  },
  // Light culling compute shader (final clustered analytic path)
  ShaderFileSpec {
    .path="Vortex/Services/Lighting/LightCulling.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
  // ImGui UI shaders (no permutations)
  ShaderFileSpec {
    .path="Ui/ImGui.hlsl",
    .entries=std::array { EntryPoint { .type=kVertex, .name="VS" }, EntryPoint { .type=kPixel, .name="PS" } }
  },
  // Compositing shaders (no permutations)
  ShaderFileSpec {
    .path="Vortex/RendererCore/Compositing/Compositing_VS.hlsl",
    .entries=std::array { EntryPoint { .type=kVertex, .name="VS" } }
  },
  ShaderFileSpec {
    .path="Vortex/RendererCore/Compositing/Compositing_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } }
  }
);
// clang-format on

// Compile-time verification:
// - ForwardMesh_VS: 1 entry
// - ForwardMesh_PS base: 8 (ALPHA_TEST x OXYGEN_HDR_OUTPUT x SKIP_BRDF_LUT)
// - ForwardMesh_PS DEBUG_DIRECT_* / DEBUG_IBL_*: 4 each (required debug define
//   x ALPHA_TEST x OXYGEN_HDR_OUTPUT)
// - ForwardWireframe_PS base: 2 (with/without ALPHA_TEST)
// - ForwardDebug_PS debug-required variants: 68
// - ForwardDebug_PS virtual-shadow-mask permutations: 8
// - VortexDepthPrepass: 8 (2 entries x HAS_VELOCITY x ALPHA_TEST)
// - VortexBasePassGBuffer: 8 (2 entries x HAS_VELOCITY x ALPHA_TEST)
// - VortexBasePassVelocityAux: 4 (2 entries x required MVWO define x
// ALPHA_TEST)
// - VortexBasePassVelocityMerge: 1
// - VortexBasePassDebugView: 7 (VS + 6 required debug PS variants)
// - VortexShadowDepth: 4 (2 entries x ALPHA_TEST)
// - VortexDeferredLightDirectional: 2 entries
// - VortexDeferredLightPoint: 2 entries
// - VortexDeferredLightSpot: 2 entries
// - VortexEnvironmentSky: 4 entries
// - VortexEnvironmentAtmosphere/Fog/LocalFog/Probe refresh families: 12 entries
// - VortexPostProcessTonemap: 2 entries
// - VortexPostProcessGroundGrid: 4 entries
// - VortexPostProcessBloomDownsample: 1 entry
// - VortexPostProcessBloomUpsample: 1 entry
// - VortexPostProcessExposure: 3 entries
// - LightCulling: 1
// - ImGui: 2 entries
// - Compositing: 2 entries
// Total: 116

} // namespace oxygen::graphics::d3d12
