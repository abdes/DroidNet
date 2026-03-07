//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Cooker/Pak/PakPlan.h>

namespace oxygen::content::pak {

auto to_string(const PakPatchAction value) noexcept -> std::string_view
{
  switch (value) {
  case PakPatchAction::kCreate:
    return "Create";
  case PakPatchAction::kReplace:
    return "Replace";
  case PakPatchAction::kDelete:
    return "Delete";
  case PakPatchAction::kUnchanged:
    return "Unchanged";
  }

  return "__NotSupported__";
}

PakPlan::PakPlan(Data data) noexcept
  : data_(std::move(data))
{
}

auto PakPlan::Header() const noexcept -> const PakHeaderPlan&
{
  return data_.header;
}

auto PakPlan::Regions() const noexcept -> std::span<const PakRegionPlan>
{
  return data_.regions;
}

auto PakPlan::Tables() const noexcept -> std::span<const PakTablePlan>
{
  return data_.tables;
}

auto PakPlan::Assets() const noexcept -> std::span<const PakAssetPlacementPlan>
{
  return data_.assets;
}

auto PakPlan::Resources() const noexcept
  -> std::span<const PakResourcePlacementPlan>
{
  return data_.resources;
}

auto PakPlan::AssetPayloadSources() const noexcept
  -> std::span<const PakPayloadSourceSlicePlan>
{
  return data_.asset_payload_sources;
}

auto PakPlan::ResourcePayloadSources() const noexcept
  -> std::span<const PakPayloadSourceSlicePlan>
{
  return data_.resource_payload_sources;
}

auto PakPlan::ResourceDescriptorSources() const noexcept
  -> std::span<const PakPayloadSourceSlicePlan>
{
  return data_.resource_descriptor_sources;
}

auto PakPlan::Directory() const noexcept -> const PakDirectoryPlan&
{
  return data_.directory;
}

auto PakPlan::BrowseIndex() const noexcept -> const PakBrowseIndexPlan&
{
  return data_.browse_index;
}

auto PakPlan::Footer() const noexcept -> const PakFooterPlan&
{
  return data_.footer;
}

auto PakPlan::PatchActions() const noexcept
  -> std::span<const PakPatchActionRecord>
{
  return data_.patch_actions;
}

auto PakPlan::PatchClosure() const noexcept
  -> std::span<const PakPatchClosureRecord>
{
  return data_.patch_closure;
}

auto PakPlan::ScriptSlots() const noexcept -> std::span<const PakScriptSlotPlan>
{
  return data_.script_slots;
}

auto PakPlan::ScriptParamRecordCount() const noexcept -> uint32_t
{
  return data_.script_param_record_count;
}

auto PakPlan::PlannedFileSize() const noexcept -> uint64_t
{
  return data_.planned_file_size;
}

} // namespace oxygen::content::pak
