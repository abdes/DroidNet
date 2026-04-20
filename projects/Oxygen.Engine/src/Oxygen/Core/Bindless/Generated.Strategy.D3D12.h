//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// clang-format off

// Generated file - do not edit.
// Source: src/Oxygen/Core/Meta/Bindless.yaml
// Source-Version: 2.0.0
// Schema-Version: 2.0.0
// Tool: BindlessCodeGen 1.2.2
// Generated: 2026-04-20 09:39:53

#pragma once

namespace oxygen::bindless::generated::d3d12 {

static constexpr const char kStrategyJson[] = R"OXJ(
{
  "$meta": {
    "source": "src/Oxygen/Core/Meta/Bindless.yaml",
    "source_version": "2.0.0",
    "tool_version": "1.2.2",
    "generated": "2026-04-20 09:39:53",
    "format": "BindlessStrategy.D3D12/1",
    "schema_version": "2.0.0"
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

} // namespace oxygen::bindless::generated::d3d12
// clang-format on
