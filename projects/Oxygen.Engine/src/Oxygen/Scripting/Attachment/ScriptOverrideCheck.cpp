//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include <fmt/format.h>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Scripting/Attachment/ScriptOverrideCheck.h>

namespace oxygen::scripting {

namespace {
  auto TypeName(const data::ScriptParam& value) -> std::string_view
  {
    return std::visit(
      [](const auto& v) -> std::string_view {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) {
          return "bool";
        } else if constexpr (std::is_same_v<T, int32_t>) {
          return "int";
        } else if constexpr (std::is_same_v<T, float>) {
          return "float";
        } else if constexpr (std::is_same_v<T, std::string>) {
          return "string";
        } else if constexpr (std::is_same_v<T, Vec2>) {
          return "vec2";
        } else if constexpr (std::is_same_v<T, Vec3>) {
          return "vec3";
        } else if constexpr (std::is_same_v<T, Vec4>) {
          return "vec4";
        } else if constexpr (std::is_same_v<T, data::ScriptAssetParam>) {
          return "asset";
        } else {
          return "node";
        }
      },
      value);
  }
} // namespace

auto CheckParameterOverride(const data::ScriptAsset& script,
  const std::string_view key, const data::ScriptParam& value,
  const std::optional<std::size_t> node_count) -> std::optional<std::string>
{
  const auto declared = script.TryGetParameter(key);
  if (!declared.has_value()) {
    return fmt::format("parameter '{}' is not declared by the script", key);
  }
  if (declared->get().index() != value.index()) {
    return fmt::format("parameter '{}' expects a {} value, not a {} value", key,
      TypeName(declared->get()), TypeName(value));
  }
  if (const auto* asset = std::get_if<data::ScriptAssetParam>(&value)) {
    const auto& kind
      = std::get<data::ScriptAssetParam>(declared->get()).asset_type;
    if (asset->asset_type != kind) {
      return fmt::format(
        "parameter '{}' refers to an asset of another kind", key);
    }
  }
  const auto* node = std::get_if<data::ScriptNodeParam>(&value);
  if (node != nullptr && node_count.has_value()
    && node->node_index != data::pak::scripting::kUnsetNodeParam
    && node->node_index >= *node_count) {
    return fmt::format(
      "parameter '{}' refers to node {}, which the scene does not have", key,
      node->node_index);
  }
  return std::nullopt;
}

} // namespace oxygen::scripting
