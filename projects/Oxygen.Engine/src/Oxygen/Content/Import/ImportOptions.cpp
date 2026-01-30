//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/ImportOptions.h>
#include <string>

namespace oxygen::content::import {

auto to_string(AssetKeyPolicy value) -> std::string
{
  switch (value) {
  case AssetKeyPolicy::kDeterministicFromVirtualPath:
    return "DeterministicFromVirtualPath";
  case AssetKeyPolicy::kRandom:
    return "Random";
  }
  return "Unknown";
}

auto to_string(UnitNormalizationPolicy value) -> std::string
{
  switch (value) {
  case UnitNormalizationPolicy::kNormalizeToMeters:
    return "NormalizeToMeters";
  case UnitNormalizationPolicy::kPreserveSource:
    return "PreserveSource";
  case UnitNormalizationPolicy::kApplyCustomFactor:
    return "ApplyCustomFactor";
  }
  return "Unknown";
}

auto to_string(ImportContentFlags value) -> std::string
{
  if (value == ImportContentFlags::kNone) {
    return "None";
  }
  if (value == ImportContentFlags::kAll) {
    return "All";
  }

  std::string result;
  auto add_flag = [&](ImportContentFlags flag, const char* name) {
    if ((value & flag) != ImportContentFlags::kNone) {
      if (!result.empty()) {
        result += "|";
      }
      result += name;
    }
  };

  add_flag(ImportContentFlags::kTextures, "Textures");
  add_flag(ImportContentFlags::kMaterials, "Materials");
  add_flag(ImportContentFlags::kGeometry, "Geometry");
  add_flag(ImportContentFlags::kScene, "Scene");

  return result.empty() ? "None" : result;
}

auto to_string(GeometryAttributePolicy value) -> std::string
{
  switch (value) {
  case GeometryAttributePolicy::kNone:
    return "None";
  case GeometryAttributePolicy::kPreserveIfPresent:
    return "PreserveIfPresent";
  case GeometryAttributePolicy::kGenerateMissing:
    return "GenerateMissing";
  case GeometryAttributePolicy::kAlwaysRecalculate:
    return "AlwaysRecalculate";
  }
  return "Unknown";
}

auto to_string(NodePruningPolicy value) -> std::string
{
  switch (value) {
  case NodePruningPolicy::kKeepAll:
    return "KeepAll";
  case NodePruningPolicy::kDropEmptyNodes:
    return "DropEmptyNodes";
  }
  return "Unknown";
}

} // namespace oxygen::content::import
