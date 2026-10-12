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
#include <Oxygen/Graphics/Direct3D12/Shaders/ShaderCatalogBuilder.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/CompileProfile.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

[[nodiscard]] auto ComputeToolchainHash(const ShaderCompileProfile& profile)
  -> uint64_t;

//! Key of everything that determines a request's compiled module, including
//! the compile-only view-binding define.
[[nodiscard]] auto ComputeShaderActionKey(const ShaderRequest& request,
  ShaderViewBinding view_binding,
  std::span<const std::filesystem::path> include_dirs,
  const ShaderCompileProfile& profile) -> uint64_t;

} // namespace oxygen::graphics::d3d12::tools::shader_bake
