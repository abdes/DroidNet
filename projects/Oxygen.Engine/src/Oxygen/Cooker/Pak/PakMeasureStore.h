//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::pak {

struct PakResourceTableSerializationInput final {
  uint32_t entry_count = 0;
  std::span<const PakResourcePlacementPlan> resources {};
};

struct PakScriptSlotTableSerializationInput final {
  uint32_t entry_count = 0;
  std::span<const PakScriptParamRangePlan> ranges {};
};

OXGN_COOK_NDAPI auto MeasurePayloadSourceSlice(
  const PakPayloadSourceSlicePlan& input) -> std::optional<uint64_t>;

OXGN_COOK_NDAPI auto StorePayloadSourceSlice(
  const PakPayloadSourceSlicePlan& input, std::vector<std::byte>& out_bytes)
  -> bool;

OXGN_COOK_NDAPI auto MeasureTextureTablePayload(
  const PakResourceTableSerializationInput& input) -> std::optional<uint64_t>;

OXGN_COOK_NDAPI auto StoreTextureTablePayload(
  const PakResourceTableSerializationInput& input,
  std::vector<std::byte>& out_bytes) -> bool;

OXGN_COOK_NDAPI auto MeasureBufferTablePayload(
  const PakResourceTableSerializationInput& input) -> std::optional<uint64_t>;

OXGN_COOK_NDAPI auto StoreBufferTablePayload(
  const PakResourceTableSerializationInput& input,
  std::vector<std::byte>& out_bytes) -> bool;

OXGN_COOK_NDAPI auto MeasureAudioTablePayload(
  const PakResourceTableSerializationInput& input) -> std::optional<uint64_t>;

OXGN_COOK_NDAPI auto StoreAudioTablePayload(
  const PakResourceTableSerializationInput& input,
  std::vector<std::byte>& out_bytes) -> bool;

OXGN_COOK_NDAPI auto MeasureScriptResourceTablePayload(
  const PakResourceTableSerializationInput& input) -> std::optional<uint64_t>;

OXGN_COOK_NDAPI auto StoreScriptResourceTablePayload(
  const PakResourceTableSerializationInput& input,
  std::vector<std::byte>& out_bytes) -> bool;

OXGN_COOK_NDAPI auto MeasureScriptSlotTablePayload(
  const PakScriptSlotTableSerializationInput& input) -> std::optional<uint64_t>;

OXGN_COOK_NDAPI auto StoreScriptSlotTablePayload(
  const PakScriptSlotTableSerializationInput& input,
  std::vector<std::byte>& out_bytes) -> bool;

OXGN_COOK_NDAPI auto MeasurePhysicsTablePayload(
  const PakResourceTableSerializationInput& input) -> std::optional<uint64_t>;

OXGN_COOK_NDAPI auto StorePhysicsTablePayload(
  const PakResourceTableSerializationInput& input,
  std::vector<std::byte>& out_bytes) -> bool;

OXGN_COOK_NDAPI auto MeasureAssetDirectoryPayload(
  std::span<const PakAssetDirectoryEntryPlan> entries)
  -> std::optional<uint64_t>;

OXGN_COOK_NDAPI auto StoreAssetDirectoryPayload(
  std::span<const PakAssetDirectoryEntryPlan> entries,
  std::vector<std::byte>& out_bytes) -> bool;

OXGN_COOK_NDAPI auto MeasureBrowseIndexPayload(
  std::span<const PakBrowseEntryPlan> entries) -> std::optional<uint64_t>;

OXGN_COOK_NDAPI auto StoreBrowseIndexPayload(
  std::span<const PakBrowseEntryPlan> entries,
  std::vector<std::byte>& out_bytes) -> bool;

} // namespace oxygen::content::pak
