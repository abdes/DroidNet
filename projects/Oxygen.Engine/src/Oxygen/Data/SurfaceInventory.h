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
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

inline constexpr uint32_t kSurfaceInventorySchemaVersion = 1U;

//! Location of one material-bearing surface in the geometry's LOD/submesh
//! tables.
struct SurfaceBindingLocation final {
  LodIndex lod_index;
  SubmeshIndex submesh_index;

  auto operator<=>(const SurfaceBindingLocation&) const = default;
};

//! One existing surface and its exact mesh-assigned default material identity.
struct SurfaceBinding final {
  LodIndex lod_index;
  SubmeshIndex submesh_index;

  //! Nil retains the native omitted-reference/default-material sentinel.
  //! Validation never replaces it with a material key or resolves missing
  //! assets.
  AssetKey default_material_key {};

  auto operator==(const SurfaceBinding&) const -> bool = default;
};

//! One declared semantic surface. Equal labels or defaults never merge entries.
struct Surface final {
  SurfaceId surface_id {};
  std::string display_name;
  std::vector<SurfaceBinding> bindings;
};

//! Native inventory value model; surface order is presentation order only.
struct SurfaceInventory final {
  uint32_t schema_version = kSurfaceInventorySchemaVersion;
  AssetKey geometry_asset_key {};
  base::Sha256Digest layout_revision {};
  std::vector<Surface> surfaces;
};

enum class SurfaceValidationErrorCode : uint8_t {
  kUnsupportedSchemaVersion,
  kNilGeometryKey,
  kGeometryIdentityMismatch,
  kNilSurfaceId,
  kDuplicateSurfaceId,
  kEmptySurfaceBindings,
  kDuplicateBinding,
  kDuplicateGeometryBinding,
  kUnknownBinding,
  kMissingBinding,
  kDefaultMaterialMismatch,
  kLayoutRevisionMismatch,
};

//! Structured semantic failure with the affected surface/location when
//! available.
struct SurfaceValidationError final {
  SurfaceValidationErrorCode code;
  std::optional<SurfaceId> surface_id;
  std::optional<SurfaceBindingLocation> binding;
};

//! Writes the versioned canonical layout record, without changing display
//! order.
/*!
 Version 1 is compact UTF-8 JSON, without BOM, whitespace or trailing newline:
 `{"schema_version":1,"surfaces":[{"surface_id":"...","bindings":[{"lod_index":0,
 "submesh_index":0,"default_material_key":"..."}]}]}`.

 Keys occur in the illustrated order. IDs use canonical lowercase UUID text;
 indices use unsigned decimal integers. Surfaces sort by their 16 ID bytes and
 bindings sort by (lod_index, submesh_index). Nil IDs, duplicate IDs, empty
 surfaces and any multiply owned binding are rejected. An empty surface array is
 valid for an empty surface layout. Geometry identity, display labels and all
 material payload/provenance data are excluded. No material defaults are
 normalized.
*/
OXGN_DATA_NDAPI auto WriteCanonicalSurfaceLayout(
  std::span<const Surface> surfaces)
  -> Result<std::string, SurfaceValidationError>;

//! SHA-256 of the exact UTF-8 bytes emitted by
//! WriteCanonicalSurfaceLayout.
OXGN_DATA_NDAPI auto ComputeSurfaceLayoutRevision(
  std::span<const Surface> surfaces)
  -> Result<base::Sha256Digest, SurfaceValidationError>;

//! Validates schema, identity, unique ownership, exact surface coverage and
//! hash.
/*!
 `geometry_bindings` is the authoritative native geometry surface inventory,
 supplied by the owning producer or loader, with one exact default key per
 existing LOD/submesh location. Defaults may differ across a surface's bindings.
 Duplicate locations in this input are rejected. Every supplied location must
 appear exactly once in `inventory`, with the same default material key.
 Neither argument is mutated, and labels do not participate in validation.
*/
OXGN_DATA_NDAPI auto ValidateSurfaceInventory(const SurfaceInventory& inventory,
  const AssetKey& geometry_asset_key,
  std::span<const SurfaceBinding> geometry_bindings)
  -> Result<void, SurfaceValidationError>;

} // namespace oxygen::data
