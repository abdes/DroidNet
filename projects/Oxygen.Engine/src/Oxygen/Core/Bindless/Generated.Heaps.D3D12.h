//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// clang-format off

// Generated file - do not edit.
// Source: projects/Oxygen.Engine/src/Oxygen/Core/Bindless/Spec.yaml
// Source-Version: 1.0.0
// Tool: BindlessCodeGen 1.1.0
// Generated: 2025-08-18 09:46:48

#pragma once

namespace oxygen::engine::binding {

// D3D12 Heap Strategy JSON embedded for convenient include-only usage.
// Parse this at runtime with your preferred JSON library.
static constexpr const char kD3D12HeapStrategyJson[] = R"OXJ(
{
  "$meta": {
    "source": "projects/Oxygen.Engine/src/Oxygen/Core/Bindless/Spec.yaml",
    "source_version": "1.0.0",
    "tool_version": "1.1.0",
    "generated": "2025-08-18 09:46:48",
    "format": "D3D12HeapStrategy/2",
    "schema_version": "1.0.0"
  },
  "heaps": {
    "CBV_SRV_UAV:cpu": {
      "capacity": 1000000,
      "shader_visible": false,
      "allow_growth": false,
      "growth_factor": 0.0,
      "max_growth_iterations": 0,
      "base_index": 0
    },
    "CBV_SRV_UAV:gpu": {
      "capacity": 1000000,
      "shader_visible": true,
      "allow_growth": false,
      "growth_factor": 0.0,
      "max_growth_iterations": 0,
      "base_index": 1000000
    },
    "SAMPLER:cpu": {
      "capacity": 2048,
      "shader_visible": false,
      "allow_growth": false,
      "growth_factor": 0.0,
      "max_growth_iterations": 0,
      "base_index": 2000000
    },
    "SAMPLER:gpu": {
      "capacity": 2048,
      "shader_visible": true,
      "allow_growth": false,
      "growth_factor": 0.0,
      "max_growth_iterations": 0,
      "base_index": 2002048
    },
    "RTV:cpu": {
      "capacity": 1024,
      "shader_visible": false,
      "allow_growth": false,
      "growth_factor": 0.0,
      "max_growth_iterations": 0,
      "base_index": 2004096
    },
    "DSV:cpu": {
      "capacity": 1024,
      "shader_visible": false,
      "allow_growth": false,
      "growth_factor": 0.0,
      "max_growth_iterations": 0,
      "base_index": 2005120
    }
  }
}
)OXJ";

} // namespace oxygen::engine::binding
// clang-format on
