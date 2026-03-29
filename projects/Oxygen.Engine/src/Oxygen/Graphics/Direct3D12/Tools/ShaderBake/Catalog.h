//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Shaders/ShaderCatalogBuilder.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

struct ExpandedShaderRequest {
  ShaderRequest request;
  uint64_t request_key { 0 };
};

[[nodiscard]] auto ExpandShaderCatalog(std::span<const ShaderEntry> entries)
  -> std::vector<ExpandedShaderRequest>;

} // namespace oxygen::graphics::d3d12::tools::shader_bake
