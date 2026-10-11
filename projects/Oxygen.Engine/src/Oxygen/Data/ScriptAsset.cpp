//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/SourceOrigin.h>

namespace oxygen::data {

auto TryGetScriptParamKey(
  const pak::scripting::ScriptParamRecord& record) noexcept
  -> std::optional<std::string_view>
{
  const auto* const begin = std::begin(record.key);
  const auto* const nul = std::ranges::find(record.key, '\0');
  if (nul == begin || nul == std::end(record.key)) {
    return std::nullopt;
  }
  return std::string_view(begin, static_cast<size_t>(nul - begin));
}

auto ToScriptParam(const pak::scripting::ScriptParamRecord& record)
  -> std::optional<ScriptParam>
{
  using pak::scripting::ScriptParamType;

  const auto& value = record.value;
  switch (record.type) {
  case ScriptParamType::kBool:
    return value.as_bool;
  case ScriptParamType::kInt32:
    return value.as_int32;
  case ScriptParamType::kFloat:
    return value.as_float;
  case ScriptParamType::kString: {
    const auto* const nul = std::ranges::find(value.as_string, '\0');
    if (nul == std::end(value.as_string)) {
      return std::nullopt;
    }
    return std::string(std::begin(value.as_string), nul);
  }
  case ScriptParamType::kVec2:
    return Vec2(value.as_vec[0], value.as_vec[1]);
  case ScriptParamType::kVec3:
    return Vec3(value.as_vec[0], value.as_vec[1], value.as_vec[2]);
  case ScriptParamType::kVec4:
    return Vec4(
      value.as_vec[0], value.as_vec[1], value.as_vec[2], value.as_vec[3]);
  case ScriptParamType::kAsset:
    return ScriptAssetParam {
      .key = value.as_asset.key,
      .asset_type = value.as_asset.asset_type,
    };
  case ScriptParamType::kNode:
    return ScriptNodeParam { .node_index = value.as_node_index };
  case ScriptParamType::kNone:
    break;
  }
  return std::nullopt;
}

auto MakeScriptParamRecord(const std::string_view key, const ScriptParam& value)
  -> std::optional<pak::scripting::ScriptParamRecord>
{
  using pak::scripting::ScriptParamType;

  auto record = pak::scripting::ScriptParamRecord {};
  if (key.empty() || key.size() >= std::size(record.key)) {
    return std::nullopt;
  }
  std::ranges::copy(key, std::begin(record.key));

  auto& out = record.value;
  const auto fits = std::visit(
    [&record, &out](const auto& v) -> bool {
      using T = std::decay_t<decltype(v)>;
      if constexpr (std::is_same_v<T, bool>) {
        record.type = ScriptParamType::kBool;
        out.as_bool = v;
      } else if constexpr (std::is_same_v<T, int32_t>) {
        record.type = ScriptParamType::kInt32;
        out.as_int32 = v;
      } else if constexpr (std::is_same_v<T, float>) {
        record.type = ScriptParamType::kFloat;
        out.as_float = v;
      } else if constexpr (std::is_same_v<T, std::string>) {
        if (v.size() >= std::size(out.as_string)) {
          return false;
        }
        record.type = ScriptParamType::kString;
        std::ranges::fill(out.as_string, '\0');
        std::ranges::copy(v, std::begin(out.as_string));
      } else if constexpr (std::is_same_v<T, Vec2>) {
        record.type = ScriptParamType::kVec2;
        out.as_vec[0] = v.x;
        out.as_vec[1] = v.y;
      } else if constexpr (std::is_same_v<T, Vec3>) {
        record.type = ScriptParamType::kVec3;
        out.as_vec[0] = v.x;
        out.as_vec[1] = v.y;
        out.as_vec[2] = v.z;
      } else if constexpr (std::is_same_v<T, Vec4>) {
        record.type = ScriptParamType::kVec4;
        out.as_vec[0] = v.x;
        out.as_vec[1] = v.y;
        out.as_vec[2] = v.z;
        out.as_vec[3] = v.w;
      } else if constexpr (std::is_same_v<T, ScriptAssetParam>) {
        record.type = ScriptParamType::kAsset;
        out.as_asset = { .key = v.key, .asset_type = v.asset_type };
      } else if constexpr (std::is_same_v<T, ScriptNodeParam>) {
        record.type = ScriptParamType::kNode;
        out.as_node_index = v.node_index;
      }
      return true;
    },
    value);
  if (!fits) {
    return std::nullopt;
  }
  return record;
}

static_assert(std::is_trivially_copyable_v<pak::scripting::ScriptAssetDesc>,
  "ScriptAssetDesc must be trivially copyable");

ScriptAsset::ScriptAsset(AssetKey asset_key,
  pak::scripting::ScriptAssetDesc desc,
  std::span<const pak::scripting::ScriptParamRecord> declared_params,
  SourceOrigin source_origin)
  : Asset(asset_key, source_origin)
  , desc_(desc)
{
  params_.reserve(declared_params.size());
  for (const auto& param : declared_params) {
    const auto key = TryGetScriptParamKey(param);
    if (!key) {
      throw std::invalid_argument("script parameter declaration has an "
                                  "empty or unterminated key");
    }
    if (HasParameter(*key)) {
      throw std::invalid_argument(
        "script parameter '" + std::string(*key) + "' is declared twice");
    }
    auto value = ToScriptParam(param);
    if (!value) {
      throw std::invalid_argument(
        "script parameter '" + std::string(*key) + "' has an invalid value");
    }
    params_.emplace_back(std::string(*key), std::move(*value));
  }
}

auto ScriptAsset::WithDeclarations(
  const std::span<const pak::scripting::ScriptParamRecord> declared_params)
  const -> std::shared_ptr<ScriptAsset>
{
  return std::make_shared<ScriptAsset>(
    GetAssetKey(), desc_, declared_params, GetSourceOrigin());
}

auto ScriptAsset::TryGetExternalSourcePath() const noexcept
  -> std::optional<std::string_view>
{
  const auto* const begin = std::begin(desc_.external_source_path);
  const auto* const end = std::end(desc_.external_source_path);
  const auto* const nul = std::ranges::find(desc_.external_source_path, '\0');
  if (nul == begin) {
    return std::nullopt;
  }
  if (nul == end) {
    LOG_F(WARNING, "script asset external_source_path is not null-terminated");
    return std::nullopt;
  }
  return std::string_view(begin, static_cast<size_t>(nul - begin));
}

auto ScriptAsset::GetExternalSourcePath() const -> std::string_view
{
  if (const auto path = TryGetExternalSourcePath()) {
    return *path;
  }
  throw std::out_of_range("ScriptAsset external source path is not set");
}

auto ScriptAsset::HasParameter(std::string_view key) const noexcept -> bool
{
  return TryGetParameter(key).has_value();
}

auto ScriptAsset::TryGetParameter(std::string_view key) const noexcept
  -> std::optional<std::reference_wrapper<const ScriptParam>>
{
  const auto it = std::ranges::find(params_, key,
    [](const auto& entry) -> std::string_view { return entry.first; });
  if (it != params_.end()) {
    return std::cref(it->second);
  }
  return std::nullopt;
}

auto ScriptAsset::GetParameter(std::string_view key) const -> const ScriptParam&
{
  if (const auto value = TryGetParameter(key)) {
    return value->get();
  }
  throw std::out_of_range(
    "ScriptAsset parameter '" + std::string(key) + "' was not found");
}

} // namespace oxygen::data
