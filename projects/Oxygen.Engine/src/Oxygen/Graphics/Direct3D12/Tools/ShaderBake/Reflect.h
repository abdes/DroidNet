//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <Oxygen/Graphics/Common/Shaders.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

auto ExtractAndSerializeReflection(const oxygen::graphics::ShaderInfo& shader,
  std::span<const std::byte> dxil) -> std::vector<std::byte>;

} // namespace oxygen::graphics::d3d12::tools::shader_bake
