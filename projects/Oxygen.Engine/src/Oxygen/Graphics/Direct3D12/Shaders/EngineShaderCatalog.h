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

// clang-format off
inline constexpr auto kEngineShaders = GenerateCatalog(
  // Forward pass vertex shader (shared by all modes)
  ShaderFileSpec {
    "Passes/Forward/ForwardMesh_VS.hlsl",
    std::array { EntryPoint { kVertex, "VS" } }
  },
  // Forward pass pixel shader: ALPHA_TEST permutation for normal rendering
  ShaderFileSpec {
    "Passes/Forward/ForwardMesh_PS.hlsl",
    std::array { EntryPoint { kPixel, "PS" } },
    std::array<std::string_view, 1> { "ALPHA_TEST" }
  },
  // Forward pass pixel shader: DEBUG_LIGHT_HEATMAP with ALPHA_TEST permutation
  ShaderFileSpec {
    "Passes/Forward/ForwardMesh_PS.hlsl",
    std::array { EntryPoint { kPixel, "PS" } },
    std::array<std::string_view, 2> { "DEBUG_LIGHT_HEATMAP", "ALPHA_TEST" }
  },
  // Forward pass pixel shader: DEBUG_DEPTH_SLICE with ALPHA_TEST permutation
  ShaderFileSpec {
    "Passes/Forward/ForwardMesh_PS.hlsl",
    std::array { EntryPoint { kPixel, "PS" } },
    std::array<std::string_view, 2> { "DEBUG_DEPTH_SLICE", "ALPHA_TEST" }
  },
  // Forward pass pixel shader: DEBUG_CLUSTER_INDEX with ALPHA_TEST permutation
  ShaderFileSpec {
    "Passes/Forward/ForwardMesh_PS.hlsl",
    std::array { EntryPoint { kPixel, "PS" } },
    std::array<std::string_view, 2> { "DEBUG_CLUSTER_INDEX", "ALPHA_TEST" }
  },
  // Depth pre-pass: VS and PS with ALPHA_TEST permutation
  ShaderFileSpec {
    "Passes/Depth/DepthPrePass.hlsl",
    std::array { EntryPoint { kPixel, "PS" }, EntryPoint { kVertex, "VS" } },
    std::array<std::string_view, 1> { "ALPHA_TEST" }
  },
  // Light culling compute shader (tile-based or clustered mode)
  ShaderFileSpec {
    "Passes/Lighting/LightCulling.hlsl",
    std::array { EntryPoint { kCompute, "CS" } },
    std::array<std::string_view, 1> { "CLUSTERED" }
  },
  // Sky sphere shaders (no permutations)
  ShaderFileSpec {
    "Passes/Sky/SkySphere_VS.hlsl",
    std::array { EntryPoint { kVertex, "VS" } }
  },
  ShaderFileSpec {
    "Passes/Sky/SkySphere_PS.hlsl",
    std::array { EntryPoint { kPixel, "PS" } }
  },
  // ImGui UI shaders (no permutations)
  ShaderFileSpec {
    "Passes/Ui/ImGui.hlsl",
    std::array { EntryPoint { kVertex, "VS" }, EntryPoint { kPixel, "PS" } }
  }
);
// clang-format on

// Compile-time verification:
// - ForwardMesh_VS: 1 entry
// - ForwardMesh_PS base: 2 (with/without ALPHA_TEST)
// - ForwardMesh_PS DEBUG_LIGHT_HEATMAP: 4 (2x2 for debug and ALPHA_TEST)
// - ForwardMesh_PS DEBUG_DEPTH_SLICE: 4
// - ForwardMesh_PS DEBUG_CLUSTER_INDEX: 4
// - DepthPrePass: 4 (2 entries x 2 permutations)
// - LightCulling: 2 (1 entry x 2 permutations)
// - SkySphere_VS: 1 entry
// - SkySphere_PS: 1 entry
// - ImGui: 2 (2 entries x 1 permutation)
// Total: 1 + 2 + 4 + 4 + 4 + 4 + 2 + 1 + 1 + 2 = 25
static_assert(kEngineShaders.size() == 25, "Expected 25 shader entries");

} // namespace oxygen::graphics::d3d12
