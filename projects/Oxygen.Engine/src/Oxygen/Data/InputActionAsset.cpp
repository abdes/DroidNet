//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <type_traits>

#include <Oxygen/Data/InputActionAsset.h>

namespace oxygen::data {

static_assert(std::is_trivially_copyable_v<pak::input::InputActionAssetDesc>,
  "InputActionAssetDesc must be trivially copyable");

InputActionAsset::InputActionAsset(
  AssetKey asset_key, pak::input::InputActionAssetDesc desc)
  : Asset(asset_key)
  , desc_(desc)
{
}

} // namespace oxygen::data
