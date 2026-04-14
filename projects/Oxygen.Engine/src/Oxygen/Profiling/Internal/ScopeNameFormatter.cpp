//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Profiling/Internal/ScopeNameFormatter.h>

#include <Oxygen/Profiling/ProfileScope.h>

namespace oxygen::profiling::internal {

auto EscapeScopeVariableValue(const std::string_view value) -> std::string
{
  std::string escaped;
  escaped.reserve(value.size());

  for (const char c : value) {
    switch (c) {
    case '\\':
    case '[':
    case ']':
    case ',':
    case '=':
      escaped.push_back('\\');
      escaped.push_back(c);
      break;
    default:
      escaped.push_back(c);
      break;
    }
  }

  return escaped;
}

auto FormatScopeNameImpl(const ProfileScopeDesc& desc) -> std::string
{
  if (desc.variables.empty()) {
    return desc.label;
  }

  std::string out;
  out.reserve(desc.label.size() + 16U * desc.variables.size());
  out += desc.label;
  out.push_back('[');

  for (std::size_t i = 0; i < desc.variables.size(); ++i) {
    const auto& variable = desc.variables[i];
    if (i > 0U) {
      out.push_back(',');
    }
    if (variable.key != nullptr) {
      out += variable.key.get();
    }
    out.push_back('=');
    out += EscapeScopeVariableValue(variable.value);
  }

  out.push_back(']');
  return out;
}

} // namespace oxygen::profiling::internal
