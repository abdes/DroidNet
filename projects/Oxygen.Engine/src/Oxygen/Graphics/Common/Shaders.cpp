//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Graphics/Common/Shaders.h>

namespace oxygen::graphics {
namespace {

  [[nodiscard]] auto StagePrefix(const ShaderType shader_type)
    -> std::string_view
  {
    switch (shader_type) { // NOLINT(clang-diagnostic-switch-enum)
      // clang-format off
  case ShaderType::kVertex:         return "VS";
  case ShaderType::kPixel:          return "PS";
  case ShaderType::kGeometry:       return "GS";
  case ShaderType::kHull:           return "HS";
  case ShaderType::kDomain:         return "DS";
  case ShaderType::kCompute:        return "CS";
  case ShaderType::kAmplification:  return "AS";
  case ShaderType::kMesh:           return "MS";
  default:                          return "XX";
      // clang-format on
    }
  }

  [[nodiscard]] auto IsAsciiIdentifier(const std::string_view s) -> bool
  {
    if (s.empty()) {
      return false;
    }

    const auto is_alpha_underscore = [](const unsigned char c) {
      return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    };
    const auto is_alnum_underscore = [](const unsigned char c) {
      return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
        || (c >= '0' && c <= '9') || c == '_';
    };

    if (!is_alpha_underscore(static_cast<unsigned char>(s.front()))) {
      return false;
    }
    for (const char ch : s) {
      if (!is_alnum_underscore(static_cast<unsigned char>(ch))) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] auto IsValidDefineName(const std::string_view s) -> bool
  {
    if (s.empty()) {
      return false;
    }
    const auto is_upper
      = [](const unsigned char c) { return c >= 'A' && c <= 'Z'; };
    const auto is_upper_digit_underscore = [](const unsigned char c) {
      return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    };

    if (!is_upper(static_cast<unsigned char>(s.front()))) {
      return false;
    }
    for (const char ch : s) {
      if (!is_upper_digit_underscore(static_cast<unsigned char>(ch))) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] auto HasAnyWhitespace(const std::string_view s) -> bool
  {
    for (const char ch : s) {
      if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] auto NormalizeSourcePath(std::string_view source_path)
    -> std::string
  {
    if (source_path.empty()) {
      throw std::invalid_argument(
        "ShaderRequest.source_path must not be empty");
    }

    std::string tmp(source_path);
    std::replace(tmp.begin(), tmp.end(), '\\', '/');

    if (tmp.find(':') != std::string::npos) {
      throw std::invalid_argument(
        "ShaderRequest.source_path must be a relative path (drive ':' found)");
    }

    std::filesystem::path p(tmp);
    if (p.is_absolute()) {
      throw std::invalid_argument(
        "ShaderRequest.source_path must be a relative path");
    }

    p = p.lexically_normal();
    const std::string normalized = p.generic_string();
    if (normalized.empty() || normalized == ".") {
      throw std::invalid_argument(
        "ShaderRequest.source_path must not be empty after normalization");
    }

    for (const auto& part : p) {
      if (part == "..") {
        throw std::invalid_argument("ShaderRequest.source_path must not "
                                    "contain '..' after normalization");
      }
    }

    return normalized;
  }

} // namespace

auto CanonicalizeShaderRequest(ShaderRequest request) -> ShaderRequest
{
  request.source_path = NormalizeSourcePath(request.source_path);

  if (!IsAsciiIdentifier(request.entry_point)) {
    throw std::invalid_argument(
      "ShaderRequest.entry_point must be a valid identifier");
  }

  for (const auto& def : request.defines) {
    if (!IsValidDefineName(def.name)) {
      throw std::invalid_argument(
        "ShaderRequest.defines[].name must match [A-Z][A-Z0-9_]*");
    }
    if (def.value && HasAnyWhitespace(*def.value)) {
      throw std::invalid_argument(
        "ShaderRequest.defines[].value must not contain whitespace");
    }
  }

  std::sort(request.defines.begin(), request.defines.end(),
    [](const ShaderDefine& a, const ShaderDefine& b) {
      if (a.name != b.name) {
        return a.name < b.name;
      }
      return a.value.value_or("") < b.value.value_or("");
    });

  for (size_t i = 1; i < request.defines.size(); ++i) {
    if (request.defines[i - 1].name == request.defines[i].name) {
      throw std::invalid_argument(
        "ShaderRequest.defines must not contain duplicate names");
    }
  }

  return request;
}

auto FormatShaderLogKey(const ShaderRequest& request) -> std::string
{
  const auto prefix = StagePrefix(request.stage);

  std::string out;
  out.reserve(prefix.size() + 1U + request.source_path.size() + 1U
    + request.entry_point.size());
  out += prefix;
  out += '@';
  out += request.source_path;
  out += ':';
  out += request.entry_point;
  return out;
}

auto FormatShaderLogKey(const ShaderInfo& shader_info) -> std::string
{
  return FormatShaderLogKey(CanonicalizeShaderRequest(ShaderRequest {
    .stage = shader_info.type,
    .source_path = shader_info.relative_path,
    .entry_point = shader_info.entry_point,
    .defines = {},
  }));
}

auto ShaderRequestHash::operator()(const ShaderRequest& request) const noexcept
  -> size_t
{
  size_t seed = 0;
  const auto hash_combine = [&seed](const size_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  };

  hash_combine(std::hash<int> {}(static_cast<int>(request.stage)));
  hash_combine(std::hash<std::string> {}(request.source_path));
  hash_combine(std::hash<std::string> {}(request.entry_point));
  for (const auto& def : request.defines) {
    hash_combine(std::hash<std::string> {}(def.name));
    if (def.value) {
      hash_combine(std::hash<std::string> {}(*def.value));
    } else {
      hash_combine(0xA5A5A5A5u);
    }
  }
  return seed;
}

auto ComputeShaderRequestKey(const ShaderRequest& request) -> uint64_t
{
  const auto canonical = CanonicalizeShaderRequest(ShaderRequest { request });

  auto seed = uint64_t { 0 };

  const auto mix = [&seed](const uint64_t value) {
    constexpr uint64_t kGoldenRatio = 0x9e3779b97f4a7c15ULL;
    seed ^= value + kGoldenRatio + (seed << 6U) + (seed >> 2U);
  };

  mix(static_cast<uint64_t>(
    static_cast<std::underlying_type_t<ShaderType>>(canonical.stage)));

  mix(oxygen::ComputeFNV1a64(
    canonical.source_path.data(), canonical.source_path.size()));
  mix(oxygen::ComputeFNV1a64(
    canonical.entry_point.data(), canonical.entry_point.size()));

  for (const auto& def : canonical.defines) {
    mix(oxygen::ComputeFNV1a64(def.name.data(), def.name.size()));
    if (def.value.has_value()) {
      mix(oxygen::ComputeFNV1a64(def.value->data(), def.value->size()));
    } else {
      mix(uint64_t { 0xA5A5A5A5u });
    }
  }

  return seed;
}

} // namespace oxygen::graphics
