//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/MaterialDomain.h>

auto oxygen::data::to_string(oxygen::data::AssetType value) noexcept -> const
  char*
{
  switch (value) {
    // clang-format off
    case AssetType::kUnknown:      return "__Unknown__";
    case AssetType::kMaterial:     return "Material";
    case AssetType::kGeometry:     return "Geometry";
    case AssetType::kScene:        return "Scene";
    // clang-format on
  }

  return "__NotSupported__";
}

auto oxygen::data::to_string(oxygen::data::MaterialDomain value) noexcept
  -> const char*
{
  switch (value) {
    // clang-format off
    case MaterialDomain::kUnknown:        return "__Unknown__";
    case MaterialDomain::kOpaque:         return "Opaque";
    case MaterialDomain::kAlphaBlended:   return "Alpha Blended";
    case MaterialDomain::kMasked:         return "Masked";
    case MaterialDomain::kDecal:          return "Decal";
    case MaterialDomain::kUserInterface:  return "User Interface";
    case MaterialDomain::kPostProcess:    return "Post-Process";
    // clang-format on
  }

  return "__NotSupported__";
}
