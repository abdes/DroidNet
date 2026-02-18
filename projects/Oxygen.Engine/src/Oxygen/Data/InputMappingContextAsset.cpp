//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <type_traits>

#include <Oxygen/Data/InputMappingContextAsset.h>

namespace oxygen::data {

static_assert(std::is_trivially_copyable_v<pak::InputMappingContextAssetDesc>,
  "InputMappingContextAssetDesc must be trivially copyable");
static_assert(std::is_trivially_copyable_v<pak::InputActionMappingRecord>,
  "InputActionMappingRecord must be trivially copyable");
static_assert(std::is_trivially_copyable_v<pak::InputTriggerRecord>,
  "InputTriggerRecord must be trivially copyable");
static_assert(std::is_trivially_copyable_v<pak::InputTriggerAuxRecord>,
  "InputTriggerAuxRecord must be trivially copyable");

InputMappingContextAsset::InputMappingContextAsset(AssetKey asset_key,
  pak::InputMappingContextAssetDesc desc,
  std::vector<pak::InputActionMappingRecord> mappings,
  std::vector<pak::InputTriggerRecord> triggers,
  std::vector<pak::InputTriggerAuxRecord> trigger_aux,
  std::vector<char> strings)
  : Asset(asset_key)
  , desc_(desc)
  , mappings_(std::move(mappings))
  , triggers_(std::move(triggers))
  , trigger_aux_(std::move(trigger_aux))
  , strings_(std::move(strings))
{
}

auto InputMappingContextAsset::TryGetString(
  const uint32_t offset) const noexcept -> std::optional<std::string_view>
{
  if (offset >= strings_.size()) {
    return std::nullopt;
  }
  const auto begin = strings_.begin() + static_cast<std::ptrdiff_t>(offset);
  const auto end = strings_.end();
  const auto nul = std::ranges::find(begin, end, '\0');
  if (nul == end) {
    return std::nullopt;
  }
  return std::string_view(
    &(*begin), static_cast<size_t>(std::distance(begin, nul)));
}

} // namespace oxygen::data
