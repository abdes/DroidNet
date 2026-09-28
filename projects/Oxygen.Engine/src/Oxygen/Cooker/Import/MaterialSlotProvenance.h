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
#include <Oxygen/Data/MaterialSlotInventory.h>

namespace oxygen::content::import {

//! Retained identity evidence for one source geometry.
struct MaterialSlotGeometryProvenance final {
  std::string source_geometry_anchor;
  base::Sha256Digest source_layout_witness {};
  data::MaterialSlotInventory inventory {};
  std::map<uint32_t, data::MaterialSlotId> allocations;
};

//! Immutable, validated identity provenance shared by all meshes in an import.
//! Retain its serialized form with source settings; publish a replacement only
//! together with the corresponding cooked geometry generation.
class MaterialSlotProvenance final {
public:
  //! An empty geometry list establishes a first-import source namespace.
  //! Throws std::invalid_argument for inconsistent identity/inventory data.
  OXGN_COOK_API explicit MaterialSlotProvenance(Uuid source_identity,
    std::vector<MaterialSlotGeometryProvenance> geometries = {});

  //! Validates schema and all recorded layouts once at the request boundary.
  OXGN_COOK_NDAPI static auto Parse(std::string_view json)
    -> std::shared_ptr<const MaterialSlotProvenance>;

  OXGN_COOK_NDAPI auto Serialize() const -> std::string;

  [[nodiscard]] auto SourceIdentity() const noexcept -> const Uuid&
  {
    return source_identity_;
  }

  [[nodiscard]] auto Geometries() const noexcept
    -> std::span<const MaterialSlotGeometryProvenance>
  {
    return geometries_;
  }

  OXGN_COOK_NDAPI auto FindGeometry(data::AssetKey key) const noexcept
    -> const MaterialSlotGeometryProvenance*;

private:
  Uuid source_identity_;
  std::vector<MaterialSlotGeometryProvenance> geometries_;
};

} // namespace oxygen::content::import
