//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/SurfaceInventory.h>

namespace oxygen::content::import {

//! Recorded identity evidence for one source geometry.
/*!
 A geometry is found by its anchor, its name inside its model (`Hull`), so its
 surface ids survive moving or renaming the model. The inventory's geometry key
 is the one of the cook that recorded it.
*/
struct SurfaceGeometryProvenance final {
  std::string source_geometry_anchor;
  base::Sha256Digest source_layout_witness {};
  data::SurfaceInventory inventory {};
  std::map<uint32_t, data::SurfaceId> allocations;
};

//! Immutable, validated identity provenance shared by all meshes in an import.
//! Its serialized form lives in the model import record; publish a replacement
//! only together with the corresponding cooked geometry generation.
class SurfaceProvenance final {
public:
  //! The `schema_version` of the serialized form.
  static constexpr uint32_t kSchemaVersion = 2;

  //! An empty geometry list establishes a first-import source namespace.
  //! Throws std::invalid_argument for inconsistent identity/inventory data.
  OXGN_COOK_API explicit SurfaceProvenance(Uuid source_identity,
    std::vector<SurfaceGeometryProvenance> geometries = {});

  //! Validates schema and all recorded layouts once at the request boundary.
  //! The serialized form holds no identity: the model import record keeps it
  //! beside the provenance.
  OXGN_COOK_NDAPI static auto Parse(std::string_view json, Uuid source_identity)
    -> std::shared_ptr<const SurfaceProvenance>;

  OXGN_COOK_NDAPI auto Serialize() const -> std::string;

  [[nodiscard]] auto SourceIdentity() const noexcept -> const Uuid&
  {
    return source_identity_;
  }

  [[nodiscard]] auto Geometries() const noexcept
    -> std::span<const SurfaceGeometryProvenance>
  {
    return geometries_;
  }

  //! The geometry named `anchor` in its model, or null.
  OXGN_COOK_NDAPI auto FindGeometry(std::string_view anchor) const noexcept
    -> const SurfaceGeometryProvenance*;

private:
  Uuid source_identity_;
  std::vector<SurfaceGeometryProvenance> geometries_;
};

} // namespace oxygen::content::import
