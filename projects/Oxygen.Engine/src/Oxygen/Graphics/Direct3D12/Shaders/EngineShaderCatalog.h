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
using enum ShaderViewBinding;

//! Complete catalog of engine shaders (computed at compile time).
//!
//! Each ShaderFileSpec declares:
//! - The shader file path (relative to engine shaders directory)
//! - The entry points with their shader types, and `.view_binding=kCamera` on
//!   entries that read the camera view constants (b1)
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
//   DEBUG_WORLD_NORMALS: World-space normals visualization
//   DEBUG_ROUGHNESS: Roughness visualization
//   DEBUG_METALNESS: Metalness visualization
//   DEBUG_DIRECT_LIGHTING_ONLY: Forward mesh direct-light term only
//   DEBUG_IBL_ONLY: IBL term only
//   DEBUG_DIRECT_PLUS_IBL: Direct + IBL terms only
//   DEBUG_DIRECT_LIGHTING_FULL: Full forward direct-light term only
//   DEBUG_DIRECT_LIGHT_GATES: R=shadow visibility, G=sun transmittance
//   DEBUG_DIRECT_BRDF_CORE: Ungated directional BRDF core only
//   DEBUG_VIRTUAL_SHADOW_MASK: Stage 15 VSM screen-space shadow mask
//   DEBUG_DIRECTIONAL_SHADOW_MASK: Deferred conventional directional CSM mask
//   DEBUG_SCENE_DEPTH_RAW: Published scene depth in reversed-Z space
//   DEBUG_SCENE_DEPTH_LINEAR: Published scene depth reconstructed to eye depth
//   DEBUG_SCENE_DEPTH_MISMATCH: Shading depth disagreement vs pre-pass depth
//   DEBUG_MASKED_ALPHA_COVERAGE: Masked alpha pass/fail classification

// clang-format off
inline constexpr auto kEngineShaders = GenerateCatalog(
  // Forward pass vertex shader (shared by all modes)
  ShaderFileSpec {
    .path="Vortex/Stages/Translucency/ForwardMesh_VS.hlsl",
    .entries=std::array { EntryPoint { .type=kVertex, .name="VS", .view_binding=kCamera } },
  },
  // Forward pass pixel shader: ALPHA_TEST permutation for normal rendering
  ShaderFileSpec {
    .path="Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .permutations=std::array<std::string_view, 4> { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT", "OXYGEN_OPAQUE_OUTPUT", "OXYGEN_DEPTH_COMPLETE" },
  },
  RequiredDefineShaderFileSpec<1, 3, 1> {
    .path="Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="ValidateRadiancePS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 3>
      { "OXYGEN_HDR_OUTPUT", "OXYGEN_OPAQUE_OUTPUT", "OXYGEN_DEPTH_COMPLETE" },
    .permutations=std::array<std::string_view, 1> { "ALPHA_TEST" },
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_DIRECT_LIGHTING_ONLY" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_IBL_ONLY" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_DIRECT_PLUS_IBL" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_DIRECT_LIGHTING_FULL" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_DIRECT_LIGHT_GATES" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1>
      { "DEBUG_DIRECT_BRDF_CORE" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  // Wireframe pass pixel shader: ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Vortex/Stages/Translucency/ForwardWireframe_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .permutations=std::array<std::string_view, 1> { "ALPHA_TEST" },
  },
  // Forward pass pixel shader: required DEBUG_LIGHT_HEATMAP plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_LIGHT_HEATMAP" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  // Forward pass pixel shader: required DEBUG_DEPTH_SLICE plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_DEPTH_SLICE" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  // Forward pass pixel shader: required DEBUG_CLUSTER_INDEX plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_CLUSTER_INDEX" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  // Forward pass pixel shader: required DEBUG_IBL_SPECULAR plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_IBL_SPECULAR" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  // Forward pass pixel shader: required DEBUG_IBL_RAW_SKY plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_IBL_RAW_SKY" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  // Forward pass pixel shader: required DEBUG_IBL_IRRADIANCE plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_IBL_IRRADIANCE" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  // Forward pass pixel shader: required DEBUG_IBL_FACE_INDEX plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_IBL_FACE_INDEX" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  // Forward pass pixel shader: required DEBUG_BASE_COLOR plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_BASE_COLOR" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  // Forward pass pixel shader: required DEBUG_WORLD_NORMALS plus ALPHA_TEST /
  // HDR output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_WORLD_NORMALS" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  // Forward pass pixel shader: required DEBUG_ROUGHNESS plus ALPHA_TEST / HDR
  // output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_ROUGHNESS" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  // Forward pass pixel shader: required DEBUG_METALNESS plus ALPHA_TEST / HDR
  // output permutations.
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_METALNESS" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  ShaderFileSpec {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .permutations=std::array<std::string_view, 3>
      { "DEBUG_VIRTUAL_SHADOW_MASK", "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_SCENE_DEPTH_RAW" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_SCENE_DEPTH_LINEAR" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_SCENE_DEPTH_MISMATCH" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/Translucency/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_MASKED_ALPHA_COVERAGE" },
    .permutations=std::array<std::string_view, 2>
      { "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" },
  },
  // Vortex deferred-core seed shaders: initial depth/base requests only.
  ShaderFileSpec {
    .path="Vortex/Stages/DepthPrepass/DepthPrepass.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="DepthPrepassPS", .view_binding=kCamera }, EntryPoint { .type=kVertex, .name="DepthPrepassVS", .view_binding=kCamera } },
    .permutations=std::array<std::string_view, 2> { "HAS_VELOCITY", "ALPHA_TEST" },
  },
  ShaderFileSpec {
    .path="Vortex/Stages/Occlusion/ScreenHzbBuild.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexScreenHzbBuildCS" }, },
  },
  ShaderFileSpec {
    .path="Vortex/Stages/Occlusion/OcclusionCull.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexOcclusionStatsClearCS" },
      EntryPoint { .type=kCompute, .name="VortexOcclusionPhase1CS" },
      EntryPoint { .type=kCompute, .name="VortexOcclusionPhase2CS" }, },
  },
  ShaderFileSpec {
    .path="Vortex/Stages/Occlusion/ListCompaction.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexListCompactionScanCS" },
      EntryPoint { .type=kCompute, .name="VortexListCompactionGroupsCS" },
      EntryPoint { .type=kCompute, .name="VortexListCompactionScatterCS" }, },
  },
  ShaderFileSpec {
    .path="Vortex/Stages/BasePass/BasePassGBuffer.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassGBufferPS", .view_binding=kCamera }, EntryPoint { .type=kVertex, .name="BasePassGBufferVS", .view_binding=kCamera } },
    .permutations=std::array<std::string_view, 3> { "HAS_VELOCITY", "ALPHA_TEST", "OXYGEN_DEPTH_COMPLETE" },
  },
  RequiredDefineShaderFileSpec<1, 1, 2> {
    .path="Vortex/Stages/BasePass/BasePassGBuffer.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassValidateRadiancePS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "OXYGEN_DEPTH_COMPLETE" },
    .permutations=std::array<std::string_view, 2> { "HAS_VELOCITY", "ALPHA_TEST" },
  },
  ShaderFileSpec {
    .path="Vortex/Stages/BasePass/BasePassWireframe.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassWireframePS", .view_binding=kCamera }, EntryPoint { .type=kVertex, .name="BasePassGBufferVS", .view_binding=kCamera } },
    .permutations=std::array<std::string_view, 1> { "ALPHA_TEST" },
  },
  RequiredDefineShaderFileSpec<2, 1, 1> {
    .path="Vortex/Stages/BasePass/BasePassVelocityAux.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="BasePassVelocityAuxPS", .view_binding=kCamera },
      EntryPoint { .type=kVertex, .name="BasePassVelocityAuxVS", .view_binding=kCamera }, },
    .required_defines=std::array<std::string_view, 1>
      { "USES_MOTION_VECTOR_WORLD_OFFSET" },
    .permutations=std::array<std::string_view, 1> { "ALPHA_TEST" },
  },
  ShaderFileSpec {
    .path="Vortex/Stages/BasePass/BasePassVelocityMerge.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="BasePassVelocityMergeCS", .view_binding=kCamera } },
  },
  ShaderFileSpec {
    .path="Vortex/Stages/BasePass/BasePassDebugView.hlsl",
    .entries=std::array { EntryPoint { .type=kVertex, .name="BasePassDebugViewVS", .view_binding=kCamera } },
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Stages/BasePass/BasePassDebugView.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassDebugViewPS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_BASE_COLOR" },
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Stages/BasePass/BasePassDebugView.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassDebugViewPS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_WORLD_NORMALS" },
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Stages/BasePass/BasePassDebugView.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassDebugViewPS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_ROUGHNESS" },
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Stages/BasePass/BasePassDebugView.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassDebugViewPS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_METALNESS" },
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Stages/BasePass/BasePassDebugView.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassDebugViewPS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_DIRECTIONAL_SHADOW_MASK" },
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Stages/BasePass/BasePassDebugView.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassDebugViewPS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_SCENE_DEPTH_RAW" },
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Stages/BasePass/BasePassDebugView.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassDebugViewPS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_SCENE_DEPTH_LINEAR" },
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Stages/BasePass/BasePassDebugView.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="BasePassDebugViewPS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_MASKED_ALPHA_COVERAGE" },
  },
  // VortexShadowDepthVS / VortexShadowDepthMaskedPS
  ShaderFileSpec {
    .path="Vortex/Services/Shadows/DirectionalShadowDepth.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="VortexShadowDepthMaskedPS" },
      EntryPoint { .type=kVertex, .name="VortexShadowDepthVS" }, },
    .permutations=std::array<std::string_view, 2> { "ALPHA_TEST", "CUBE_SHADOW" },
  },
  // VortexContactShadowCasterDepthVS / VortexContactShadowCasterDepthPS
  ShaderFileSpec {
    .path="Vortex/Services/Shadows/ContactShadowCasterDepth.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="VortexContactShadowCasterDepthPS" },
      EntryPoint { .type=kVertex, .name="VortexContactShadowCasterDepthVS" }, },
    .permutations=std::array<std::string_view, 1> { "ALPHA_TEST" },
  },
  // VortexDeferredLightDirectionalVS / VortexDeferredLightDirectionalPS
  ShaderFileSpec {
    .path="Vortex/Services/Lighting/DeferredLightDirectional.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="DeferredLightDirectionalPS", .view_binding=kCamera }, EntryPoint { .type=kVertex, .name="DeferredLightDirectionalVS", .view_binding=kCamera } },
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Services/Lighting/DeferredLightDirectional.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="DeferredLightDirectionalPS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_DIRECT_LIGHTING_ONLY" },
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Services/Lighting/DeferredLightDirectional.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="DeferredLightDirectionalPS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_IBL_ONLY" },
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Services/Lighting/DeferredLightDirectional.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="DeferredLightDirectionalPS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_DIRECT_PLUS_IBL" },
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Services/Lighting/DeferredLightDirectional.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="DeferredLightDirectionalPS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_DIRECT_LIGHTING_FULL" },
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Services/Lighting/DeferredLightDirectional.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="DeferredLightDirectionalPS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_DIRECT_LIGHT_GATES" },
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Services/Lighting/DeferredLightDirectional.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="DeferredLightDirectionalPS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "DEBUG_DIRECT_BRDF_CORE" },
  },
  // VortexDeferredLightPointVS / VortexDeferredLightPointPS
  ShaderFileSpec {
    .path="Vortex/Services/Lighting/SpatialLightGrid.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="SpatialLightGridCS" }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Lighting/DeferredLightPoint.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="DeferredLightPointPS", .view_binding=kCamera },
      EntryPoint { .type=kVertex, .name="DeferredLightPointVS", .view_binding=kCamera }, },
  },
  RequiredDefineShaderFileSpec<1, 1> {
    .path="Vortex/Services/Lighting/DeferredLightPoint.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="DeferredLightPointPS", .view_binding=kCamera } },
    .required_defines=std::array<std::string_view, 1> { "PUNCTUAL_POINT" },
  },
  // VortexDeferredLightSpotVS / VortexDeferredLightSpotPS
  ShaderFileSpec {
    .path="Vortex/Services/Lighting/DeferredLightSpot.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="DeferredLightSpotPS", .view_binding=kCamera },
      EntryPoint { .type=kVertex, .name="DeferredLightSpotVS", .view_binding=kCamera }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/Sky.hlsl",
    .entries=std::array {
      EntryPoint { .type=kVertex, .name="VortexSkyPassVS", .view_binding=kCamera },
      EntryPoint { .type=kPixel, .name="VortexSkyPassPS", .view_binding=kCamera }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/IblProcessing.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="IblInitializeCS" },
      EntryPoint { .type=kCompute, .name="IblPrepareCS" },
      EntryPoint { .type=kCompute, .name="IblCapturePrepareCS" },
      EntryPoint { .type=kCompute, .name="IblRangeCS" },
      EntryPoint { .type=kCompute, .name="IblNormalizeCS" },
      EntryPoint { .type=kCompute, .name="IblMipCS" },
      EntryPoint { .type=kCompute, .name="IblShCS" },
      EntryPoint { .type=kCompute, .name="IblShReduceCS" },
      EntryPoint { .type=kCompute, .name="IblPrefilterCS" },
      EntryPoint { .type=kCompute, .name="IblNarrowCS" },
      EntryPoint { .type=kCompute, .name="IblPrecisionRangeCS" },
      EntryPoint { .type=kCompute, .name="IblPrecisionReduceCS" },
      EntryPoint { .type=kCompute, .name="IblCompleteCS" }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/IndirectLighting/DeferredIbl.hlsl",
    .entries=std::array {
      EntryPoint { .type=kVertex, .name="DeferredIblVS", .view_binding=kCamera },
      EntryPoint { .type=kPixel, .name="DeferredIblPS", .view_binding=kCamera }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/AtmosphereCompose.hlsl",
    .entries=std::array {
      EntryPoint { .type=kVertex, .name="VortexAtmosphereComposeVS", .view_binding=kCamera },
      EntryPoint { .type=kPixel, .name="VortexAtmosphereComposePS", .view_binding=kCamera }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/AtmosphereTransmittanceLut.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexAtmosphereTransmittanceLutCS" }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/AtmosphereMultiScatteringLut.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexAtmosphereMultiScatteringLutCS" }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/DistantSkyLightLut.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexDistantSkyLightLutCS" }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/AtmosphereSkyViewLut.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexAtmosphereSkyViewLutCS", .view_binding=kCamera },
      EntryPoint { .type=kCompute, .name="VortexAtmosphereCaptureSkyViewLutCS" }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/AtmosphereCameraAerialPerspective.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexAtmosphereCameraAerialPerspectiveCS", .view_binding=kCamera }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/VolumetricFog.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexVolumetricFogCS", .view_binding=kCamera },
      EntryPoint { .type=kCompute, .name="VortexVolumetricFogIntegrateCS", .view_binding=kCamera }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/Fog.hlsl",
    .entries=std::array {
      EntryPoint { .type=kVertex, .name="VortexFogPassVS", .view_binding=kCamera },
      EntryPoint { .type=kPixel, .name="VortexFogPassPS", .view_binding=kCamera }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/LocalFogVolumeTiledCulling.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexLocalFogVolumeTiledCullingCS", .view_binding=kCamera }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Environment/LocalFogVolumeCompose.hlsl",
    .entries=std::array {
      EntryPoint { .type=kVertex, .name="VortexLocalFogVolumeComposeVS", .view_binding=kCamera },
      EntryPoint { .type=kPixel, .name="VortexLocalFogVolumeComposePS", .view_binding=kCamera }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/PostProcess/Tonemap.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="VortexTonemapPS" },
      EntryPoint { .type=kVertex, .name="VortexTonemapVS" }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/PostProcess/GroundGrid.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="VortexGroundGridPS", .view_binding=kCamera },
      EntryPoint { .type=kVertex, .name="VortexGroundGridVS", .view_binding=kCamera }, },
    .permutations=std::array<std::string_view, 1> { "OXYGEN_HDR_OUTPUT" },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Editor/SelectionOutline.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="VortexSelectionOutlineMaskPS", .view_binding=kCamera }, },
    .permutations=std::array<std::string_view, 1> { "ALPHA_TEST" },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Editor/SelectionOutlineComposite.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="VortexSelectionOutlineCompositePS" },
      EntryPoint { .type=kVertex, .name="VortexSelectionOutlineCompositeVS" }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Editor/ViewOverlay.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="VortexViewOverlayPS", .view_binding=kCamera },
      EntryPoint { .type=kVertex, .name="VortexViewOverlayTriangleVS", .view_binding=kCamera },
      EntryPoint { .type=kVertex, .name="VortexViewOverlayLineVS", .view_binding=kCamera }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Shadows/Conventional/ConventionalShadowReceiverAnalysis.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexShadowReceiverClearCS" },
      EntryPoint { .type=kCompute, .name="VortexShadowReceiverDepthCS" },
      EntryPoint { .type=kCompute, .name="VortexShadowReceiverTranslucentCS" },
      EntryPoint { .type=kCompute, .name="VortexShadowReceiverFitCS" }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Shadows/Conventional/ConventionalShadowReceiverMask.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexShadowReceiverMaskClearCS" },
      EntryPoint { .type=kCompute, .name="VortexShadowReceiverMaskDepthCS" },
      EntryPoint { .type=kCompute, .name="VortexShadowReceiverMaskTranslucentCS" },
      EntryPoint { .type=kCompute, .name="VortexShadowReceiverMaskDilateCS" }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Diagnostics/GpuDebugClear.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="VortexGpuDebugClearCS" }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Diagnostics/GpuDebugDraw.hlsl",
    .entries=std::array {
      EntryPoint { .type=kVertex, .name="VortexGpuDebugDrawVS", .view_binding=kCamera },
      EntryPoint { .type=kPixel, .name="VortexGpuDebugDrawPS", .view_binding=kCamera }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/Editor/ViewPick.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="VortexViewPickPS", .view_binding=kCamera }, },
    .permutations=std::array<std::string_view, 1> { "ALPHA_TEST" },
  },
  ShaderFileSpec {
    .path="Vortex/Services/PostProcess/BloomDownsample.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="VortexBloomDownsamplePS" }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/PostProcess/BloomUpsample.hlsl",
    .entries=std::array {
      EntryPoint { .type=kPixel, .name="VortexBloomUpsamplePS" }, },
  },
  ShaderFileSpec {
    .path="Vortex/Services/PostProcess/Exposure.hlsl",
    .entries=std::array {
      EntryPoint { .type=kCompute, .name="ClearHistogram" },
      EntryPoint { .type=kCompute, .name="VortexExposureHistogramCS" },
      EntryPoint { .type=kCompute, .name="VortexExposureAverageCS" },
      EntryPoint { .type=kCompute, .name="VortexExposureFrameCS" },
      EntryPoint { .type=kCompute, .name="VortexExposureFallbackCS" },
      EntryPoint { .type=kCompute, .name="ClearSuitability" },
      EntryPoint { .type=kCompute, .name="GatherSuitabilityMaximum" },
      EntryPoint { .type=kCompute, .name="SelectSuitabilityCandidate" },
      EntryPoint { .type=kCompute, .name="CheckSuitabilityProduct" },
      EntryPoint { .type=kCompute, .name="ConvertQualifiedSceneColor" },
      EntryPoint { .type=kCompute, .name="FinalizeFp16Suitability" }, },
  },
  // ImGui UI shaders (no permutations)
  ShaderFileSpec {
    .path="Ui/ImGui.hlsl",
    .entries=std::array { EntryPoint { .type=kVertex, .name="VS" }, EntryPoint { .type=kPixel, .name="PS" } },
  },
  // Compositing shaders (no permutations)
  ShaderFileSpec {
    .path="Vortex/RendererCore/Compositing/Compositing_VS.hlsl",
    .entries=std::array { EntryPoint { .type=kVertex, .name="VS" } },
  },
  ShaderFileSpec {
    .path="Vortex/RendererCore/Compositing/Compositing_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
  }
);
// clang-format on

// Compile-time verification:
// - ForwardMesh_VS: 1 entry
// - ForwardMesh_PS base: 16 (ALPHA_TEST x OXYGEN_HDR_OUTPUT x
// OXYGEN_OPAQUE_OUTPUT x OXYGEN_DEPTH_COMPLETE)
// - ForwardMesh_PS DEBUG_DIRECT_* / DEBUG_IBL_*: 4 each (required debug define
//   x ALPHA_TEST x OXYGEN_HDR_OUTPUT)
// - ForwardWireframe_PS base: 2 (with/without ALPHA_TEST)
// - ForwardDebug_PS debug-required variants: 60
// - ForwardDebug_PS virtual-shadow-mask permutations: 8
// - VortexDepthPrepass: 8 (2 entries x HAS_VELOCITY x ALPHA_TEST)
// - VortexBasePassGBuffer: 8 (2 entries x HAS_VELOCITY x ALPHA_TEST)
// - VortexBasePassVelocityAux: 4 (2 entries x required MVWO define x
// ALPHA_TEST)
// - VortexBasePassVelocityMerge: 1
// - VortexBasePassDebugView: 8 (VS + 7 required debug PS variants)
// - VortexShadowDepth: 8 (2 entries x ALPHA_TEST x CUBE_SHADOW)
// - VortexContactShadowCasterDepth: 4 (2 entries x ALPHA_TEST)
// - VortexDeferredLightDirectional: 2 entries
// - VortexDeferredLightPoint: 2 entries
// - VortexDeferredLightSpot: 2 entries
// - VortexEnvironmentSky: 4 entries
// - VortexEnvironmentAtmosphere/Fog/LocalFog/Probe refresh families: 12 entries
// - VortexPostProcessTonemap: 2 entries
// - VortexPostProcessGroundGrid: 4 entries
// - VortexSelectionOutline: 4 (mask PS x ALPHA_TEST, composite VS + PS)
// - VortexViewPick: 2 (PS x ALPHA_TEST)
// - VortexViewOverlay: 3 (PS, triangle VS, line VS)
// - VortexGpuDebug: 3 (clear CS, line draw VS + PS)
// - VortexShadowReceiverFit: 8 (analysis and mask CS)
// - VortexPostProcessBloomDownsample: 1 entry
// - VortexPostProcessBloomUpsample: 1 entry
// - VortexPostProcessExposure: 3 entries
// - LightCulling: 1
// - ImGui: 2 entries
// - Compositing: 2 entries
// Total: 129

} // namespace oxygen::graphics::d3d12
