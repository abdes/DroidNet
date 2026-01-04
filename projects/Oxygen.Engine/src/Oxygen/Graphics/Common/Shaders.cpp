//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Graphics/Common/Shaders.h>

auto oxygen::graphics::MakeShaderIdentifier(const ShaderType shader_type,
  const std::string& relative_path, const std::string_view entry_point,
  const std::span<const ShaderDefine> defines) -> std::string
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

  std::string out;
  out.reserve(
    prefix.size() + 1U + normalized_path.size() + 1U + entry_point.size() + 8U);
  out += prefix;
  out += '@';
  out += normalized_path;
  out += '#';
  out += entry_point;

  if (!defines.empty()) {
    std::vector<ShaderDefine> sorted_defines(defines.begin(), defines.end());
    std::sort(sorted_defines.begin(), sorted_defines.end(),
      [](const ShaderDefine& a, const ShaderDefine& b) {
        if (a.name != b.name) {
          return a.name < b.name;
        }
        return a.value.value_or("") < b.value.value_or("");
      });

    out += '?';
    bool first = true;
    for (const auto& def : sorted_defines) {
      if (!first) {
        out += '&';
      }
      first = false;

      out += def.name;
      if (def.value) {
        out += '=';
        out += *def.value;
      }
    }
  }

  return out;
}
