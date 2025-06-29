//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Hash.h>
#include <array>
#include <compare>
#include <cstdint>
#include <random>
#include <uuid.h> // stduuid: https://github.com/mariusbancila/stduuid

#include <Oxygen/Content/api_export.h>

namespace oxygen::content {

//! Asset type identifier for AssetKey
/*!
  AssetType defines the type of asset for identification and loading.
  - kGeometry: A geometry asset containing one or more LODs. Each LOD is a Mesh.
  - kMesh: A single mesh (vertex/index data), with zero or more mesh views.
  - kTexture, kShader, kMaterial, kAudio: Other asset types.
*/
enum class AssetType : uint8_t {
  kUnknown = 0,

  kGeometry, //!< Geometry asset (one or more LODs; each LOD is a Mesh)
  kMesh, //!< Single mesh (one LOD, one or more sub-meshes)
  kTexture,
  kShader,
  kMaterial,
  kAudio,
  // Extend as needed

  kMaxAssetType = 255 // Maximum value for AssetType
};

//! String representation of enum values in `Format`.
OXGN_CNTT_API auto to_string(AssetType value) noexcept -> const char*;

//! Uniquely identifies an asset in the system
/*!
  The 'guid' field is a 128-bit value stored as std::array<uint8_t, 16>.
  The 'variant' field is a 32-bit project-defined mask or flag value. The engine
  does not interpret this field; it is available for project-specific use (e.g.,
  localization, skin, region, quality, animation set, etc.). LODs are always
  built-in to geometry assets and are NOT represented by variant.

  Geometry asset structure:
    - Geometry: one or more LODs (indexed 0..N-1)
    - Each LOD: a Mesh asset
    - Each Mesh: zero or more MeshViews (sub-meshes), `no view` -> entire mesh
*/
struct AssetKey {
  std::array<uint8_t, 16> guid; //!< 128-bit GUID (raw bytes)
  uint32_t variant; //!< Project-defined mask/flag (not interpreted by engine)
  uint8_t version; //!< Asset version (up to 256 versions)
  AssetType type; //!< AssetType enum value (up to 256 types)
  uint16_t reserved = 0; //!< Reserved for future use or alignment

  auto operator<=>(const AssetKey&) const = default;
};
static_assert(sizeof(AssetKey) == 24);

//! Generates a random 128-bit GUID using stduuid and stores as array of bytes.
inline auto GenerateGuid() -> std::array<uint8_t, 16>
{
  std::random_device rd;
  auto seed_data = std::array<int, std::mt19937::state_size> {};
  std::generate(std::begin(seed_data), std::end(seed_data), std::ref(rd));
  std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
  std::mt19937 generator(seq);
  uuids::uuid_random_generator gen { generator };

  const auto id = gen();
  std::array<uint8_t, 16> arr;
  std::memcpy(arr.data(), id.as_bytes().data(), 16);
  return arr;
}

} // namespace oxygen::content

namespace std {

template <> struct hash<oxygen::content::AssetKey> {
  size_t operator()(const oxygen::content::AssetKey& key) const noexcept
  {
    size_t seed = 0;
    for (auto b : key.guid) {
      oxygen::HashCombine(seed, b);
    }
    oxygen::HashCombine(seed, key.variant);
    oxygen::HashCombine(seed, key.version);
    oxygen::HashCombine(seed, key.type);
    oxygen::HashCombine(seed, key.reserved);
    return seed;
  }
};

} // namespace std
