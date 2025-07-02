//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <string>

#include <Oxygen/Graphics/Common/Shaders.h>

auto oxygen::graphics::MakeShaderIdentifier(
  const ShaderType shader_type, const std::string& relative_path) -> std::string
{
  using oxygen::ShaderType;

  std::string prefix;
  switch (shader_type) { // NOLINT(clang-diagnostic-switch-enum) rest are not
                         // valid or not supported
    // clang-format off
  case ShaderType::kVertex:         prefix = "VS"; break;
  case ShaderType::kPixel:          prefix = "PS"; break;
  case ShaderType::kGeometry:       prefix = "GS"; break;
  case ShaderType::kHull:           prefix = "HS"; break;
  case ShaderType::kDomain:         prefix = "DS"; break;
  case ShaderType::kCompute:        prefix = "CS"; break;
  case ShaderType::kAmplification:  prefix = "AS"; break;
  case ShaderType::kMesh:           prefix = "MS"; break;
  default: prefix = "XX"; break;
    // clang-format on
  }

  std::filesystem::path path(relative_path);
  path = path.lexically_normal();

  // Convert path to string with forward slashes
  const std::string normalized_path = path.generic_string();

  return prefix + "@" + normalized_path;
}
