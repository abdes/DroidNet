//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace oxygen::graphics::d3d12::tools::shader_bake {

enum class ShaderBakeCommand : uint8_t {
  kUpdate,
  kRebuild,
  kCleanCache,
};

enum class ShaderBakeMode : uint8_t {
  kDev,
  kProduction,
};

struct BakeArgs {
  ShaderBakeCommand command { ShaderBakeCommand::kUpdate };
  ShaderBakeMode mode { ShaderBakeMode::kDev };
  std::filesystem::path workspace_root;
  std::filesystem::path build_root;
  std::filesystem::path out_file;
  std::filesystem::path shader_source_root;
  std::filesystem::path oxygen_include_root;
  std::vector<std::filesystem::path> extra_include_dirs;
};

auto BakeShaderLibrary(const BakeArgs& args) -> int;

} // namespace oxygen::graphics::d3d12::tools::shader_bake
