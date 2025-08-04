//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Composition/Object.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::data {

//=== Asset Basse Class ===---------------------------------------------------//

//! Base class for all asset types
/*!
  Provides immutable access to asset metadata as defined in AssetHeader. Assumes
  that the asset passed all validation checks in the loader. No extra
  validations are made here.

  @see AssetHeader, AssetType, AssetKey
*/
class Asset : public Object {
public:
  //! Virtual destructor for interface.
  virtual ~Asset() = default;

  //! Returns the asset type field from the header (for debugging).
  auto GetAssetType() const noexcept -> AssetType
  {
    return static_cast<AssetType>(GetHeader().asset_type);
  }

  //! Returns the asset name as a string view (for debugging/tools).
  /*!
    Returns the asset name from the header as a string view. The name is
    guaranteed not to exceed pak::kMaxNameSize. This is primarily for debugging
    and tools, not for runtime use.
  */
  auto GetAssetName() const noexcept -> std::string_view
  {
    const char* name = GetHeader().name;
    std::size_t len = 0;
    while (len < pak::kMaxNameSize && name[len] != '\0') {
      ++len;
    }
    return std::string_view(name, len);
  }

  //! Returns the asset format version.
  auto GetVersion() const noexcept -> uint8_t { return GetHeader().version; }

  //! Returns the streaming priority (0=highest, 255=lowest).
  auto GetStreamingPriority() const noexcept -> uint8_t
  {
    return GetHeader().streaming_priority;
  }

  //! Returns the content integrity hash.
  auto GetContentHash() const noexcept -> uint64_t
  {
    return GetHeader().content_hash;
  }

  //! Returns the project-defined variant flags.
  auto GetVariantFlags() const noexcept -> uint32_t
  {
    return GetHeader().variant_flags;
  }

protected:
  //! Returns the asset header (to be implemented by derived classes).
  virtual auto GetHeader() const noexcept -> const pak::AssetHeader& = 0;
};

} // namespace oxygen::data
