//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <type_traits>

#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/InputActionAsset.h>
#include <Oxygen/Data/PakFormat_input.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::data {

static_assert(std::is_trivially_copyable_v<pak::input::InputActionAssetDesc>,
  "InputActionAssetDesc must be trivially copyable");

InputActionAsset::InputActionAsset(AssetKey asset_key,
  pak::input::InputActionAssetDesc desc, SourceKey source_key)
  : Asset(asset_key, source_key)
  , desc_(desc)
{
}

} // namespace oxygen::data
