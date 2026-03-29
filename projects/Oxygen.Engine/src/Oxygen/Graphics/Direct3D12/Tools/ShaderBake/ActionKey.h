//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

#include <Oxygen/Graphics/Common/Shaders.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

[[nodiscard]] auto ComputeToolchainHash() -> uint64_t;

[[nodiscard]] auto ComputeShaderActionKey(const ShaderRequest& request,
  std::span<const std::filesystem::path> include_dirs) -> uint64_t;

} // namespace oxygen::graphics::d3d12::tools::shader_bake
