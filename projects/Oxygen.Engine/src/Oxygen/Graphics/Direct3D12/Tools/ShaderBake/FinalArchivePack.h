//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Catalog.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/ModuleArtifact.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

auto PackFinalShaderArchive(const std::filesystem::path& out_file,
  uint64_t toolchain_hash, std::span<const ExpandedShaderRequest> requests,
  std::span<const ModuleArtifact> artifacts) -> void;

} // namespace oxygen::graphics::d3d12::tools::shader_bake
