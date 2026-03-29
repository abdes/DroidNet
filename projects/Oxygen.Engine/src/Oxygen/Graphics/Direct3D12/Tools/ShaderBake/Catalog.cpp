//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Catalog.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace oxygen::graphics::d3d12::tools::shader_bake {

namespace {

  auto FormatRequestKeyHex(const uint64_t request_key) -> std::string
  {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setw(16) << std::setfill('0')
           << request_key;
    return stream.str();
  }

  auto CompareDefines(
    std::span<const ShaderDefine> lhs, std::span<const ShaderDefine> rhs) -> int
  {
    const size_t common_size = std::min(lhs.size(), rhs.size());
    for (size_t i = 0; i < common_size; ++i) {
      if (lhs[i].name != rhs[i].name) {
        return lhs[i].name < rhs[i].name ? -1 : 1;
      }

      const auto lhs_value = lhs[i].value.value_or("");
      const auto rhs_value = rhs[i].value.value_or("");
      if (lhs_value != rhs_value) {
        return lhs_value < rhs_value ? -1 : 1;
      }
    }

    if (lhs.size() == rhs.size()) {
      return 0;
    }
    return lhs.size() < rhs.size() ? -1 : 1;
  }

  auto CompareRequestsForBuildOrder(
    const ExpandedShaderRequest& lhs, const ExpandedShaderRequest& rhs) -> bool
  {
    if (lhs.request.source_path != rhs.request.source_path) {
      return lhs.request.source_path < rhs.request.source_path;
    }

    const auto lhs_stage
      = static_cast<std::underlying_type_t<ShaderType>>(lhs.request.stage);
    const auto rhs_stage
      = static_cast<std::underlying_type_t<ShaderType>>(rhs.request.stage);
    if (lhs_stage != rhs_stage) {
      return lhs_stage < rhs_stage;
    }

    if (lhs.request.entry_point != rhs.request.entry_point) {
      return lhs.request.entry_point < rhs.request.entry_point;
    }

    return CompareDefines(lhs.request.defines, rhs.request.defines) < 0;
  }

} // namespace

auto ExpandShaderCatalog(std::span<const ShaderEntry> entries)
  -> std::vector<ExpandedShaderRequest>
{
  std::vector<ExpandedShaderRequest> expanded;
  expanded.reserve(entries.size());

  for (const auto& entry : entries) {
    const auto shader = ToShaderInfo(entry);
    auto request = CanonicalizeShaderRequest(ShaderRequest {
      .stage = shader.type,
      .source_path = shader.relative_path,
      .entry_point = shader.entry_point,
      .defines = shader.defines,
    });

    expanded.push_back(ExpandedShaderRequest {
      .request = std::move(request),
      .request_key = 0,
    });

    auto& current = expanded.back();
    current.request_key = ComputeShaderRequestKey(current.request);
  }

  std::sort(expanded.begin(), expanded.end(), CompareRequestsForBuildOrder);

  std::unordered_map<uint64_t, size_t> seen_request_keys;
  seen_request_keys.reserve(expanded.size());

  for (size_t i = 0; i < expanded.size(); ++i) {
    const auto [it, inserted]
      = seen_request_keys.emplace(expanded[i].request_key, i);
    if (inserted) {
      continue;
    }

    throw std::runtime_error("duplicate shader request key "
      + FormatRequestKeyHex(expanded[i].request_key) + ": "
      + FormatShaderLogKey(expanded[it->second].request) + " and "
      + FormatShaderLogKey(expanded[i].request));
  }

  return expanded;
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
