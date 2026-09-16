//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Data/MaterialSlotInventory.h>

namespace oxygen::data {
namespace {

  using ErrorCode = MaterialSlotValidationErrorCode;

  auto Failure(const ErrorCode code, std::optional<MaterialSlotId> slot_id = {},
    std::optional<MaterialSlotBindingLocation> binding = {})
    -> MaterialSlotValidationError
  {
    return { .code = code, .slot_id = slot_id, .binding = binding };
  }

  auto LocationOf(const MaterialSlotBinding& binding)
    -> MaterialSlotBindingLocation
  {
    return { .lod_index = binding.lod_index,
      .submesh_index = binding.submesh_index };
  }

  auto HashRecord(const std::string& record) -> base::Sha256Digest
  {
    return base::ComputeSha256(
      std::as_bytes(std::span(record.data(), record.size())));
  }

} // namespace

auto WriteCanonicalMaterialSlotLayout(const std::span<const MaterialSlot> slots)
  -> Result<std::string, MaterialSlotValidationError>
{
  std::vector<const MaterialSlot*> ordered_slots;
  ordered_slots.reserve(slots.size());
  for (const auto& slot : slots) {
    ordered_slots.push_back(&slot);
  }
  std::ranges::sort(
    ordered_slots, {}, [](const MaterialSlot* slot) { return slot->slot_id; });

  std::string record;
  fmt::format_to(std::back_inserter(record),
    "{{\"schema_version\":{},\"slots\":[", kMaterialSlotInventorySchemaVersion);
  std::set<MaterialSlotBindingLocation> owned_locations;
  std::optional<MaterialSlotId> previous_id;
  for (const auto* slot : ordered_slots) {
    if (slot->slot_id.IsNil()) {
      return Err(Failure(ErrorCode::kNilSlotId));
    }
    if (previous_id == slot->slot_id) {
      return Err(Failure(ErrorCode::kDuplicateSlotId, slot->slot_id));
    }
    if (slot->bindings.empty()) {
      return Err(Failure(ErrorCode::kEmptySlotBindings, slot->slot_id));
    }
    if (previous_id.has_value()) {
      record += ',';
    }
    previous_id = slot->slot_id;
    fmt::format_to(std::back_inserter(record),
      "{{\"slot_id\":\"{}\",\"bindings\":[", slot->slot_id);

    std::vector<MaterialSlotBinding> bindings(slot->bindings);
    std::ranges::sort(bindings, {}, LocationOf);
    bool first_binding = true;
    for (const auto& binding : bindings) {
      if (!owned_locations.insert(LocationOf(binding)).second) {
        return Err(Failure(
          ErrorCode::kDuplicateBinding, slot->slot_id, LocationOf(binding)));
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

auto ComputeMaterialSlotLayoutRevision(
  const std::span<const MaterialSlot> slots)
  -> Result<base::Sha256Digest, MaterialSlotValidationError>
{
  const auto canonical = WriteCanonicalMaterialSlotLayout(slots);
  if (!canonical.has_value()) {
    return Err(canonical.error());
  }
  return Ok(HashRecord(canonical.value()));
}

auto ValidateMaterialSlotInventory(const MaterialSlotInventory& inventory,
  const AssetKey& geometry_asset_key,
  const std::span<const MaterialSlotBinding> geometry_bindings)
  -> Result<void, MaterialSlotValidationError>
{
  if (inventory.schema_version != kMaterialSlotInventorySchemaVersion) {
    return Err(Failure(ErrorCode::kUnsupportedSchemaVersion));
  }
  if (inventory.geometry_asset_key.IsNil() || geometry_asset_key.IsNil()) {
    return Err(Failure(ErrorCode::kNilGeometryKey));
  }
  if (inventory.geometry_asset_key != geometry_asset_key) {
    return Err(Failure(ErrorCode::kGeometryIdentityMismatch));
  }

  const auto canonical = WriteCanonicalMaterialSlotLayout(inventory.slots);
  if (!canonical.has_value()) {
    return Err(canonical.error());
  }

  std::map<MaterialSlotBindingLocation, AssetKey> remaining_bindings;
  for (const auto& binding : geometry_bindings) {
    if (!remaining_bindings
          .emplace(LocationOf(binding), binding.default_material_key)
          .second) {
      return Err(
        Failure(ErrorCode::kDuplicateGeometryBinding, {}, LocationOf(binding)));
    }
  }
  for (const auto& slot : inventory.slots) {
    for (const auto& binding : slot.bindings) {
      const auto found = remaining_bindings.find(LocationOf(binding));
      if (found == remaining_bindings.end()) {
        return Err(Failure(
          ErrorCode::kUnknownBinding, slot.slot_id, LocationOf(binding)));
      }
      if (found->second != binding.default_material_key) {
        return Err(Failure(ErrorCode::kDefaultMaterialMismatch, slot.slot_id,
          LocationOf(binding)));
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
  return Result<void, MaterialSlotValidationError>::Ok();
}

} // namespace oxygen::data
