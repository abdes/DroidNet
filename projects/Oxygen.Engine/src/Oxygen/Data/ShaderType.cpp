//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/ShaderType.h>

auto oxygen::data::to_string(const ShaderType value) -> const char*
{
  switch (value) {
    // clang-format off
  case ShaderType::kUnknown:         return "Unknown";
  case ShaderType::kAmplification:   return "Amplification Shader";
  case ShaderType::kMesh:            return "Mesh Shader";
  case ShaderType::kVertex:          return "Vertex Shader";
  case ShaderType::kHull:            return "Hull Shader";
  case ShaderType::kDomain:          return "Domain Shader";
  case ShaderType::kGeometry:        return "Geometry Shader";
  case ShaderType::kPixel:           return "Pixel Shader";
  case ShaderType::kCompute:         return "Compute Shader";
  case ShaderType::kRayGen:          return "Ray Generation Shader";
  case ShaderType::kIntersection:    return "Intersection Shader";
  case ShaderType::kAnyHit:          return "Any-Hit Shader";
  case ShaderType::kClosestHit:      return "Closest-Hit Shader";
  case ShaderType::kMiss:            return "Miss Shader";
  case ShaderType::kCallable:        return "Callable Shader";
  case ShaderType::kMaxShaderType:   return "__Max__";
    // clang-format on
  }

  return "__NotSupported__";
}
