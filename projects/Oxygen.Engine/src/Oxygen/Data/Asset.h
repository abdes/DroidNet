//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::data {

//=== Asset Interface
//===--------------------------------------------------------//

//! Base interface for all asset types
/*!
  Provides immutable access to asset metadata as defined in AssetHeader.
  All asset types must implement this interface. Assets are immutable after
  creation; only getters are provided.

  @see AssetHeader, AssetType, AssetKey
*/
class Asset {
public:
  //! Returns the asset's name as a string view.
  virtual auto GetName() const noexcept -> std::string_view = 0;

  //! Returns the asset's type (see AssetType).
  virtual auto GetAssetType() const noexcept -> AssetType = 0;

  //! Returns the asset's unique key (see AssetKey).
  virtual auto GetAssetKey() const noexcept -> const AssetKey& = 0;

  //! Virtual destructor for interface.
  virtual ~Asset() = default;
};

} // namespace oxygen::data
