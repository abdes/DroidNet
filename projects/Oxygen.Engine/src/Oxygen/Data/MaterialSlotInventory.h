//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

inline constexpr uint32_t kMaterialSlotInventorySchemaVersion = 1U;

//! Location of one material-bearing surface in the geometry's LOD/submesh
//! tables.
struct MaterialSlotBindingLocation final {
  uint32_t lod_index = 0;
  uint32_t submesh_index = 0;

  auto operator<=>(const MaterialSlotBindingLocation&) const = default;
};

//! One existing surface and its exact mesh-assigned default material identity.
struct MaterialSlotBinding final {
  uint32_t lod_index = 0;
  uint32_t submesh_index = 0;

  //! Nil retains the native omitted-reference/default-material sentinel.
  //! Validation never replaces it with a material key or resolves missing
  //! assets.
  AssetKey default_material_key {};

  auto operator==(const MaterialSlotBinding&) const -> bool = default;
};

//! One declared semantic slot. Equal labels or defaults never merge entries.
struct MaterialSlot final {
  MaterialSlotId slot_id {};
  std::string display_name;
  std::vector<MaterialSlotBinding> bindings;
};

//! Native inventory value model; slot order is presentation order only.
struct MaterialSlotInventory final {
  uint32_t schema_version = kMaterialSlotInventorySchemaVersion;
  AssetKey geometry_asset_key {};
  base::Sha256Digest layout_revision {};
  std::vector<MaterialSlot> slots;
};

enum class MaterialSlotValidationErrorCode : uint8_t {
  kUnsupportedSchemaVersion,
  kNilGeometryKey,
  kGeometryIdentityMismatch,
  kNilSlotId,
  kDuplicateSlotId,
  kEmptySlotBindings,
  kDuplicateBinding,
  kDuplicateGeometryBinding,
  kUnknownBinding,
  kMissingBinding,
  kDefaultMaterialMismatch,
  kLayoutRevisionMismatch,
};

//! Structured semantic failure with the affected slot/location when available.
struct MaterialSlotValidationError final {
  MaterialSlotValidationErrorCode code;
  std::optional<MaterialSlotId> slot_id;
  std::optional<MaterialSlotBindingLocation> binding;
};

//! Writes the versioned canonical layout record, without changing display
//! order.
/*!
 Version 1 is compact UTF-8 JSON, without BOM, whitespace or trailing newline:
 `{"schema_version":1,"slots":[{"slot_id":"...","bindings":[{"lod_index":0,
 "submesh_index":0,"default_material_key":"..."}]}]}`.

 Keys occur in the illustrated order. IDs use canonical lowercase UUID text;
 indices use unsigned decimal integers. Slots sort by their 16 ID bytes and
 bindings sort by (lod_index, submesh_index). Nil IDs, duplicate IDs, empty
 slots and any multiply owned binding are rejected. An empty slot array is valid
 for an empty surface layout. Geometry identity, display labels and all material
 payload/provenance data are excluded. No material defaults are normalized.
*/
OXGN_DATA_NDAPI auto WriteCanonicalMaterialSlotLayout(
  std::span<const MaterialSlot> slots)
  -> Result<std::string, MaterialSlotValidationError>;

//! SHA-256 of the exact UTF-8 bytes emitted by
//! WriteCanonicalMaterialSlotLayout.
OXGN_DATA_NDAPI auto ComputeMaterialSlotLayoutRevision(
  std::span<const MaterialSlot> slots)
  -> Result<base::Sha256Digest, MaterialSlotValidationError>;

//! Validates schema, identity, unique ownership, exact surface coverage and
//! hash.
/*!
 `geometry_bindings` is the authoritative native geometry surface inventory,
 supplied by the owning producer or loader, with one exact default key per
 existing LOD/submesh location. Defaults may differ across a slot's bindings.
 Duplicate locations in this input are rejected. Every supplied location must
 appear exactly once in `inventory`, with the same default material key.
 Neither argument is mutated, and labels do not participate in validation.
*/
OXGN_DATA_NDAPI auto ValidateMaterialSlotInventory(
  const MaterialSlotInventory& inventory, const AssetKey& geometry_asset_key,
  std::span<const MaterialSlotBinding> geometry_bindings)
  -> Result<void, MaterialSlotValidationError>;

} // namespace oxygen::data
