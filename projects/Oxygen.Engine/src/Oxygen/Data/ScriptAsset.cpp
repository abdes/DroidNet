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

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/SourceOrigin.h>

namespace oxygen::data {

auto TryGetScriptParamKey(const pak::scripting::ScriptParamRecord& record,
  const DescriptorStrings& strings) noexcept -> std::optional<std::string_view>
{
  const auto key = strings.TryGet(record.key_offset);
  if (!key || key->empty()) {
    return std::nullopt;
  }
  return key;
}

auto ToScriptParam(const pak::scripting::ScriptParamRecord& record,
  const DescriptorStrings& strings) -> std::optional<ScriptParam>
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
    const auto text = strings.TryGet(value.as_string_offset);
    if (!text) {
      return std::nullopt;
    }
    return std::string { *text };
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

auto DecodeScriptParameters(
  const std::span<const pak::scripting::ScriptParamRecord> records,
  const DescriptorStrings& strings) -> ScriptParameters
{
  auto params = ScriptParameters {};
  params.reserve(records.size());
  for (const auto& record : records) {
    const auto key = TryGetScriptParamKey(record, strings);
    if (!key) {
      throw std::invalid_argument(
        "script parameter declaration has an empty or invalid key");
    }
    auto value = ToScriptParam(record, strings);
    if (!value) {
      throw std::invalid_argument(
        "script parameter '" + std::string(*key) + "' has an invalid value");
    }
    params.emplace_back(std::string(*key), std::move(*value));
  }
  return params;
}

auto MakeScriptParamRecord(const std::string_view key, const ScriptParam& value,
  DescriptorStringsBuilder& strings)
  -> std::optional<pak::scripting::ScriptParamRecord>
{
  using pak::scripting::ScriptParamType;

  const auto holds_nul = [](const std::string_view text) -> bool {
    return text.find('\0') != std::string_view::npos;
  };
  if (key.empty() || holds_nul(key)) {
    return std::nullopt;
  }
  if (const auto* text = std::get_if<std::string>(&value);
    text != nullptr && holds_nul(*text)) {
    return std::nullopt;
  }

  auto record = pak::scripting::ScriptParamRecord {};
  record.key_offset = strings.Add(key);

  auto& out = record.value;
  std::visit(
    [&record, &out, &strings](const auto& v) -> void {
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
        record.type = ScriptParamType::kString;
        out.as_string_offset = strings.Add(v);
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
    },
    value);
  return record;
}

static_assert(std::is_trivially_copyable_v<pak::scripting::ScriptAssetDesc>,
  "ScriptAssetDesc must be trivially copyable");

ScriptAsset::ScriptAsset(AssetKey asset_key,
  pak::scripting::ScriptAssetDesc desc, ScriptParameters declared_params,
  std::string external_source_path, SourceOrigin source_origin)
  : Asset(asset_key, source_origin)
  , desc_(desc)
  , external_source_path_(std::move(external_source_path))
{
  params_.reserve(declared_params.size());
  for (auto& [key, value] : declared_params) {
    if (key.empty()) {
      throw std::invalid_argument(
        "script parameter declaration has an empty key");
    }
    if (HasParameter(key)) {
      throw std::invalid_argument(
        "script parameter '" + key + "' is declared twice");
    }
    params_.emplace_back(std::move(key), std::move(value));
  }
}

auto ScriptAsset::WithDeclarations(ScriptParameters declared_params) const
  -> std::shared_ptr<ScriptAsset>
{
  auto asset = std::make_shared<ScriptAsset>(GetAssetKey(), desc_,
    std::move(declared_params), external_source_path_, GetSourceOrigin());
  asset->SetAssetName(std::string { GetAssetName() });
  return asset;
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
