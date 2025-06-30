//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>

#include <Oxygen/Graphics/Common/Shaders.h>

using oxygen::data::ShaderType;
using oxygen::graphics::MakeShaderIdentifier;

auto main(int /*argc*/, char** /*argv*/) -> int
{
  const auto id
    = MakeShaderIdentifier(ShaderType::kVertex, "shaders/vertex.glsl");
  std::cout << id << '\n';
  return 0;
}
