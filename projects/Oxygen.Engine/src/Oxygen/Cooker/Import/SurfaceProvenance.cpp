//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/SurfaceInventory.h>

namespace oxygen::content::import {
namespace {

  auto DigestText(const base::Sha256Digest& digest) -> std::string
  {
    return fmt::format("{:02x}", fmt::join(digest, ""));
  }

  auto ReadDigest(const std::string& text) -> base::Sha256Digest
  {
    base::Sha256Digest result {};
    constexpr size_t kHexCharactersPerByte = 2U;
    if (text.size() != result.size() * kHexCharactersPerByte) {
      throw std::invalid_argument("Invalid surface digest length");
    }
    for (size_t index = 0U; index < result.size(); ++index) {
      const auto* begin = text.data() + (index * kHexCharactersPerByte);
      const auto* end = begin + kHexCharactersPerByte;
      constexpr int kHexRadix = 16;
      const auto parsed
        = std::from_chars(begin, end, result.at(index), kHexRadix);
      if (parsed.ec != std::errc {} || parsed.ptr != end) {
        throw std::invalid_argument("Invalid surface digest");
      }
    }
    return result;
  }

  auto ReadGeometry(const nlohmann::json& record) -> SurfaceGeometryProvenance
  {
    auto result = SurfaceGeometryProvenance {};
    result.source_geometry_anchor
      = record.at("source_geometry_anchor").get<std::string>();
    result.source_layout_witness
      = ReadDigest(record.at("source_layout_witness").get<std::string>());
    result.inventory.geometry_asset_key = data::AssetKey::FromString(
      record.at("geometry_asset_key").get<std::string>())
                                            .value();
    result.inventory.layout_revision
      = ReadDigest(record.at("layout_revision").get<std::string>());
    for (const auto& allocation : record.at("allocations")) {
      const auto id = data::SurfaceId::FromString(
        allocation.at("surface_id").get<std::string>())
                        .value();
      if (!result.allocations
            .emplace(allocation.at("declaration_key").get<uint32_t>(), id)
            .second) {
        throw std::invalid_argument("Duplicate surface declaration");
      }
    }
    for (const auto& surface_json : record.at("surfaces")) {
      auto surface = data::Surface {
        .surface_id = data::SurfaceId::FromString(
          surface_json.at("surface_id").get<std::string>())
          .value(),
        .display_name = surface_json.at("display_name").get<std::string>(),
        .bindings = {},
        .defaults = std::nullopt,
      };
      for (const auto& binding : surface_json.at("bindings")) {
        surface.bindings.push_back(data::SurfaceBinding {
          .lod_index
          = data::LodIndex { binding.at("lod_index").get<uint32_t>() },
          .submesh_index
          = data::SubmeshIndex { binding.at("submesh_index").get<uint32_t>() },
          .default_material_key = data::AssetKey::FromString(
            binding.at("default_material_key").get<std::string>())
            .value(),
        });
      }
      result.inventory.surfaces.push_back(std::move(surface));
    }
    return result;
  }

  auto WriteGeometry(const SurfaceGeometryProvenance& geometry)
    -> nlohmann::json
  {
    using nlohmann::json;
    auto allocations = json::array();
    for (const auto& [declaration, id] : geometry.allocations) {
      allocations.push_back({ { "declaration_key", declaration },
        { "surface_id", to_string(id) } });
    }
    auto surfaces = json::array();
    for (const auto& surface : geometry.inventory.surfaces) {
      auto bindings = json::array();
      for (const auto& binding : surface.bindings) {
        bindings.push_back({
          { "lod_index", binding.lod_index.get() },
          { "submesh_index", binding.submesh_index.get() },
          {
            "default_material_key",
            to_string(binding.default_material_key),
          },
        });
      }
      surfaces.push_back({
        { "surface_id", to_string(surface.surface_id) },
        { "display_name", surface.display_name },
        { "bindings", std::move(bindings) },
      });
    }
    return json {
      {
        "geometry_asset_key",
        to_string(geometry.inventory.geometry_asset_key),
      },
      { "source_geometry_anchor", geometry.source_geometry_anchor },
      { "source_layout_witness", DigestText(geometry.source_layout_witness) },
      { "layout_revision", DigestText(geometry.inventory.layout_revision) },
      { "allocations", std::move(allocations) },
      { "surfaces", std::move(surfaces) },
    };
  }

} // namespace

SurfaceProvenance::SurfaceProvenance(
  Uuid source_identity, std::vector<SurfaceGeometryProvenance> geometries)
  : source_identity_(source_identity)
  , geometries_(std::move(geometries))
{
  if (!source_identity_.IsValidV7()) {
    throw std::invalid_argument("Surfaces require a retained source UUIDv7");
  }
  std::ranges::sort(geometries_, {}, [](const auto& geometry) -> auto {
    return std::string_view(geometry.source_geometry_anchor);
  });
  const SurfaceGeometryProvenance* previous = nullptr;
  for (const auto& geometry : geometries_) {
    const auto& inventory = geometry.inventory;
    if (inventory.geometry_asset_key.IsNil()
      || geometry.source_geometry_anchor.empty()
      || (previous != nullptr
        && previous->source_geometry_anchor == geometry.source_geometry_anchor)
      || base::IsAllZero(geometry.source_layout_witness)) {
      throw std::invalid_argument(
        "Invalid or duplicate geometry surface provenance");
    }
    previous = &geometry;
    std::set<data::SurfaceId> allocated;
    for (const auto& [declaration, id] : geometry.allocations) {
      static_cast<void>(declaration);
      if (id.IsNil() || !allocated.insert(id).second) {
        throw std::invalid_argument("Invalid or duplicate surface allocation");
      }
    }
    std::vector<data::SurfaceBinding> bindings;
    std::set<data::SurfaceId> recorded;
    for (const auto& surface : inventory.surfaces) {
      recorded.insert(surface.surface_id);
      bindings.insert(
        bindings.end(), surface.bindings.begin(), surface.bindings.end());
    }
    if (allocated != recorded
      || !data::ValidateSurfaceInventory(
        inventory, inventory.geometry_asset_key, bindings)) {
      throw std::invalid_argument(
        "Surface provenance inventory is inconsistent");
    }
  }
}

auto SurfaceProvenance::Parse(const std::string_view text)
  -> std::shared_ptr<const SurfaceProvenance>
{
  const auto document = nlohmann::json::parse(text);
  nlohmann::json_schema::json_validator validator;
  validator.set_root_schema(nlohmann::json::parse(kSurfaceProvenanceSchema));
  validator.validate(document);
  const auto source_identity
    = Uuid::FromString(document.at("source_identity").get<std::string>())
        .value();
  std::vector<SurfaceGeometryProvenance> geometries;
  geometries.reserve(document.at("geometries").size());
  for (const auto& geometry : document.at("geometries")) {
    geometries.push_back(ReadGeometry(geometry));
  }
  return std::make_shared<const SurfaceProvenance>(
    source_identity, std::move(geometries));
}

auto SurfaceProvenance::Serialize() const -> std::string
{
  auto geometries = nlohmann::json::array();
  for (const auto& geometry : geometries_) {
    geometries.push_back(WriteGeometry(geometry));
  }
  return nlohmann::json {
    { "schema_version", 1 },
    { "source_identity", source_identity_.ToString() },
    { "geometries", std::move(geometries) },
  }
    .dump();
}

auto SurfaceProvenance::FindGeometry(
  const std::string_view anchor) const noexcept
  -> const SurfaceGeometryProvenance*
{
  const auto found = std::ranges::lower_bound(
    geometries_, anchor, {}, [](const auto& geometry) -> auto {
      return std::string_view(geometry.source_geometry_anchor);
    });
  return found != geometries_.end() && found->source_geometry_anchor == anchor
    ? &*found
    : nullptr;
}

} // namespace oxygen::content::import
