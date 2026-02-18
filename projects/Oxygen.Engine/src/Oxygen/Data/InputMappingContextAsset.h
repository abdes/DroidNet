//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

class InputMappingContextAsset final : public Asset {
  OXYGEN_TYPED(InputMappingContextAsset)

public:
  OXGN_DATA_API InputMappingContextAsset(AssetKey asset_key,
    pak::InputMappingContextAssetDesc desc,
    std::vector<pak::InputActionMappingRecord> mappings = {},
    std::vector<pak::InputTriggerRecord> triggers = {},
    std::vector<pak::InputTriggerAuxRecord> trigger_aux = {},
    std::vector<char> strings = {});

  ~InputMappingContextAsset() override = default;

  OXYGEN_MAKE_NON_COPYABLE(InputMappingContextAsset)
  OXYGEN_DEFAULT_MOVABLE(InputMappingContextAsset)

  [[nodiscard]] auto GetHeader() const noexcept
    -> const pak::AssetHeader& override
  {
    return desc_.header;
  }

  [[nodiscard]] auto GetFlags() const noexcept -> pak::InputMappingContextFlags
  {
    return desc_.flags;
  }

  [[nodiscard]] auto GetMappings() const noexcept
    -> std::span<const pak::InputActionMappingRecord>
  {
    return mappings_;
  }

  [[nodiscard]] auto GetTriggers() const noexcept
    -> std::span<const pak::InputTriggerRecord>
  {
    return triggers_;
  }

  [[nodiscard]] auto GetTriggerAuxRecords() const noexcept
    -> std::span<const pak::InputTriggerAuxRecord>
  {
    return trigger_aux_;
  }

  [[nodiscard]] auto GetStringTable() const noexcept -> std::span<const char>
  {
    return strings_;
  }

  [[nodiscard]] OXGN_DATA_API auto TryGetString(uint32_t offset) const noexcept
    -> std::optional<std::string_view>;

private:
  pak::InputMappingContextAssetDesc desc_ {};
  std::vector<pak::InputActionMappingRecord> mappings_;
  std::vector<pak::InputTriggerRecord> triggers_;
  std::vector<pak::InputTriggerAuxRecord> trigger_aux_;
  std::vector<char> strings_;
};

} // namespace oxygen::data
