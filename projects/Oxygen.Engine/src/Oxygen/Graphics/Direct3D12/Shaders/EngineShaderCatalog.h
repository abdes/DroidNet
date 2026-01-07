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
// clang-format off
inline constexpr auto kEngineShaders = GenerateCatalog(
  // Forward pass: VS and PS with ALPHA_TEST permutation
  ShaderFileSpec {
    "Passes/Forward/ForwardMesh.hlsl",
    std::array { EntryPoint { kPixel, "PS" }, EntryPoint { kVertex, "VS" } },
    std::array<std::string_view, 1> { "ALPHA_TEST" }
  },
  // Depth pre-pass: VS and PS with ALPHA_TEST permutation
  ShaderFileSpec {
    "Passes/Depth/DepthPrePass.hlsl",
    std::array { EntryPoint { kPixel, "PS" }, EntryPoint { kVertex, "VS" } },
    std::array<std::string_view, 1> { "ALPHA_TEST" }
  },
  // Light culling compute shader (no permutations)
  ShaderFileSpec {
    "Passes/Lighting/LightCulling.hlsl",
    std::array { EntryPoint { kCompute, "CS" } }
  },
  // ImGui UI shaders (no permutations)
  ShaderFileSpec {
    "Passes/Ui/ImGui.hlsl",
    std::array { EntryPoint { kVertex, "VS" }, EntryPoint { kPixel, "PS" } }
  }
);
// clang-format on

// Compile-time verification
static_assert(kEngineShaders.size() == 11, "Expected 11 shader entries");

} // namespace oxygen::graphics::d3d12
