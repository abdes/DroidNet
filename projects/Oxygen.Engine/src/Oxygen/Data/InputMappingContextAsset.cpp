//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/PakFormat_input.h>
#include <Oxygen/Data/SourceOrigin.h>

namespace oxygen::data {

static_assert(
  std::is_trivially_copyable_v<pak::input::InputMappingContextAssetDesc>,
  "InputMappingContextAssetDesc must be trivially copyable");
static_assert(
  std::is_trivially_copyable_v<pak::input::InputActionMappingRecord>,
  "InputActionMappingRecord must be trivially copyable");
static_assert(std::is_trivially_copyable_v<pak::input::InputTriggerRecord>,
  "InputTriggerRecord must be trivially copyable");
static_assert(std::is_trivially_copyable_v<pak::input::InputTriggerAuxRecord>,
  "InputTriggerAuxRecord must be trivially copyable");

InputMappingContextAsset::InputMappingContextAsset(AssetKey asset_key,
  pak::input::InputMappingContextAssetDesc desc,
  std::vector<pak::input::InputActionMappingRecord> mappings,
  std::vector<pak::input::InputTriggerRecord> triggers,
  std::vector<pak::input::InputTriggerAuxRecord> trigger_aux,
  DescriptorStrings strings, SourceOrigin source_origin)
  : Asset(asset_key, source_origin)
  , desc_(desc)
  , mappings_(std::move(mappings))
  , triggers_(std::move(triggers))
  , trigger_aux_(std::move(trigger_aux))
  , strings_(std::move(strings))
{
}

} // namespace oxygen::data
