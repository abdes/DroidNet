//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>

namespace oxygen::graphics::d3d12::tools::shader_bake {

[[nodiscard]] constexpr auto GetActiveShaderBuildConfigName() noexcept
  -> std::string_view
{
#if !defined(NDEBUG)
  return "Debug";
#else
  return "Release";
#endif
}

[[nodiscard]] constexpr auto IsExternalShaderDebugInfoEnabled() noexcept -> bool
{
#if !defined(NDEBUG)
  return true;
#else
  return false;
#endif
}

[[nodiscard]] inline auto GetFixedDxcArgumentSchema() -> std::string
{
  std::string schema = "-Ges;-enable-16bit-types;-HV=2021;sm=6_6;";
#if !defined(NDEBUG)
  schema += "-Od;-Zi;-Fo=<request-key>.dxil;-Fd=<request-key>.pdb;";
#else
  schema += "-O3;-Fo=<request-key>.dxil;";
#endif
  return schema;
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
