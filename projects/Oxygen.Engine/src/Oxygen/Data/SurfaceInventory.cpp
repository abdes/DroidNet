//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <fmt/base.h>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/SurfaceInventory.h>

namespace oxygen::data {
namespace {

  using ErrorCode = SurfaceValidationErrorCode;

  auto Failure(const ErrorCode code, std::optional<SurfaceId> surface_id = {},
    std::optional<SurfaceBindingLocation> binding = {})
    -> SurfaceValidationError
  {
    return { .code = code, .surface_id = surface_id, .binding = binding };
  }

  auto LocationOf(const SurfaceBinding& binding) -> SurfaceBindingLocation
  {
    return {
      .lod_index = binding.lod_index,
      .submesh_index = binding.submesh_index,
    };
  }

  auto HashRecord(const std::string& record) -> base::Sha256Digest
  {
    return base::ComputeSha256(
      std::as_bytes(std::span(record.data(), record.size())));
  }

} // namespace

auto WriteCanonicalSurfaceLayout(const std::span<const Surface> surfaces)
  -> Result<std::string, SurfaceValidationError>
{
  std::vector<const Surface*> ordered_surfaces;
  ordered_surfaces.reserve(surfaces.size());
  for (const auto& surface : surfaces) {
    ordered_surfaces.push_back(&surface);
  }
  std::ranges::sort(ordered_surfaces, {},
    [](const Surface* surface) -> SurfaceId { return surface->surface_id; });

  std::string record;
  fmt::format_to(std::back_inserter(record),
    "{{\"schema_version\":{},\"surfaces\":[", kSurfaceInventorySchemaVersion);
  std::set<SurfaceBindingLocation> owned_locations;
  std::optional<SurfaceId> previous_id;
  for (const auto* surface : ordered_surfaces) {
    if (surface->surface_id.IsNil()) {
      return Err(Failure(ErrorCode::kNilSurfaceId));
    }
    if (previous_id == surface->surface_id) {
      return Err(Failure(ErrorCode::kDuplicateSurfaceId, surface->surface_id));
    }
    if (surface->bindings.empty()) {
      return Err(
        Failure(ErrorCode::kEmptySurfaceBindings, surface->surface_id));
    }
    if (previous_id.has_value()) {
      record += ',';
    }
    previous_id = surface->surface_id;
    fmt::format_to(std::back_inserter(record),
      "{{\"surface_id\":\"{}\",\"bindings\":[", surface->surface_id);

    std::vector<SurfaceBinding> bindings(surface->bindings);
    std::ranges::sort(bindings, {}, LocationOf);
    bool first_binding = true;
    for (const auto& binding : bindings) {
      if (!owned_locations.insert(LocationOf(binding)).second) {
        return Err(Failure(ErrorCode::kDuplicateBinding, surface->surface_id,
          LocationOf(binding)));
      }
      if (!first_binding) {
        record += ',';
      }
      first_binding = false;
      fmt::format_to(std::back_inserter(record),
        "{{\"lod_index\":{},\"submesh_index\":{},"
        "\"default_material_key\":\"{}\"}}",
        binding.lod_index, binding.submesh_index, binding.default_material_key);
    }
    record += "]}";
  }
  record += "]}";
  return Ok(std::move(record));
}

auto ComputeSurfaceLayoutRevision(const std::span<const Surface> surfaces)
  -> Result<base::Sha256Digest, SurfaceValidationError>
{
  const auto canonical = WriteCanonicalSurfaceLayout(surfaces);
  if (!canonical.has_value()) {
    return Err(canonical.error());
  }
  return Ok(HashRecord(canonical.value()));
}

auto ValidateSurfaceInventory(const SurfaceInventory& inventory,
  const AssetKey& geometry_asset_key,
  const std::span<const SurfaceBinding> geometry_bindings)
  -> Result<void, SurfaceValidationError>
{
  if (inventory.schema_version != kSurfaceInventorySchemaVersion) {
    return Err(Failure(ErrorCode::kUnsupportedSchemaVersion));
  }
  if (inventory.geometry_asset_key.IsNil() || geometry_asset_key.IsNil()) {
    return Err(Failure(ErrorCode::kNilGeometryKey));
  }
  if (inventory.geometry_asset_key != geometry_asset_key) {
    return Err(Failure(ErrorCode::kGeometryIdentityMismatch));
  }

  const auto canonical = WriteCanonicalSurfaceLayout(inventory.surfaces);
  if (!canonical.has_value()) {
    return Err(canonical.error());
  }

  std::map<SurfaceBindingLocation, AssetKey> remaining_bindings;
  for (const auto& binding : geometry_bindings) {
    if (!remaining_bindings
          .emplace(LocationOf(binding), binding.default_material_key)
          .second) {
      return Err(
        Failure(ErrorCode::kDuplicateGeometryBinding, {}, LocationOf(binding)));
    }
  }
  for (const auto& surface : inventory.surfaces) {
    for (const auto& binding : surface.bindings) {
      const auto found = remaining_bindings.find(LocationOf(binding));
      if (found == remaining_bindings.end()) {
        return Err(Failure(
          ErrorCode::kUnknownBinding, surface.surface_id, LocationOf(binding)));
      }
      if (found->second != binding.default_material_key) {
        return Err(Failure(ErrorCode::kDefaultMaterialMismatch,
          surface.surface_id, LocationOf(binding)));
      }
      remaining_bindings.erase(found);
    }
  }
  if (!remaining_bindings.empty()) {
    return Err(Failure(
      ErrorCode::kMissingBinding, {}, remaining_bindings.begin()->first));
  }
  if (HashRecord(canonical.value()) != inventory.layout_revision) {
    return Err(Failure(ErrorCode::kLayoutRevisionMismatch));
  }
  return Result<void, SurfaceValidationError>::Ok();
}

} // namespace oxygen::data
