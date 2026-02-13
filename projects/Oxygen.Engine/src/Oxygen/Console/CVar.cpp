//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Console/CVar.h>

namespace oxygen::console {

auto to_string(const CVarType value) -> const char*
{
  switch (value) {
  case CVarType::kBool:
    return "Bool";
  case CVarType::kInt:
    return "Int";
  case CVarType::kFloat:
    return "Float";
  case CVarType::kString:
    return "String";
  }
  return "__NotSupported__";
}

auto to_string(const CVarFlags value) -> std::string
{
  if (value == CVarFlags::kNone) {
    return "None";
  }

  std::string result;
  bool first = true;
  auto checked = CVarFlags::kNone;

  const auto append = [&](const CVarFlags flag, const char* name) {
    if ((value & flag) == flag) {
      if (!first) {
        result += "|";
      }
      result += name;
      first = false;
      checked |= flag;
    }
  };

  append(CVarFlags::kArchive, "Archive");
  append(CVarFlags::kReadOnly, "ReadOnly");
  append(CVarFlags::kCheat, "Cheat");
  append(CVarFlags::kDevOnly, "DevOnly");
  append(CVarFlags::kRequiresRestart, "RequiresRestart");
  append(CVarFlags::kLatched, "Latched");
  append(CVarFlags::kRenderThreadSafe, "RenderThreadSafe");
  append(CVarFlags::kHidden, "Hidden");

  return checked == value ? result : "__NotSupported__";
}

} // namespace oxygen::console
