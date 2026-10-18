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
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

class InputMappingContextAsset final : public Asset {
  OXYGEN_TYPED(InputMappingContextAsset)

public:
  OXGN_DATA_API InputMappingContextAsset(AssetKey asset_key,
    pak::input::InputMappingContextAssetDesc desc,
    std::vector<pak::input::InputActionMappingRecord> mappings = {},
    std::vector<pak::input::InputTriggerRecord> triggers = {},
    std::vector<pak::input::InputTriggerAuxRecord> trigger_aux = {},
    DescriptorStrings strings = {}, SourceOrigin source_origin = {});

  ~InputMappingContextAsset() override = default;

  OXYGEN_MAKE_NON_COPYABLE(InputMappingContextAsset)
  OXYGEN_DEFAULT_MOVABLE(InputMappingContextAsset)

  [[nodiscard]] auto GetHeader() const noexcept
    -> const pak::core::AssetHeader& override
  {
    return desc_.header;
  }

  [[nodiscard]] auto GetFlags() const noexcept
    -> pak::input::InputMappingContextFlags
  {
    return desc_.flags;
  }

  [[nodiscard]] auto GetMappings() const noexcept
    -> std::span<const pak::input::InputActionMappingRecord>
  {
    return mappings_;
  }

  [[nodiscard]] auto GetDefaultPriority() const noexcept -> int32_t
  {
    return desc_.default_priority;
  }

  [[nodiscard]] auto GetTriggers() const noexcept
    -> std::span<const pak::input::InputTriggerRecord>
  {
    return triggers_;
  }

  [[nodiscard]] auto GetTriggerAuxRecords() const noexcept
    -> std::span<const pak::input::InputTriggerAuxRecord>
  {
    return trigger_aux_;
  }

  //! Resolves a string of the descriptor's string table, such as a slot name.
  [[nodiscard]] auto TryGetString(
    const pak::core::StringTableOffsetT offset) const noexcept
    -> std::optional<std::string_view>
  {
    return strings_.TryGet(offset);
  }

private:
  pak::input::InputMappingContextAssetDesc desc_ {};
  std::vector<pak::input::InputActionMappingRecord> mappings_;
  std::vector<pak::input::InputTriggerRecord> triggers_;
  std::vector<pak::input::InputTriggerAuxRecord> trigger_aux_;
  DescriptorStrings strings_;
};

} // namespace oxygen::data
