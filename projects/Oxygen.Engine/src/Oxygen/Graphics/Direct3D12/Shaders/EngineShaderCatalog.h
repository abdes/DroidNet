//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>

#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Shaders.h>

namespace oxygen::graphics::d3d12 {

// Specification of engine shaders. Each entry is a ShaderProfile corresponding
// to one of the shaders we want to automatically compile, package and load.
inline const std::array<ShaderInfo, 12> kEngineShaders = {
  {
    {
      .type = ShaderType::kPixel,
      .relative_path = "Passes/Forward/ForwardMesh.hlsl",
      .entry_point = "PS",
    },
    {
      .type = ShaderType::kPixel,
      .relative_path = "Passes/Forward/ForwardMesh.hlsl",
      .entry_point = "PS_Masked",
    },
    {
      .type = ShaderType::kVertex,
      .relative_path = "Passes/Forward/ForwardMesh.hlsl",
      .entry_point = "VS",
    },
    {
      .type = ShaderType::kPixel,
      .relative_path = "Passes/Depth/DepthPrePass.hlsl",
      .entry_point = "PS",
    },
    {
      .type = ShaderType::kPixel,
      .relative_path = "Passes/Depth/DepthPrePass.hlsl",
      .entry_point = "PS_OpaqueDepth",
    },
    {
      .type = ShaderType::kPixel,
      .relative_path = "Passes/Depth/DepthPrePass.hlsl",
      .entry_point = "PS_MaskedDepth",
    },
    {
      .type = ShaderType::kVertex,
      .relative_path = "Passes/Depth/DepthPrePass.hlsl",
      .entry_point = "VS",
    },
    {
      .type = ShaderType::kVertex,
      .relative_path = "Passes/Depth/DepthPrePass.hlsl",
      .entry_point = "VS_OpaqueDepth",
    },
    {
      .type = ShaderType::kVertex,
      .relative_path = "Passes/Depth/DepthPrePass.hlsl",
      .entry_point = "VS_MaskedDepth",
    },
    {
      .type = ShaderType::kCompute,
      .relative_path = "Passes/Lighting/LightCulling.hlsl",
      .entry_point = "CS",
    },
    {
      .type = ShaderType::kVertex,
      .relative_path = "Passes/Ui/ImGui.hlsl",
      .entry_point = "VS",
    },
    {
      .type = ShaderType::kPixel,
      .relative_path = "Passes/Ui/ImGui.hlsl",
      .entry_point = "PS",
    },
  },
};

} // namespace oxygen::graphics::d3d12
