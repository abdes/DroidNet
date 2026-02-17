//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <stdexcept>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ScriptAsset.h>

namespace oxygen::data {

namespace {

  // Internal helper
  auto ToScriptParam(const pak::ScriptParamRecord& record) -> ScriptParam
  {
    using pak::ScriptParamType;

    switch (record.type) {
    case ScriptParamType::kBool:
      return record.value.as_bool;
    case ScriptParamType::kInt32:
      return record.value.as_int32;
    case ScriptParamType::kFloat:
      return record.value.as_float;
    case ScriptParamType::kString:
      // String parameters are handled in ScriptAsset constructor after
      // null-termination validation.
      CHECK_F(false, "kString must be handled by ScriptAsset constructor");
      return {};
    case ScriptParamType::kVec2:
      return Vec2(record.value.as_vec[0], record.value.as_vec[1]);
    case ScriptParamType::kVec3:
      return Vec3(
        record.value.as_vec[0], record.value.as_vec[1], record.value.as_vec[2]);
    case ScriptParamType::kVec4:
      return Vec4(record.value.as_vec[0], record.value.as_vec[1],
        record.value.as_vec[2], record.value.as_vec[3]);
    case ScriptParamType::kNone:
    default:
      return {};
    }
  }

} // namespace

static_assert(std::is_trivially_copyable_v<pak::ScriptAssetDesc>,
  "ScriptAssetDesc must be trivially copyable");

ScriptAsset::ScriptAsset(AssetKey asset_key, pak::ScriptAssetDesc desc,
  const std::vector<pak::ScriptParamRecord>& params)
  : Asset(asset_key)
  , desc_(desc)
{
  params_.reserve(params.size());

  for (const auto& param : params) {
    const auto* const key_end = std::ranges::find(param.key, '\0');

    // Check key is null-terminated
    CHECK_F(key_end != std::end(param.key),
      "script param key is not null-terminated: {}",
      std::string_view(static_cast<const char*>(param.key), 16));

    ScriptParam value;
    if (param.type == pak::ScriptParamType::kString) {
      const auto& str = param.value.as_string;
      const auto* const str_end = std::ranges::find(str, '\0');
      CHECK_F(str_end != std::end(str),
        "script param string value is not null-terminated: {}",
        std::string_view(static_cast<const char*>(str), 16));
      value = std::string(std::begin(str), str_end);
    } else {
      value = ToScriptParam(param);
    }

    const std::string key(std::begin(param.key), key_end);
    const auto [_, inserted] = params_.insert_or_assign(key, std::move(value));
    if (!inserted) {
      LOG_F(
        WARNING, "duplicate script parameter key '{}': last value wins", key);
    }
  }
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

auto oxygen::data::ScriptAsset::HasParameter(
  std::string_view key) const noexcept -> bool
{
  return TryGetParameter(key).has_value();
}

auto oxygen::data::ScriptAsset::TryGetParameter(
  std::string_view key) const noexcept
  -> std::optional<std::reference_wrapper<const ScriptParam>>
{
  if (auto it = params_.find(std::string(key)); it != params_.end()) {
    return std::cref(it->second);
  }
  return std::nullopt;
}

auto oxygen::data::ScriptAsset::GetParameter(std::string_view key) const
  -> const ScriptParam&
{
  if (const auto value = TryGetParameter(key)) {
    return value->get();
  }
  throw std::out_of_range(
    "ScriptAsset parameter '" + std::string(key) + "' was not found");
}

} // namespace oxygen::data
