//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>

#include <Oxygen/Graphics/Common/Shaders.h>

using oxygen::ShaderType;
using oxygen::graphics::FormatShaderLogKey;

auto main(int /*argc*/, char** /*argv*/) -> int
{
  const oxygen::graphics::ShaderInfo shader_info {
    .type = ShaderType::kVertex,
    .relative_path = "shaders/vertex.glsl",
    .entry_point = "main",
  };
  std::cout << FormatShaderLogKey(shader_info) << '\n';
  return 0;
}
