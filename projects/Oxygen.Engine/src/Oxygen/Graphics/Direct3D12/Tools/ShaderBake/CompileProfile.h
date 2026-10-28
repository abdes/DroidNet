//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace oxygen::graphics::d3d12::tools::shader_bake {

//! Shader configuration a bake compiles, independent of the ShaderBake build.
enum class ShaderConfig : uint8_t {
  kDebug, //!< Unoptimized (`-Od`), external debug info by default.
  kRelease, //!< Optimized (`-O3`), no debug info by default.
};

//! Where compiled shaders keep their debug information.
enum class ShaderDebugInfo : uint8_t {
  kNone, //!< No debug information.
  //! A PDB sidecar per request, beside the archive under `pdb/`. The DXIL
  //! names the sidecar's file, so debuggers find it by searching that tree.
  kExternal,
  //! Debug information inside each DXIL (`-Qembed_debug`). Tools need no
  //! search path, at the cost of a larger archive.
  kEmbedded,
};

//! The DXC compilation policy of one bake.
struct ShaderCompileProfile {
  ShaderConfig config { ShaderConfig::kRelease };
  ShaderDebugInfo debug_info { ShaderDebugInfo::kNone };

  auto operator==(const ShaderCompileProfile&) const -> bool = default;

  [[nodiscard]] constexpr auto HasDebugInfo() const noexcept -> bool
  {
    return debug_info != ShaderDebugInfo::kNone;
  }

  [[nodiscard]] constexpr auto WritesPdbSidecars() const noexcept -> bool
  {
    return debug_info == ShaderDebugInfo::kExternal;
  }
};

//! The profile of `config`, with `debug_info` overriding its default.
[[nodiscard]] constexpr auto MakeShaderCompileProfile(const ShaderConfig config,
  const std::optional<ShaderDebugInfo> debug_info = std::nullopt) noexcept
  -> ShaderCompileProfile
{
  const auto default_debug_info = config == ShaderConfig::kDebug
    ? ShaderDebugInfo::kExternal
    : ShaderDebugInfo::kNone;
  return ShaderCompileProfile {
    .config = config,
    .debug_info = debug_info.value_or(default_debug_info),
  };
}

[[nodiscard]] constexpr auto to_string(const ShaderConfig config) noexcept
  -> std::string_view
{
  switch (config) {
  case ShaderConfig::kDebug:
    return "debug";
  case ShaderConfig::kRelease:
    return "release";
  }
  return "unknown";
}

[[nodiscard]] constexpr auto to_string(
  const ShaderDebugInfo debug_info) noexcept -> std::string_view
{
  switch (debug_info) {
  case ShaderDebugInfo::kNone:
    return "none";
  case ShaderDebugInfo::kExternal:
    return "external";
  case ShaderDebugInfo::kEmbedded:
    return "embedded";
  }
  return "unknown";
}

[[nodiscard]] constexpr auto ParseShaderConfig(const std::string_view text)
  -> std::optional<ShaderConfig>
{
  if (text == "debug") {
    return ShaderConfig::kDebug;
  }
  if (text == "release") {
    return ShaderConfig::kRelease;
  }
  return std::nullopt;
}

[[nodiscard]] constexpr auto ParseShaderDebugInfo(const std::string_view text)
  -> std::optional<ShaderDebugInfo>
{
  if (text == "none") {
    return ShaderDebugInfo::kNone;
  }
  if (text == "external") {
    return ShaderDebugInfo::kExternal;
  }
  if (text == "embedded") {
    return ShaderDebugInfo::kEmbedded;
  }
  return std::nullopt;
}

//! The profile-dependent DXC arguments, in the form the cache keys hash.
/*!
 Every argument that changes compiled output must appear here, so a profile
 change invalidates exactly the artifacts it affects.
*/
[[nodiscard]] inline auto GetDxcArgumentSchema(
  const ShaderCompileProfile& profile) -> std::string
{
  std::string schema = "-Ges;-Zpc;-enable-16bit-types;-HV=2021;sm=6_6;";
  schema += profile.config == ShaderConfig::kDebug ? "-Od;" : "-O3;";
  switch (profile.debug_info) {
  case ShaderDebugInfo::kNone:
    break;
  case ShaderDebugInfo::kExternal:
    schema += "-Zi;-Fd=<entry-point>__<request-key>.pdb;";
    break;
  case ShaderDebugInfo::kEmbedded:
    schema += "-Zi;-Qembed_debug;";
    break;
  }
  schema += "-Fo=<request-key>.dxil;";
  return schema;
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
