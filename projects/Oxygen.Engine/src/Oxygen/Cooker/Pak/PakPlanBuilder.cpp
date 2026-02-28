//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>

#include <Oxygen/Cooker/Pak/PakPlanPolicy.h>
#include <Oxygen/Cooker/Pak/PakValidation.h>
#include <Oxygen/Data/PakFormat_audio.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string_view>

namespace {
namespace pak = oxygen::content::pak;
namespace data = oxygen::data;
namespace core = oxygen::data::pak::core;
namespace audio = oxygen::data::pak::audio;
namespace render = oxygen::data::pak::render;
namespace script = oxygen::data::pak::scripting;
namespace physics = oxygen::data::pak::physics;

constexpr std::array<uint8_t, 16> kZeroSourceKeyBytes {};

auto AddDiagnostic(std::vector<pak::PakDiagnostic>& diagnostics,
  const pak::PakDiagnosticSeverity severity, const pak::PakBuildPhase phase,
  const std::string_view code, const std::string_view message,
  const std::filesystem::path& path = {}) -> void
{
  diagnostics.push_back(pak::PakDiagnostic {
    .severity = severity,
    .phase = phase,
    .code = std::string(code),
    .message = std::string(message),
    .asset_key = {},
    .resource_kind = {},
    .table_name = {},
    .path = path,
    .offset = {},
  });
}

auto IsAssetKeyLess(const data::AssetKey& lhs, const data::AssetKey& rhs)
  -> bool
{
  return std::lexicographical_compare(
    lhs.guid.begin(), lhs.guid.end(), rhs.guid.begin(), rhs.guid.end());
}

auto MakeSkeletonTables() -> std::vector<pak::PakTablePlan>
{
  return {
    pak::PakTablePlan {
      .table_name = "texture_table",
      .offset = 0,
      .size_bytes = 0,
      .count = 0,
      .entry_size = static_cast<uint32_t>(sizeof(render::TextureResourceDesc)),
      .expected_entry_size
      = static_cast<uint32_t>(sizeof(render::TextureResourceDesc)),
      .alignment = 1,
      .index_zero_required = false,
      .index_zero_present = false,
      .index_zero_forbidden = false,
    },
    pak::PakTablePlan {
      .table_name = "buffer_table",
      .offset = 0,
      .size_bytes = 0,
      .count = 0,
      .entry_size = static_cast<uint32_t>(sizeof(core::BufferResourceDesc)),
      .expected_entry_size
      = static_cast<uint32_t>(sizeof(core::BufferResourceDesc)),
      .alignment = 1,
      .index_zero_required = false,
      .index_zero_present = false,
      .index_zero_forbidden = false,
    },
    pak::PakTablePlan {
      .table_name = "audio_table",
      .offset = 0,
      .size_bytes = 0,
      .count = 0,
      .entry_size = static_cast<uint32_t>(sizeof(audio::AudioResourceDesc)),
      .expected_entry_size
      = static_cast<uint32_t>(sizeof(audio::AudioResourceDesc)),
      .alignment = 1,
      .index_zero_required = false,
      .index_zero_present = false,
      .index_zero_forbidden = false,
    },
    pak::PakTablePlan {
      .table_name = "script_resource_table",
      .offset = 0,
      .size_bytes = 0,
      .count = 0,
      .entry_size = static_cast<uint32_t>(sizeof(script::ScriptResourceDesc)),
      .expected_entry_size
      = static_cast<uint32_t>(sizeof(script::ScriptResourceDesc)),
      .alignment = 1,
      .index_zero_required = false,
      .index_zero_present = false,
      .index_zero_forbidden = false,
    },
    pak::PakTablePlan {
      .table_name = "script_slot_table",
      .offset = 0,
      .size_bytes = 0,
      .count = 0,
      .entry_size = static_cast<uint32_t>(sizeof(script::ScriptSlotRecord)),
      .expected_entry_size
      = static_cast<uint32_t>(sizeof(script::ScriptSlotRecord)),
      .alignment = 1,
      .index_zero_required = false,
      .index_zero_present = false,
      .index_zero_forbidden = false,
    },
    pak::PakTablePlan {
      .table_name = "physics_resource_table",
      .offset = 0,
      .size_bytes = 0,
      .count = 0,
      .entry_size = static_cast<uint32_t>(sizeof(physics::PhysicsResourceDesc)),
      .expected_entry_size
      = static_cast<uint32_t>(sizeof(physics::PhysicsResourceDesc)),
      .alignment = 1,
      .index_zero_required = false,
      .index_zero_present = false,
      .index_zero_forbidden = false,
    },
  };
}

auto MakeSkeletonPlan(const pak::PakBuildRequest& request) -> pak::PakPlan::Data
{
  pak::PakPlan::Data data_plan {};

  data_plan.header = pak::PakHeaderPlan {
    .offset = 0,
    .size_bytes = static_cast<uint32_t>(sizeof(core::PakHeader)),
    .content_version = request.content_version,
    .source_key = request.source_key,
  };

  data_plan.tables = MakeSkeletonTables();
  data_plan.resources = {};
  data_plan.directory = pak::PakDirectoryPlan {
    .offset = 0,
    .size_bytes = 0,
    .entries = {},
  };
  data_plan.browse_index = pak::PakBrowseIndexPlan {
    .enabled = false,
    .offset = 0,
    .size_bytes = 0,
    .virtual_paths = {},
  };

  const auto footer_offset = static_cast<uint64_t>(sizeof(core::PakHeader));
  data_plan.footer = pak::PakFooterPlan {
    .offset = footer_offset,
    .size_bytes = static_cast<uint32_t>(sizeof(core::PakFooter)),
    .crc32_field_absolute_offset
    = footer_offset + offsetof(core::PakFooter, pak_crc32),
  };

  data_plan.script_param_record_count = 0;
  data_plan.script_param_ranges = {};
  data_plan.patch_closure = {};
  data_plan.planned_file_size
    = footer_offset + static_cast<uint64_t>(sizeof(core::PakFooter));

  return data_plan;
}

} // namespace

namespace oxygen::content::pak {

auto PakPlanBuilder::Build(const PakBuildRequest& request) const -> BuildResult
{
  const auto start = std::chrono::steady_clock::now();

  BuildResult output {};
  auto data_plan = MakeSkeletonPlan(request);
  const auto policy = DerivePakPlanPolicy(request);

  if (request.source_key.get() == kZeroSourceKeyBytes) {
    AddDiagnostic(output.diagnostics, PakDiagnosticSeverity::kError,
      PakBuildPhase::kPlanning, "pak.plan.source_key_zero",
      "Planning requires a non-zero source_key.");
  }

  if (request.output_pak_path.empty()) {
    AddDiagnostic(output.diagnostics, PakDiagnosticSeverity::kError,
      PakBuildPhase::kPlanning, "pak.plan.output_pak_path_empty",
      "Planning requires a non-empty output_pak_path.",
      request.output_pak_path);
  }

  if (policy.requires_base_catalogs && request.base_catalogs.empty()) {
    AddDiagnostic(output.diagnostics, PakDiagnosticSeverity::kError,
      PakBuildPhase::kPlanning, "pak.plan.patch_requires_base_catalogs",
      "Patch planning requires at least one base catalog.");
  }

  if (policy.mode == PakPlanMode::kPatch
    && request.output_manifest_path.empty()) {
    AddDiagnostic(output.diagnostics, PakDiagnosticSeverity::kError,
      PakBuildPhase::kPlanning, "pak.plan.patch_requires_manifest_path",
      "Patch planning requires a non-empty output_manifest_path.",
      request.output_manifest_path);
  }

  if (policy.mode == PakPlanMode::kFull && policy.emits_manifest
    && request.output_manifest_path.empty()) {
    AddDiagnostic(output.diagnostics, PakDiagnosticSeverity::kError,
      PakBuildPhase::kPlanning, "pak.plan.full_manifest_requires_manifest_path",
      "Full planning with emit_manifest_in_full=true requires "
      "output_manifest_path.",
      request.output_manifest_path);
  }

  if (policy.mode == PakPlanMode::kPatch) {
    std::vector<data::PakCatalogEntry> merged_base_entries;
    for (const auto& catalog : request.base_catalogs) {
      merged_base_entries.insert(merged_base_entries.end(),
        catalog.entries.begin(), catalog.entries.end());
    }

    std::ranges::sort(merged_base_entries,
      [](const data::PakCatalogEntry& lhs, const data::PakCatalogEntry& rhs) {
        return IsAssetKeyLess(lhs.asset_key, rhs.asset_key);
      });

    for (size_t i = 0; i < merged_base_entries.size(); ++i) {
      if (i > 0
        && merged_base_entries[i].asset_key
          == merged_base_entries[i - 1].asset_key
        && merged_base_entries[i].asset_type
          != merged_base_entries[i - 1].asset_type) {
        AddDiagnostic(output.diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.base_catalog_type_mismatch",
          "Same AssetKey appears in base catalogs with different asset_type.");
      }

      if (i > 0
        && merged_base_entries[i].asset_key
          == merged_base_entries[i - 1].asset_key) {
        continue;
      }

      data_plan.patch_actions.push_back(PakPatchActionRecord {
        .asset_key = merged_base_entries[i].asset_key,
        .asset_type = merged_base_entries[i].asset_type,
        .action = PakPatchAction::kDelete,
      });
    }
  }

  const auto validation
    = PakValidation::Validate(PakPlan(data_plan), policy, request);
  output.diagnostics.insert(output.diagnostics.end(),
    validation.diagnostics.begin(), validation.diagnostics.end());

  std::ranges::sort(
    output.diagnostics, [](const PakDiagnostic& lhs, const PakDiagnostic& rhs) {
      if (lhs.code != rhs.code) {
        return lhs.code < rhs.code;
      }
      if (lhs.phase != rhs.phase) {
        return static_cast<uint8_t>(lhs.phase)
          < static_cast<uint8_t>(rhs.phase);
      }
      return lhs.message < rhs.message;
    });

  const auto has_error = std::ranges::any_of(
    output.diagnostics, [](const PakDiagnostic& diagnostic) {
      return diagnostic.severity == PakDiagnosticSeverity::kError;
    });

  if (!has_error) {
    output.plan = PakPlan(std::move(data_plan));
    output.summary.assets_processed
      = static_cast<uint32_t>(output.plan->Assets().size());

    output.summary.resources_processed
      = static_cast<uint32_t>(output.plan->Resources().size());

    for (const auto& action : output.plan->PatchActions()) {
      switch (action.action) {
      case PakPatchAction::kCreate:
        ++output.summary.patch_created;
        break;
      case PakPatchAction::kReplace:
        ++output.summary.patch_replaced;
        break;
      case PakPatchAction::kDelete:
        ++output.summary.patch_deleted;
        break;
      case PakPatchAction::kUnchanged:
        ++output.summary.patch_unchanged;
        break;
      }
    }
  }

  output.planning_duration
    = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start);

  return output;
}

} // namespace oxygen::content::pak
