//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Cooker/Pak/PakBuildPhase.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::pak {

enum class PakDiagnosticSeverity : uint8_t {
  kInfo,
  kWarning,
  kError,
};

OXGN_COOK_NDAPI auto to_string(PakDiagnosticSeverity value) noexcept
  -> std::string_view;

struct PakDiagnostic {
  PakDiagnosticSeverity severity = PakDiagnosticSeverity::kInfo;
  PakBuildPhase phase = PakBuildPhase::kPlanning;
  std::string code;
  std::string message;

  std::string asset_key;
  std::string resource_kind;
  std::string table_name;
  std::filesystem::path path;
  std::optional<uint64_t> offset;
};

struct PakBuildSummary {
  uint32_t diagnostics_info = 0;
  uint32_t diagnostics_warning = 0;
  uint32_t diagnostics_error = 0;

  uint32_t assets_processed = 0;
  uint32_t resources_processed = 0;

  uint32_t patch_created = 0;
  uint32_t patch_replaced = 0;
  uint32_t patch_deleted = 0;
  uint32_t patch_unchanged = 0;

  bool crc_computed = true;
};

struct PakBuildTelemetry {
  std::optional<std::chrono::microseconds> planning_duration;
  std::optional<std::chrono::microseconds> writing_duration;
  std::optional<std::chrono::microseconds> manifest_duration;
  std::optional<std::chrono::microseconds> total_duration;
};

} // namespace oxygen::content::pak
