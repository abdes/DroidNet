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

//! Base class for all asset types.
/*!
  Provides immutable access to asset metadata as defined in AssetHeader. All
  asset types in the engine derive from this class and expose common metadata
  fields for identification, streaming, integrity, and project-defined flags.
  Asset metadata is loaded from the PAK file and assumed to be valid; no extra
  validations are performed here.

  ### Common Asset Metadata

  - **Asset Type**: Identifies the kind of asset (geometry, material, texture,
    etc.).
  - **Name**: Human-readable asset name, max length defined by
    pak::kMaxNameSize.
  - **Version**: Asset format version, used for compatibility and migration.
  - **Streaming Priority**: Loading priority (0=highest, 255=lowest), used to
    schedule asset streaming.
  - **Content Hash**: 64-bit integrity hash for verifying asset data.
  - **Variant Flags**: Project-defined bitfield for custom metadata,
    compatibility, or feature flags.

  These fields are always present and accessible for all asset types. Reserved
  fields in AssetHeader are for future expansion and are not interpreted by the
  engine.

  @see AssetHeader, AssetType, AssetKey
*/
class Asset : public Object {
public:
  virtual ~Asset() = default;

  //! Returns the asset type (geometry, material, texture, etc.).
  /*! @return AssetType enum value for this asset. */
  auto GetAssetType() const noexcept -> AssetType
  {
    return static_cast<AssetType>(GetHeader().asset_type);
  }

  //! Returns the asset name as a string view.
  /*!
    Returns the human-readable asset name from the header. The name is
    guaranteed not to exceed pak::kMaxNameSize and is intended for debugging,
    tools, and editor integration. Not used for runtime lookups.
    @return Asset name as a std::string_view.
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
  /*!
    Returns the asset format version as stored in the header. Used for
    compatibility checks and migration logic.
    @return Asset format version (0-255).
  */
  auto GetVersion() const noexcept -> uint8_t { return GetHeader().version; }

  //! Returns the streaming priority for asset loading.
  /*!
    Returns the streaming priority value (0=highest, 255=lowest) used to
    schedule asset loading and streaming order. Lower values indicate higher
    priority.
    @return Streaming priority (0-255).
  */
  auto GetStreamingPriority() const noexcept -> uint8_t
  {
    return GetHeader().streaming_priority;
  }

  //! Returns the content integrity hash.
  /*!
    Returns the 64-bit hash for verifying asset data integrity. Used to
    detect corruption or tampering.
    @return Content hash value.
  */
  auto GetContentHash() const noexcept -> uint64_t
  {
    return GetHeader().content_hash;
  }

  //! Returns the project-defined variant flags.
  /*!
    Returns the variant flags bitfield, which is project-defined and may
    encode compatibility, feature, or usage information. Not interpreted by
    the engine core.
    @return Variant flags bitfield.
  */
  auto GetVariantFlags() const noexcept -> uint32_t
  {
    return GetHeader().variant_flags;
  }

protected:
  //! Returns the asset header (to be implemented by derived classes).
  virtual auto GetHeader() const noexcept -> const pak::AssetHeader& = 0;
};

} // namespace oxygen::data
