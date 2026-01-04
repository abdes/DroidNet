//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string>

namespace oxygen::graphics::d3d12::tools::shader_bake {

struct InspectArgs {
  std::filesystem::path file;

  bool header_only { false };
  bool modules_only { false };
  bool show_defines { false };
  bool show_offsets { false };
  bool show_reflection { false };
};

auto InspectShaderLibrary(const InspectArgs& args) -> int;

} // namespace oxygen::graphics::d3d12::tools::shader_bake
