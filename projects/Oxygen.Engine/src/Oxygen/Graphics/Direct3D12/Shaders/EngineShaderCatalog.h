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
  // Wireframe pass pixel shader: ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Forward/ForwardWireframe_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 1> { "ALPHA_TEST" }
  },
  // Forward pass pixel shader: DEBUG_LIGHT_HEATMAP with ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 3>
      { "DEBUG_LIGHT_HEATMAP", "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: DEBUG_DEPTH_SLICE with ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 3>
      { "DEBUG_DEPTH_SLICE", "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: DEBUG_CLUSTER_INDEX with ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 3>
      { "DEBUG_CLUSTER_INDEX", "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: DEBUG_IBL_SPECULAR with ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 3>
      { "DEBUG_IBL_SPECULAR", "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: DEBUG_IBL_RAW_SKY with ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 3>
      { "DEBUG_IBL_RAW_SKY", "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: DEBUG_IBL_IRRADIANCE with ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 3>
      { "DEBUG_IBL_IRRADIANCE", "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: DEBUG_IBL_FACE_INDEX with ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 3>
      { "DEBUG_IBL_FACE_INDEX", "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: DEBUG_BASE_COLOR with ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 3>
      { "DEBUG_BASE_COLOR", "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: DEBUG_UV0 with ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 3>
      { "DEBUG_UV0", "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: DEBUG_OPACITY with ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 3>
      { "DEBUG_OPACITY", "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: DEBUG_WORLD_NORMALS with ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 3>
      { "DEBUG_WORLD_NORMALS", "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: DEBUG_ROUGHNESS with ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 3>
      { "DEBUG_ROUGHNESS", "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Forward pass pixel shader: DEBUG_METALNESS with ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Forward/ForwardDebug_PS.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" } },
    .permutations=std::array<std::string_view, 3>
      { "DEBUG_METALNESS", "ALPHA_TEST", "OXYGEN_HDR_OUTPUT" }
  },
  // Depth pre-pass: VS and PS with ALPHA_TEST permutation
  ShaderFileSpec {
    .path="Depth/DepthPrePass.hlsl",
    .entries=std::array { EntryPoint { .type=kPixel, .name="PS" }, EntryPoint { .type=kVertex, .name="VS" } },
    .permutations=std::array<std::string_view, 1> { "ALPHA_TEST" }
  },
  // Light culling compute shader (tile-based or clustered mode)
  ShaderFileSpec {
    .path="Lighting/LightCulling.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } },
    .permutations=std::array<std::string_view, 1> { "CLUSTERED" }
  },
  // Sky atmosphere LUT compute shaders (no permutations)
  ShaderFileSpec {
    .path="Atmosphere/TransmittanceLut_CS.hlsl",
    .entries=std::array { EntryPoint { .type=kCompute, .name="CS" } }
  },
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
// - ForwardMesh_PS base: 4 (ALPHA_TEST x OXYGEN_HDR_OUTPUT)
// - ForwardWireframe_PS base: 2 (with/without ALPHA_TEST)
// - ForwardMesh_PS DEBUG_*: 8 each (debug define x ALPHA_TEST x
// OXYGEN_HDR_OUTPUT)
// - DepthPrePass: 4 (2 entries x 2 permutations)
// - LightCulling: 2 (1 entry x 2 permutations)
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
// - AutoExposure_Histogram_CS: 1 entry
// - AutoExposure_Average_CS: 1 entry
// Total: 132

} // namespace oxygen::graphics::d3d12
