//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <vector>

namespace oxygen::graphics::d3d12::tools::shader_bake {

struct BakeArgs {
  std::filesystem::path workspace_root;
  std::filesystem::path out_file;
  std::filesystem::path shader_source_root;
  std::filesystem::path oxygen_include_root;
  std::vector<std::filesystem::path> extra_include_dirs;
};

auto BakeShaderLibrary(const BakeArgs& args) -> int;

} // namespace oxygen::graphics::d3d12::tools::shader_bake
